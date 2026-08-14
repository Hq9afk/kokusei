#pragma once

#include "app/config.h"

#include <string>

enum class SettingsFieldId;

void settings_service_apply_field_text(Config &cfg, SettingsFieldId id,
                                       const std::string &text);

void settings_service_save(const Config &cfg);
