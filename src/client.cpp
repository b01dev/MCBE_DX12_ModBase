#include "client.h"

#include "config/config_manager.h"
#include "events/client_instance_update_event.h"
#include "events/event_bus.h"
#include "minecraft/hooks/hook_manager.h"
#include "modules/module_manager.h"
#include "render/dx12/dx12_hook.h"
#include <utils/logger.h>

Client& Client::instance() {
    static Client client;
    return client;
}

void Client::initialize(HMODULE module) {// تفعيل الهوك على عنوان وضع الكتل
uintptr_t baseAddr = (uintptr_t)dlopen("libminecraft.so", RTLD_LAZY);
if (baseAddr) {
    void* target = (void*)(baseAddr + 0x0fb99330);
    // استدعاء الهوك
}

    m_module = module;

    // Initialize Logger
    Logger::instance().initialize("MCBE Client", true, true);

    LOG_INFO("Initializing MCBE DX12 ModBase...");

    // Initialize module manager
    ModuleManager::instance().initialize();

    // Install hooks & event listeners
    if (!minecraft::hooks::initialize()) {
        LOG_ERROR("Failed to initialize game hooks!");
        shutdown();
        FreeLibraryAndExitThread(m_module, 0);
        return;
    }

    // Start DX12/ImGui hook
    if (!dx12_hook::start(m_module)) {
        LOG_ERROR("Failed to start DX12 overlay hooks!");
        shutdown();
        FreeLibraryAndExitThread(m_module, 0);
        return;
    }

    // Safely defer config loading until ClientInstance is ready
    EventBus::instance().subscribe<ClientInstanceUpdateEvent>([this](ClientInstanceUpdateEvent&) {
        if (!m_configLoaded) {
            m_configLoaded = true;
            LOG_INFO("ClientInstance initialized. Loading default configuration profile...");
            ConfigManager::instance().load_config("default");
        }
        });

    LOG_INFO("MCBE Client initialized successfully!");
}

void Client::shutdown() {
    LOG_INFO("Shutting down MCBE Client...");
    dx12_hook::stop();
    minecraft::hooks::shutdown();
    ModuleManager::instance().shutdown();
    Logger::instance().shutdown();
    LOG_INFO("MCBE Client shutdown complete.");
}// 1. تعريف شكل الإحداثيات
struct BlockPos {
    int x;
    int y;
    int z;
};

// 2. تعريف الدالة الأصلية للمباني
typedef bool (*BlockSource_setBlock_fn)(void* _this, const BlockPos& pos, const void* block, int updateFlags, signed char actorFilter);
BlockSource_setBlock_fn original_setBlock = nullptr;

// 3. دالة السماح بالبناء فوق سقف النيذر
bool hooked_BlockSource_setBlock(void* _this, const BlockPos& pos, const void* block, int updateFlags, signed char actorFilter) {
    if (pos.y >= 128) {
        return original_setBlock(_this, pos, block, updateFlags, actorFilter);
    }
    return original_setBlock(_this, pos, block, updateFlags, actorFilter);
}
