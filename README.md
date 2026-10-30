# MCBE DX12 Internal Client Template

A modular C++20 template for building internal DirectX 12 clients for Minecraft Bedrock Edition. Built on a clean event-driven architecture featuring an EnTT-based SDK, dynamic vtable hooking, dual-pipeline rendering (Native Tessellator 3D + DirectX 12 ImGui overlay), Win32 input routing, and a JSON profile configuration system.

> [!NOTE]
> **Game Version & Compatibility**: Current signatures, vtable slots, and struct offsets are updated for Minecraft Bedrock **v26.60**.
> 
> **Template Base**: This repository is designed as a foundational template/base. It may or may not receive continuous SDK expansions or updates for future game versions. You are completely free to expand, modify the code, integrate it into your own client, or fully rebrand it under the terms of the MIT License.

---

## Features

- **Dual Rendering Architecture**:
  - **In-World 3D (Native Tessellator)**: Render directly into Minecraft's 3D render pipeline via `LevelRenderer::renderLevel` with zero jitter, zero thread race conditions, and automatic depth testing.
  - **Screen 2D (DirectX 12 + ImGui)**: Native D3D12 render hook powered by Kiero with fence synchronization, buffer resize handling, and ImGui integration.
- **Event-Driven Pipeline**: Type-safe, decoupled `EventBus` supporting publish/subscribe patterns and event cancellation.
- **EnTT Component SDK**: Direct access to Minecraft Bedrock ECS (`EntityContext`, entity registries, and standard game components).
- **Dynamic VTable & Signature Hooks**: Memory scanning via `libhat` with compile-time member function vtable target extraction and MinHook detours.
- **Modular Feature Architecture**: Category-based module system supporting toggle/hold keybinds, runtime lifecycle callbacks (`on_level_render()`, `on_imgui_render()`), and typed serialized settings.
- **JSON Profile Manager**: Automatic profile serialization and deserialization for module states and configuration values.
- **Modern C++20 Core**: Structured logging with `std::source_location` and `std::format`, RAII memory management, and clean CMake build configuration.

---

## Project Structure

```text
MCBE_DX12_ModBase/
├── CMakeLists.txt              # CMake build definitions and compiler flags
├── README.md                   # Project documentation
│
├── src/
│   ├── client.h / .cpp         # Client singleton lifecycle (init, hooks, shutdown)
│   │
│   ├── config/                 # Configuration & profile persistence
│   │   ├── config_manager.h    # JSON profile save/load/delete manager
│   │   └── setting.h           # Typed settings (Float, Int, Bool, Enum, Color)
│   │
│   ├── events/                 # EventBus & runtime event payloads
│   │   ├── event.h             # Base cancellable Event class
│   │   ├── event_bus.h         # Type-safe EventBus singleton
│   │   ├── actor_tick_event.h  # Dispatched on player/actor tick
│   │   ├── attack_event.h      # Dispatched on GameMode::attack
│   │   ├── client_instance_update_event.h
│   │   ├── level_render_event.h # Dispatched on LevelRenderer::renderLevel
│   │   └── key_event.h         # Dispatched on Win32 key presses
│   │
│   ├── input/                  # Win32 input processing & keybind tracking
│   │   ├── input_manager.h     # Input routing & keybind listening state
│   │   └── key_names.h         # Virtual key code to display name mapping
│   │
│   ├── minecraft/              # Bedrock memory, hooks, and SDK
│   │   ├── bedrock_memory.h    # Signatures, vtable indices, and struct offsets
│   │   ├── hooks/              # MinHook installations & memory helpers
│   │   │   ├── hook_manager.h  # Master hook lifecycle orchestrator
│   │   │   ├── hook_memory.h   # libhat pattern scanner & vtable helpers
│   │   │   ├── actor_tick_hook.h
│   │   │   ├── attack_hook.h
│   │   │   ├── client_instance_hook.h
│   │   │   └── level_render_hook.h  # LevelRenderer::renderLevel hook (publishes LevelRenderEvent)
│   │   └── sdk/                # Reverse-engineered game classes & EnTT ECS
│   │       ├── actor/          # Actor, LocalPlayer, GameMode
│   │       ├── client/         # ClientInstance, LevelRenderer, GuiData, GLMatrix
│   │       ├── components/     # StateVector, Rotation, AABB, etc.
│   │       ├── entity/         # EntityContext, EntityId, EnTT integration
│   │       └── render/         # ScreenContext, Tessellator, MaterialPtr, MeshHelpers, Draw3D
│   │
│   ├── modules/                # Base module system and feature modules
│   │   ├── module.h / .cpp     # Module base class (on_imgui_render, on_level_render)
│   │   ├── module_manager.h    # Module registry and category manager
│   │   ├── combat/             # Combat modules (e.g. Killaura)
│   │   ├── movement/           # Movement modules (e.g. Jetpack, Fly)
│   │   ├── player/             # Player utility modules
│   │   └── visual/             # Render & HUD modules (Arraylist, ESP, Trail)
│   │
│   ├── platform/windows/       # DLL entry point (DllMain) and uninject thread
│   │
│   ├── render/dx12/            # DirectX 12 hook & ImGui backend management
│   │   └── dx12_hook.h / .cpp  # SwapChain Present/ResizeBuffers hooks & fence sync
│   │
│   ├── ui/                     # ImGui UI design, theme styling, and widgets
│   │   ├── menu.h / .cpp       # Main client GUI overlay
│   │   ├── ui_settings.h       # UI scale and rendering settings
│   │   └── widgets/            # Custom switches, sliders, search bar, badges
│   │
│   └── utils/                  # Utilities, logging, and math
│       ├── color.h             # Color definitions and conversion utilities
│       ├── hash.h              # Compile-time 64-bit FNV-1a string hashing
│       ├── logger.h / .cpp     # Thread-safe format logger
│       └── math.h              # Vec2, Vec3, ViewAngles, AABB math types
│
└── external/
    ├── d3d12/                  # DirectX 12 helper headers
    ├── entt/                   # EnTT entity component system
    ├── imgui/                  # Dear ImGui library & backends
    ├── kiero/                  # D3D12 hooking library
    ├── libhat/                 # SIMD signature scanning library
    ├── minhook/                # x64 API hook library
    └── nlohmann/               # JSON for Modern C++
```

