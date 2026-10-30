#include "render/dx12/dx12_hook.h"

#include "modules/module_manager.h"
#include "ui/clickgui.h"
#include "ui/ui_settings.h"
#include "utils/logger.h"
#include "input/input_manager.h"

#include <Windows.h>
#include <atomic>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <MinHook.h>
#include <mutex>
#include <vector>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
    enum class RenderBackend {
        None,
        D3D12,
        D3D11
    };

    constexpr UINT kFramesInFlight = 3;

    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain3*, UINT, UINT);
    using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    using ResizeBuffers1Fn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);
    using ExecuteCommandListsFn = void(__stdcall*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

    PresentFn g_originalPresent = nullptr;
    ResizeBuffersFn g_originalResizeBuffers = nullptr;
    ResizeBuffers1Fn g_originalResizeBuffers1 = nullptr;
    ExecuteCommandListsFn g_originalExecuteCommandLists = nullptr;

    void* g_presentTarget = nullptr;
    void* g_resizeBuffersTarget = nullptr;
    void* g_resizeBuffers1Target = nullptr;
    void* g_executeCommandListsTarget = nullptr;

    RenderBackend g_backend = RenderBackend::None;
    HWND g_window = nullptr;
    WNDPROC g_originalWndProc = nullptr;

    ID3D12Device* g_device = nullptr;
    ID3D12CommandQueue* g_commandQueue = nullptr;
    ID3D12DescriptorHeap* g_rtvHeap = nullptr;
    ID3D12DescriptorHeap* g_srvHeap = nullptr;
    ID3D12GraphicsCommandList* g_commandList = nullptr;
    ID3D12Fence* g_fence = nullptr;
    HANDLE g_fenceEvent = nullptr;

    struct FrameContext {
        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12Resource* renderTarget = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};
        UINT64 fenceValue = 0;
    };

    std::vector<FrameContext> g_frames;
    UINT g_bufferCount = 0;
    UINT g_rtvDescriptorSize = 0;
    DXGI_FORMAT g_renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT64 g_nextFenceValue = 1;

    ID3D11Device* g_d3d11Device = nullptr;
    ID3D11DeviceContext* g_d3d11Context = nullptr;
    ID3D11RenderTargetView* g_d3d11Rtv = nullptr;

    std::recursive_mutex g_mutex;
    std::atomic_bool g_initialized = false;
    std::atomic_bool g_shuttingDown = false;
    std::atomic_bool g_menuOpen = false;

    std::atomic<uint32_t> g_activeResizes = 0;
    std::atomic<int> g_resizeQuietFrames = 0;

    void release_gameplay_input(HWND hwnd) {
        if (!g_originalWndProc || !hwnd) return;

        InputManager::instance().clear_key_states();

        auto release_key = [hwnd](WPARAM key) {
            LPARAM keyUpLParam = 0xC0000001;
            const UINT scanCode = MapVirtualKey(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
            keyUpLParam |= static_cast<LPARAM>(scanCode) << 16;
            CallWindowProc(g_originalWndProc, hwnd, WM_KEYUP, key, keyUpLParam);
        };

        release_key('W');
        release_key('A');
        release_key('S');
        release_key('D');
        release_key(VK_SPACE);
        release_key(VK_SHIFT);
        release_key(VK_CONTROL);

        CallWindowProc(g_originalWndProc, hwnd, WM_LBUTTONUP, 0, 0);
        CallWindowProc(g_originalWndProc, hwnd, WM_RBUTTONUP, 0, 0);
        CallWindowProc(g_originalWndProc, hwnd, WM_MBUTTONUP, 0, 0);
    }

    void force_non_client_mouse(HWND hwnd) {
        if (!hwnd || !g_originalWndProc) return;

        ReleaseCapture();
        ClipCursor(nullptr);

        POINT pt{};
        GetCursorPos(&pt);
        const LPARAM screenPos = MAKELPARAM(static_cast<short>(pt.x), static_cast<short>(pt.y));

        CallWindowProc(g_originalWndProc, hwnd, WM_NCMOUSEMOVE, HTCAPTION, screenPos);
        CallWindowProc(g_originalWndProc, hwnd, WM_MOUSELEAVE, 0, 0);
    }

    LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        if (g_shuttingDown.load()) {
            return CallWindowProc(g_originalWndProc, hwnd, msg, wparam, lparam);
        }

        const bool keyDown = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        const bool keyUp = (msg == WM_KEYUP || msg == WM_SYSKEYUP);

        if (keyUp && wparam == VK_INSERT) {
            const bool opening = !g_menuOpen.load();
            g_menuOpen.store(opening);

            if (opening) {
                release_gameplay_input(hwnd);
                force_non_client_mouse(hwnd);
            }
            return 0;
        }

        if (keyDown && wparam == VK_ESCAPE && g_menuOpen.load()) {
            if (InputManager::instance().is_binding()) {
                InputManager::instance().cancel_binding();
                return 0;
            }
            g_menuOpen.store(false);
            return 0;
        }

        const bool isInputMessage = (
            msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN ||
            msg == WM_KEYUP || msg == WM_SYSKEYUP ||
            msg == WM_CHAR ||
            msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK ||
            msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP || msg == WM_RBUTTONDBLCLK ||
            msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP || msg == WM_MBUTTONDBLCLK ||
            msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP || msg == WM_XBUTTONDBLCLK ||
            msg == WM_MOUSEWHEEL || msg == WM_MOUSEMOVE
        );

        if (g_menuOpen.load()) {
            ReleaseCapture();
            ClipCursor(nullptr);

            if (InputManager::instance().is_binding()) {
                InputManager::instance().handle_win32_message(msg, wparam, lparam);
            }

            if (ImGui::GetCurrentContext()) {
                ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
            }

            if (isInputMessage) {
                return 0;
            }
        }

        const bool consumedByInput = InputManager::instance().handle_win32_message(msg, wparam, lparam);
        if (consumedByInput) {
            return 0;
        }

        return CallWindowProc(g_originalWndProc, hwnd, msg, wparam, lparam);
    }

    void wait_for_fence(UINT64 fenceValue) {
        if (!g_fence || !g_fenceEvent || fenceValue == 0) {
            return;
        }

        if (g_fence->GetCompletedValue() >= fenceValue) {
            return;
        }

        if (SUCCEEDED(g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent))) {
            WaitForSingleObject(g_fenceEvent, INFINITE);
        }
    }

    void wait_for_overlay_work() {
        if (g_backend == RenderBackend::D3D12) {
            for (FrameContext& frame : g_frames) {
                wait_for_fence(frame.fenceValue);
                frame.fenceValue = 0;
            }
        } else if (g_backend == RenderBackend::D3D11 && g_d3d11Context) {
            g_d3d11Context->Flush();
        }
    }

    void apply_overlay_style() {
        ui::apply_theme();
    }

    void release_frame_resources_dx12() {
        wait_for_overlay_work();

        for (FrameContext& frame : g_frames) {
            if (frame.renderTarget) {
                frame.renderTarget->Release();
                frame.renderTarget = nullptr;
            }
        }

        if (g_rtvHeap) {
            g_rtvHeap->Release();
            g_rtvHeap = nullptr;
        }

        g_frames.clear();
        g_bufferCount = 0;
    }

    void release_surfaces_dx11() {
        if (g_d3d11Context) {
            ID3D11RenderTargetView* nullView = nullptr;
            g_d3d11Context->OMSetRenderTargets(1, &nullView, nullptr);
            g_d3d11Context->ClearState();
            g_d3d11Context->Flush();
        }

        if (g_d3d11Rtv) {
            g_d3d11Rtv->Release();
            g_d3d11Rtv = nullptr;
        }
    }

    void cleanup_imgui() {
        if (!g_initialized.exchange(false)) {
            return;
        }

        LOG_INFO("Cleaning up ImGui & render resources...");

        wait_for_overlay_work();

        if (g_backend == RenderBackend::D3D12) {
            ImGui_ImplDX12_Shutdown();
        } else if (g_backend == RenderBackend::D3D11) {
            ImGui_ImplDX11_Shutdown();
        }

        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        if (g_originalWndProc && g_window) {
            SetWindowLongPtr(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalWndProc));
            g_originalWndProc = nullptr;
        }

        if (g_backend == RenderBackend::D3D12) {
            release_frame_resources_dx12();

            for (FrameContext& frame : g_frames) {
                if (frame.allocator) {
                    frame.allocator->Release();
                    frame.allocator = nullptr;
                }
            }

            if (g_commandList) {
                g_commandList->Release();
                g_commandList = nullptr;
            }

            if (g_srvHeap) {
                g_srvHeap->Release();
                g_srvHeap = nullptr;
            }

            if (g_fence) {
                g_fence->Release();
                g_fence = nullptr;
            }

            if (g_fenceEvent) {
                CloseHandle(g_fenceEvent);
                g_fenceEvent = nullptr;
            }

            if (g_device) {
                g_device->Release();
                g_device = nullptr;
            }

            g_commandQueue = nullptr;
        } else if (g_backend == RenderBackend::D3D11) {
            release_surfaces_dx11();

            if (g_d3d11Context) {
                g_d3d11Context->Release();
                g_d3d11Context = nullptr;
            }

            if (g_d3d11Device) {
                g_d3d11Device->Release();
                g_d3d11Device = nullptr;
            }
        }

        g_window = nullptr;
        g_backend = RenderBackend::None;
        LOG_INFO("ImGui render resources cleaned up successfully.");
    }

    bool create_render_targets_dx12(IDXGISwapChain3* swapChain) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(swapChain->GetDesc(&desc))) {
            return false;
        }

        g_bufferCount = desc.BufferCount;
        g_frames.resize(g_bufferCount);

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = g_bufferCount;
        rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        if (FAILED(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap)))) {
            return false;
        }

        g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < g_bufferCount; ++i) {
            FrameContext& frame = g_frames[i];

            if (!frame.allocator) {
                if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator)))) {
                    return false;
                }
            }

            if (FAILED(swapChain->GetBuffer(i, IID_PPV_ARGS(&frame.renderTarget)))) {
                return false;
            }

            frame.rtvHandle = rtvHandle;
            g_device->CreateRenderTargetView(frame.renderTarget, nullptr, frame.rtvHandle);
            rtvHandle.ptr += g_rtvDescriptorSize;
        }

        return true;
    }

    bool initialize_imgui_dx12(IDXGISwapChain3* swapChain) {
        if (!g_commandQueue) {
            return false;
        }

        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(swapChain->GetDesc(&desc)) || !desc.OutputWindow) {
            return false;
        }

        g_window = desc.OutputWindow;
        g_renderTargetFormat = desc.BufferDesc.Format == DXGI_FORMAT_UNKNOWN
            ? DXGI_FORMAT_R8G8B8A8_UNORM
            : desc.BufferDesc.Format;

        LOG_INFO("Initializing ImGui DX12 overlay for window handle: {:p}...", static_cast<void*>(g_window));

        if (!g_device) {
            if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&g_device)))) {
                LOG_ERROR("Failed to acquire D3D12 device from swapchain!");
                return false;
            }
        }

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = 1;
        srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        if (FAILED(g_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g_srvHeap)))) {
            LOG_ERROR("Failed to create SRV descriptor heap!");
            return false;
        }

        if (!create_render_targets_dx12(swapChain)) {
            LOG_ERROR("Failed to create render target views!");
            return false;
        }

        if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) {
            LOG_ERROR("Failed to create D3D12 synchronization fence!");
            return false;
        }

        g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!g_fenceEvent) {
            LOG_ERROR("Failed to create fence event handle!");
            return false;
        }

        if (FAILED(g_device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            g_frames[0].allocator,
            nullptr,
            IID_PPV_ARGS(&g_commandList)))) {
            LOG_ERROR("Failed to create graphics command list!");
            return false;
        }
        g_commandList->Close();

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        static const char* fontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
        if (std::filesystem::exists(fontPath)) {
            io.Fonts->AddFontFromFileTTF(fontPath, 22.0f);
        }

        apply_overlay_style();
        ui_settings::reset_ui_scale_cache();
        ImGui_ImplWin32_Init(g_window);
        ImGui_ImplDX12_Init(
            g_device,
            static_cast<int>(kFramesInFlight),
            g_renderTargetFormat,
            g_srvHeap,
            g_srvHeap->GetCPUDescriptorHandleForHeapStart(),
            g_srvHeap->GetGPUDescriptorHandleForHeapStart());

        g_originalWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtr(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wnd_proc)));

        g_initialized.store(true);
        LOG_INFO("ImGui DX12 overlay initialized successfully.");
        return true;
    }

    bool initialize_imgui_dx11(IDXGISwapChain3* swapChain) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(swapChain->GetDesc(&desc)) || !desc.OutputWindow) {
            return false;
        }

        g_window = desc.OutputWindow;

        LOG_INFO("Initializing ImGui D3D11 fallback overlay for window handle: {:p}...", static_cast<void*>(g_window));

        if (!g_d3d11Device) {
            if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&g_d3d11Device)))) {
                LOG_ERROR("Failed to acquire D3D11 device from swapchain!");
                return false;
            }
            g_d3d11Device->GetImmediateContext(&g_d3d11Context);
        }

        ID3D11Texture2D* backBuffer = nullptr;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer) {
            LOG_ERROR("Failed to acquire D3D11 backbuffer!");
            return false;
        }

        HRESULT hr = g_d3d11Device->CreateRenderTargetView(backBuffer, nullptr, &g_d3d11Rtv);
        backBuffer->Release();
        if (FAILED(hr)) {
            LOG_ERROR("Failed to create D3D11 RenderTargetView!");
            return false;
        }

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        static const char* fontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
        if (std::filesystem::exists(fontPath)) {
            io.Fonts->AddFontFromFileTTF(fontPath, 22.0f);
        }

        apply_overlay_style();
        ui_settings::reset_ui_scale_cache();
        ImGui_ImplWin32_Init(g_window);
        ImGui_ImplDX11_Init(g_d3d11Device, g_d3d11Context);

        g_originalWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtr(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wnd_proc)));

        g_initialized.store(true);
        LOG_INFO("ImGui D3D11 fallback overlay initialized successfully.");
        return true;
    }

    void render_imgui_dx12(IDXGISwapChain3* swapChain) {
        const UINT index = swapChain->GetCurrentBackBufferIndex();
        if (index >= g_frames.size()) {
            return;
        }

        FrameContext& frame = g_frames[index];
        wait_for_fence(frame.fenceValue);
        frame.fenceValue = 0;

        frame.allocator->Reset();

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = frame.renderTarget;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        g_commandList->Reset(frame.allocator, nullptr);
        g_commandList->ResourceBarrier(1, &barrier);
        g_commandList->OMSetRenderTargets(1, &frame.rtvHandle, FALSE, nullptr);
        g_commandList->SetDescriptorHeaps(1, &g_srvHeap);

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ui_settings::apply_ui_scale();
        ImGui::NewFrame();

        ModuleManager::instance().on_imgui_render();

        if (g_menuOpen.load()) {
            clickgui::render();
        }

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_commandList);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        g_commandList->ResourceBarrier(1, &barrier);
        g_commandList->Close();

        ID3D12CommandList* lists[] = { g_commandList };
        g_commandQueue->ExecuteCommandLists(1, lists);

        const UINT64 fenceValue = g_nextFenceValue++;
        if (SUCCEEDED(g_commandQueue->Signal(g_fence, fenceValue))) {
            frame.fenceValue = fenceValue;
        }
    }

    void render_imgui_dx11(IDXGISwapChain3* swapChain) {
        if (!g_d3d11Rtv) {
            ID3D11Texture2D* backBuffer = nullptr;
            if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) && backBuffer) {
                g_d3d11Device->CreateRenderTargetView(backBuffer, nullptr, &g_d3d11Rtv);
                backBuffer->Release();
            }
        }

        if (!g_d3d11Rtv) return;

        g_d3d11Context->OMSetRenderTargets(1, &g_d3d11Rtv, nullptr);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ui_settings::apply_ui_scale();
        ImGui::NewFrame();

        ModuleManager::instance().on_imgui_render();

        if (g_menuOpen.load()) {
            clickgui::render();
        }

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    void __stdcall hook_execute_command_lists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
        if (g_activeResizes.load(std::memory_order_acquire) > 0) {
            if (g_originalExecuteCommandLists) {
                g_originalExecuteCommandLists(queue, count, lists);
            }
            return;
        }

        if (!g_commandQueue && queue) {
            D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
            if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
                g_commandQueue = queue;
            }
        }

        if (g_originalExecuteCommandLists) {
            g_originalExecuteCommandLists(queue, count, lists);
        }
    }

    HRESULT __stdcall hook_present(IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags) {
        if (!swapChain || !g_originalPresent) return S_OK;

        if (flags & DXGI_PRESENT_TEST) {
            return g_originalPresent(swapChain, syncInterval, flags);
        }

        if (!g_shuttingDown.load()) {
            if (g_activeResizes.load(std::memory_order_acquire) > 0) {
                return g_originalPresent(swapChain, syncInterval, flags);
            }

            const int quiet = g_resizeQuietFrames.load(std::memory_order_acquire);
            if (quiet > 0) {
                g_resizeQuietFrames.store(quiet - 1, std::memory_order_release);
                return g_originalPresent(swapChain, syncInterval, flags);
            }

            if (g_window) {
                if (IsIconic(g_window)) {
                    return g_originalPresent(swapChain, syncInterval, flags);
                }
                RECT cr{};
                GetClientRect(g_window, &cr);
                if ((cr.right - cr.left) < 16 || (cr.bottom - cr.top) < 16) {
                    return g_originalPresent(swapChain, syncInterval, flags);
                }
            }

            std::lock_guard lock(g_mutex);

            if (g_backend == RenderBackend::None) {
                ID3D12Device* d12 = nullptr;
                if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&d12))) && d12) {
                    g_device = d12;
                    g_backend = RenderBackend::D3D12;
                    LOG_INFO("Detected DirectX 12 render backend.");
                } else {
                    ID3D11Device* d11 = nullptr;
                    if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&d11))) && d11) {
                        g_d3d11Device = d11;
                        g_d3d11Device->GetImmediateContext(&g_d3d11Context);
                        g_backend = RenderBackend::D3D11;
                        LOG_INFO("Detected DirectX 11 fallback render backend.");
                    }
                }
            }

            if (!g_initialized.load()) {
                if (g_backend == RenderBackend::D3D12) {
                    if (!g_commandQueue) {
                        ID3D12CommandQueue* scQueue = nullptr;
                        if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&scQueue))) && scQueue) {
                            if (scQueue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
                                g_commandQueue = scQueue;
                            } else {
                                scQueue->Release();
                            }
                        }
                    }
                    if (g_commandQueue) {
                        initialize_imgui_dx12(swapChain);
                    }
                } else if (g_backend == RenderBackend::D3D11) {
                    initialize_imgui_dx11(swapChain);
                }
            }

            if (g_initialized.load()) {
                if (g_backend == RenderBackend::D3D12) {
                    if (g_frames.empty() || !g_rtvHeap) {
                        create_render_targets_dx12(swapChain);
                    }
                    if (!g_frames.empty()) {
                        render_imgui_dx12(swapChain);
                    }
                } else if (g_backend == RenderBackend::D3D11) {
                    render_imgui_dx11(swapChain);
                }
            }
        }

        return g_originalPresent(swapChain, syncInterval, flags);
    }

    HRESULT __stdcall hook_resize_buffers(
        IDXGISwapChain* swapChain,
        UINT bufferCount,
        UINT width,
        UINT height,
        DXGI_FORMAT newFormat,
        UINT swapChainFlags)
    {
        if (!swapChain || !g_originalResizeBuffers) return E_INVALIDARG;

        g_activeResizes.fetch_add(1, std::memory_order_acq_rel);
        HRESULT hr = S_OK;
        {
            std::lock_guard lock(g_mutex);

            if (g_initialized.load()) {
                if (g_backend == RenderBackend::D3D12) {
                    wait_for_overlay_work();
                    release_frame_resources_dx12();
                    wait_for_overlay_work();
                } else if (g_backend == RenderBackend::D3D11) {
                    release_surfaces_dx11();
                }
            }

            hr = g_originalResizeBuffers(swapChain, bufferCount, width, height, newFormat, swapChainFlags);
        }

        g_resizeQuietFrames.store(4, std::memory_order_release);
        g_activeResizes.fetch_sub(1, std::memory_order_acq_rel);
        return hr;
    }

    HRESULT __stdcall hook_resize_buffers1(
        IDXGISwapChain* swapChain,
        UINT bufferCount,
        UINT width,
        UINT height,
        DXGI_FORMAT newFormat,
        UINT swapChainFlags,
        const UINT* pCreationNodeMask,
        IUnknown* const* ppPresentQueue)
    {
        if (!swapChain || !g_originalResizeBuffers1) return E_INVALIDARG;

        if (ppPresentQueue && *ppPresentQueue) {
            ID3D12CommandQueue* queue = nullptr;
            if (SUCCEEDED((*ppPresentQueue)->QueryInterface(IID_PPV_ARGS(&queue))) && queue) {
                if (queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
                    g_commandQueue = queue;
                }
                queue->Release();
            }
        }

        g_activeResizes.fetch_add(1, std::memory_order_acq_rel);
        HRESULT hr = S_OK;
        {
            std::lock_guard lock(g_mutex);

            if (g_initialized.load()) {
                if (g_backend == RenderBackend::D3D12) {
                    wait_for_overlay_work();
                    release_frame_resources_dx12();
                    wait_for_overlay_work();
                } else if (g_backend == RenderBackend::D3D11) {
                    release_surfaces_dx11();
                }
            }

            hr = g_originalResizeBuffers1(swapChain, bufferCount, width, height, newFormat, swapChainFlags, pCreationNodeMask, ppPresentQueue);
        }

        g_resizeQuietFrames.store(4, std::memory_order_release);
        g_activeResizes.fetch_sub(1, std::memory_order_acq_rel);
        return hr;
    }
}

