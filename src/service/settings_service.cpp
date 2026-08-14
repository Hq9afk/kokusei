#include "service/settings_service.h"

#include "core/log.h"
#include "settings/settings.h"

#include <algorithm>

void settings_service_apply_field_text(Config &cfg, SettingsFieldId id,
                                       const std::string &text) {
    try {
        switch (id) {
        case SettingsFieldId::WallpaperPath:
            cfg.wallpaper_path = text;
            break;
        case SettingsFieldId::WallpaperDir:
            cfg.wallpaper_dir = text;
            break;
        case SettingsFieldId::IdleTimeout:
            cfg.idle_timeout_seconds =
                static_cast<uint32_t>(std::max(0, std::stoi(text)));
            break;
        case SettingsFieldId::IdleCommand:
            cfg.idle_command = text;
            break;
        case SettingsFieldId::IdleResumeCommand:
            cfg.idle_resume_command = text;
            break;
        default:
            break;
        }
    } catch (const std::exception &) {
        klog("settings: could not parse '%s' for field %d, keeping previous "
             "value",
             text.c_str(), static_cast<int>(id));
    }
}

void settings_service_save(const Config &cfg) { save_config(cfg); }