---

## Requirements

- **Operating System**: Windows 10 or Windows 11 (x64)
- **Compiler / IDE**: Visual Studio 2022 (MSVC v143+) with the *Desktop development with C++* workload
- **Build System**: CMake 3.20 or newer
- **C++ Standard**: C++20 (`/std:c++20`)

---

## Build & Usage

### 1. Build via Visual Studio
1. Open the project folder in Visual Studio 2022 (**File > Open > Folder**).
2. Set the configuration target to **x64-Release**.
3. Build the project (**Build > Build All** or `Ctrl+Shift+B`).
4. Output DLL will be generated in `build/Release/MCBE_DX12_ModBase.dll` or `out/build/x64-Release/MCBE_DX12_ModBase.dll`.

### 2. Build via CMake CLI
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### 3. Injection & Controls
Inject the compiled DLL into `Minecraft.Windows.exe` using any standard 64-bit injector.

| Hotkey | Action |
| :--- | :--- |
| `INSERT` | Toggle client ClickGUI menu |
| `END` | Cleanly unhook, release resources, and unload DLL |

### 4. Configuration & Profiles
Client profile data and log outputs are automatically stored in the `%APPDATA%` directory:
- **Configuration Profiles**: `%APPDATA%\MCBE_DX12_ModBase\configs\<profile>.json`
- **Client Logs**: `%APPDATA%\MCBE_DX12_ModBase\logs\latest.log`

---

## Developer Guide

### 1. Creating a New Module

Create a header and source file in the appropriate `src/modules/<category>/` folder:

```cpp
#pragma once
#include "modules/module.h"
#include "events/event_bus.h"
#include "events/actor_tick_event.h"
#include "minecraft/sdk/actor/local_player.h"

class Jetpack final : public Module {
public:
    Jetpack() : Module("Movement", "Jetpack", "Flies in the direction you look.") {
        m_speed = add_setting<config::FloatSetting>("Speed", "Flight velocity multiplier.", 1.5f, 0.1f, 10.0f);
    }

    void on_enable() override {
        m_tickListener = EventBus::instance().subscribe<ActorTickEvent>([this](ActorTickEvent& event) {
            if (!event.localPlayer) return;

            math::Vec3 direction = math::get_direction(event.localPlayer->getRotation());
            event.localPlayer->setVelocity(direction * m_speed->value());
        });
    }

    void on_disable() override {
        EventBus::instance().unsubscribe(m_tickListener);
    }

private:
    EventBus::ListenerId m_tickListener = 0;
    config::FloatSetting* m_speed = nullptr;
};
```

Register the module in `src/modules/module_manager.cpp`:

```cpp
m_modules.push_back(std::make_unique<Jetpack>());
```

CMake will automatically pick up any new `.cpp` and `.h` files under `src/`.

---

### 2. Dual Rendering Pipelines: In-World 3D vs 2D Overlay

The base provides two distinct rendering passes depending on what you want to draw:

#### Option A: In-World 3D (Tessellator via `on_level_render`)
Runs directly inside the engine's `LevelRenderer::renderLevel` pass. It is 100% thread-safe to iterate `Actor::get_all()` directly with zero entity caches, zero jitter, and no manual projection.

