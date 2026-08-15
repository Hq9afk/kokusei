#pragma once

#include "app/config.h"
#include "config/settings_config.h"

#include <string>

void settings_service_apply_field_text(Config &cfg, SettingsFieldId id,
                                       const std::string &text);

void settings_service_save(const Config &cfg);