namespace dx12_hook {
    bool start(HMODULE) {
        g_shuttingDown.store(false);

        LOG_INFO("Creating dummy DXGI swapchain to obtain vtable...");

        Microsoft::WRL::ComPtr<IDXGIFactory> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> dctx;

        if (FAILED(CreateDXGIFactory(IID_PPV_ARGS(&factory))) || !factory) {
            LOG_ERROR("Failed to create DXGI factory!");
            return false;
        }
        if (FAILED(factory->EnumAdapters(0, adapter.GetAddressOf())) || !adapter) {
            LOG_ERROR("Failed to enumerate DXGI adapter!");
            return false;
        }

        WNDCLASSEXW wnd = { sizeof(WNDCLASSEXW) };
        wnd.hInstance = GetModuleHandle(nullptr);
        wnd.lpszClassName = L"modbase_dummy";
        wnd.lpfnWndProc = DefWindowProcW;
        RegisterClassExW(&wnd);

        HWND hWnd = CreateWindowExW(0, L"modbase_dummy", L"", WS_MINIMIZEBOX, 0, 0, 100, 100, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferCount = 2;
        swapChainDesc.BufferDesc.Width = 100;
        swapChainDesc.BufferDesc.Height = 100;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.OutputWindow = hWnd;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Windowed = TRUE;

        D3D_FEATURE_LEVEL lvl[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, lvl, 2, D3D11_SDK_VERSION,
                                                    &swapChainDesc, swapChain.GetAddressOf(), device.GetAddressOf(),
                                                    &featureLevel, dctx.GetAddressOf());

        uintptr_t* vftable = swapChain ? *reinterpret_cast<uintptr_t**>(swapChain.Get()) : nullptr;
        uintptr_t* cqueueVftable = nullptr;

        HMODULE hD3D12 = GetModuleHandleA("d3d12.dll");
        if (!hD3D12) {
            hD3D12 = LoadLibraryA("d3d12.dll");
        }
        if (hD3D12) {
            typedef HRESULT(WINAPI* PFN_D3D12_CREATE_DEVICE)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
            auto pfnD3D12CreateDevice = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(hD3D12, "D3D12CreateDevice"));
            if (pfnD3D12CreateDevice && adapter) {
                Microsoft::WRL::ComPtr<ID3D12Device> device12;
                if (SUCCEEDED(pfnD3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12))) && device12) {
                    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
                    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
                    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                    Microsoft::WRL::ComPtr<ID3D12CommandQueue> cqueue;
                    if (SUCCEEDED(device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cqueue))) && cqueue) {
                        cqueueVftable = *reinterpret_cast<uintptr_t**>(cqueue.Get());
                    }
                }
            }
        }

        if (hWnd) DestroyWindow(hWnd);
        UnregisterClassW(L"modbase_dummy", GetModuleHandle(nullptr));

        if (!vftable) {
            LOG_ERROR("Failed to obtain DXGI SwapChain vtable!");
            return false;
        }

        g_presentTarget = reinterpret_cast<void*>(vftable[8]);
        if (MH_CreateHook(g_presentTarget, reinterpret_cast<void*>(&hook_present), reinterpret_cast<void**>(&g_originalPresent)) != MH_OK ||
            MH_EnableHook(g_presentTarget) != MH_OK) {
            LOG_ERROR("Failed to hook Present!");
            return false;
        }

        g_resizeBuffersTarget = reinterpret_cast<void*>(vftable[13]);
        if (MH_CreateHook(g_resizeBuffersTarget, reinterpret_cast<void*>(&hook_resize_buffers), reinterpret_cast<void**>(&g_originalResizeBuffers)) != MH_OK ||
            MH_EnableHook(g_resizeBuffersTarget) != MH_OK) {
            LOG_ERROR("Failed to hook ResizeBuffers!");
            return false;
        }

        if (vftable[39]) {
            g_resizeBuffers1Target = reinterpret_cast<void*>(vftable[39]);
            if (MH_CreateHook(g_resizeBuffers1Target, reinterpret_cast<void*>(&hook_resize_buffers1), reinterpret_cast<void**>(&g_originalResizeBuffers1)) == MH_OK) {
                MH_EnableHook(g_resizeBuffers1Target);
                LOG_INFO("Hooked ResizeBuffers1 successfully (slot 39).");
            }
        }

        if (cqueueVftable && cqueueVftable[10]) {
            g_executeCommandListsTarget = reinterpret_cast<void*>(cqueueVftable[10]);
            if (MH_CreateHook(g_executeCommandListsTarget, reinterpret_cast<void*>(&hook_execute_command_lists), reinterpret_cast<void**>(&g_originalExecuteCommandLists)) == MH_OK) {
                MH_EnableHook(g_executeCommandListsTarget);
                LOG_INFO("Hooked D3D12 ExecuteCommandLists successfully.");
            }
        }

        LOG_INFO("Render hooks initialized successfully (supporting DX12 & DX11 fallback).");
        return true;
    }

    void stop() {
        LOG_INFO("Stopping render hooks...");
        g_shuttingDown.store(true);

        {
            std::lock_guard lock(g_mutex);
            cleanup_imgui();
        }

        if (g_executeCommandListsTarget) {
            MH_DisableHook(g_executeCommandListsTarget);
            MH_RemoveHook(g_executeCommandListsTarget);
            g_executeCommandListsTarget = nullptr;
        }
        if (g_resizeBuffers1Target) {
            MH_DisableHook(g_resizeBuffers1Target);
            MH_RemoveHook(g_resizeBuffers1Target);
            g_resizeBuffers1Target = nullptr;
        }
        if (g_resizeBuffersTarget) {
            MH_DisableHook(g_resizeBuffersTarget);
            MH_RemoveHook(g_resizeBuffersTarget);
            g_resizeBuffersTarget = nullptr;
        }
        if (g_presentTarget) {
            MH_DisableHook(g_presentTarget);
            MH_RemoveHook(g_presentTarget);
            g_presentTarget = nullptr;
        }

        LOG_INFO("Render hooks stopped.");
    }

    bool is_menu_open() {
        return g_menuOpen.load();
    }

    void set_menu_open(bool open) {
        g_menuOpen.store(open);
    }
}