```cpp
#include "minecraft/sdk/render/draw_3d.h"

void ESP::on_level_render() {
    Draw3D dc;
    if (!dc.is_valid()) return;

    // Draw 3D bounding boxes directly in world space
    dc.draw_box(min, max, mce::Color{ 1.0f, 0.2f, 0.2f, 1.0f });
    dc.fill_box(min, max, mce::Color{ 1.0f, 0.2f, 0.2f, 0.2f });
    dc.flush();
}
```

#### Option B: 2D Screen Overlay (DirectX 12 + ImGui via `on_imgui_render`)
Runs on the DirectX 12 present hook. Ideal for HUD elements, 2D trails, ClickGUI, and overlays:

```cpp
#include <imgui.h>
#include "minecraft/sdk/client/client_instance.h"

void Trail::on_imgui_render() {
    auto* ci = ClientInstance::get();
    if (!ci) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    math::Vec2 screenA, screenB;
    if (ci->world_to_screen(worldPosA, screenA) && ci->world_to_screen(worldPosB, screenB)) {
        drawList->AddLine(ImVec2(screenA.x, screenA.y), ImVec2(screenB.x, screenB.y), IM_COL32(255, 255, 255, 255), 2.0f);
    }
}
```

---

### 3. Working with the EventBus

#### Subscribing to Events
```cpp
EventBus::ListenerId id = EventBus::instance().subscribe<ActorTickEvent>([](ActorTickEvent& event) {
    LocalPlayer* player = event.localPlayer;
});
```

#### Publishing Events
```cpp
AttackEvent event(gameMode, localPlayer, target);
EventBus::instance().publish(event);
if (event.is_cancelled()) {
    // Attack was blocked by a module
}
```

#### Unsubscribing
```cpp
EventBus::instance().unsubscribe(id);
```

---

### 4. Accessing Minecraft EnTT Components

All actors wrap an `EntityContext` providing direct access to Bedrock's EnTT registry:

```cpp
// Check component existence
if (actor->hasComponent<StateVectorComponent>()) {
    auto* state = actor->getComponent<StateVectorComponent>();
    math::Vec3 position = state->mPos;
    math::Vec3 velocity = state->mPosDelta;
}

// In on_level_render() or on_enable() (tick thread), iterate active world actors:
for (Actor* actor : Actor::get_all(localPlayer)) {
    if (actor->isPlayer()) {
        // Process player entity
    }
}
```

---

### 5. Hooking Functions & Memory Definitions

#### Defining Signatures
Signatures are declared inside `minecraft::BedrockSignatures` in `src/minecraft/bedrock_memory.h` using the `DEFINE_SIG` macro:

```cpp
DEFINE_SIG(clientInstance_Update, "ClientInstance::update",
    "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? "
    "48 C7 85 ? ? ? ? FE FF FF FF 89 D3 48 89 CE 48 8B 01 48 8B 80 ? ? ? ? "
    "48 8D 95 ? ? ? ? FF 15 ? ? ? ?");
```

- **`name`**: Member identifier (e.g. `BedrockSignatures::clientInstance_Update`).
- **`readableName`**: Human-readable name automatically logged during signature resolution (`[OK]` / `[FAILED]`).
- **`patternStr`**: IDA-style byte string pattern with `?` wildcards.
- **`hash`**: Automatically computes a 64-bit FNV-1a hash at compile time via `utils::hash::fnv1a_64`.

#### Resolving Signatures
Scan with `libhat` using the type-safe single-argument signature resolver:

```cpp
const std::uintptr_t addr = hook_memory::resolve_signature(BedrockSignatures::render_levelRender);
```

#### Defining Struct Offsets
Offsets are declared inside `minecraft::BedrockOffsets` using `DEFINE_OFFSET(OwnerClass, FieldType, name, offsetVal)`:

```cpp
DEFINE_OFFSET(ClientInstance, GuiData*, clientInstance_guiData, 0x650);
```

Access fields type-safely via `.get(instance)` or `operator()`:
```cpp
GuiData* guiData = BedrockOffsets::clientInstance_guiData.get(clientInstance);
```

#### Defining VTable Slots
VTable indices are declared in `minecraft::BedrockVtableSlots` using `DEFINE_VTABLE_SLOT(name, slotIndex)`:

```cpp
DEFINE_VTABLE_SLOT(gameMode_Attack, 15);
```

---

## Disclaimer

This project is provided strictly for educational and research purposes. Test and modify software only in authorized private development environments. You are solely responsible for adhering to applicable software licenses, game terms of service, and platform rules.

---

## License

This project is licensed under the [MIT License](LICENSE). You are permitted to use, modify, distribute, sublicense, and rebrand this software without restriction, provided that the original copyright and permission notices are retained.
