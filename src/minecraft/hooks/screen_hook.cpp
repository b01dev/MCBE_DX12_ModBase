#include "minecraft/hooks/screen_hook.h"
#include "minecraft/sdk/client/client_instance.h"
#include "input/input_manager.h"
#include "events/event_bus.h"
#include "events/screen_change_event.h"
#include "utils/logger.h"

#include <mutex>
#include <new>

namespace {
    using GetScreenNameFn = std::string&(__fastcall*)(ClientInstance* self, std::string& out);
    void* g_screenTarget = nullptr;

    std::string s_currentScreenName;
    std::string s_overrideScreenName;
    std::mutex s_screenMutex;

    static bool call_get_screen_name_raw(ClientInstance* self, GetScreenNameFn fn, std::string* out) {
        __try {
            fn(self, *out);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            new (static_cast<void*>(out)) std::string();
            return false;
        }
    }
}

namespace minecraft::hooks {
    bool install_screen_hook(std::uintptr_t screenNameAddr) {
        if (!screenNameAddr) {
            LOG_ERROR("Invalid ClientInstance::getScreenName address provided!");
            return false;
        }

        LOG_INFO("Configured ClientInstance::getScreenName resolution at address: {:#x}", screenNameAddr);
        g_screenTarget = reinterpret_cast<void*>(screenNameAddr);
        return true;
    }

    void remove_screen_hook() {
        {
            std::lock_guard<std::mutex> lock(s_screenMutex);
            s_currentScreenName.clear();
            s_overrideScreenName.clear();
        }

        g_screenTarget = nullptr;
        LOG_INFO("ClientInstance getScreenName handler removed.");
    }

    void update_screen_name(ClientInstance* self) {
        if (!self || !g_screenTarget) return;

        GetScreenNameFn fn = reinterpret_cast<GetScreenNameFn>(g_screenTarget);
        if (!fn) return;

        std::string temp;
        if (!call_get_screen_name_raw(self, fn, &temp)) {
            return;
        }

        std::string prevScreen;
        bool changed = false;

        {
            std::lock_guard<std::mutex> lock(s_screenMutex);
            if (!temp.empty() && temp != s_currentScreenName) {
                prevScreen = s_currentScreenName;
                s_currentScreenName = temp;
                changed = true;
            }
        }

        if (changed) {
            ScreenChangeEvent event(self, prevScreen, temp);
            EventBus::instance().publish(event);

            InputManager::instance().clear_key_states();
        }
    }

    std::string get_current_screen_name() {
        std::lock_guard<std::mutex> lock(s_screenMutex);
        return s_currentScreenName;
    }

    bool is_game_ui_open() {
        // If there is no local player, we are in menus
        if (!ClientInstance::getLocalPlayer()) {
            return true;
        }

        const std::string screen = get_current_screen_name();
        if (screen.empty()) {
            return false;
        }
        return screen != "hud_screen";
    }

    void set_screen_override(const std::string& screenName) {
        std::lock_guard<std::mutex> lock(s_screenMutex);
        s_overrideScreenName = screenName;
    }

    void clear_screen_override() {
        std::lock_guard<std::mutex> lock(s_screenMutex);
        s_overrideScreenName.clear();
    }
}
