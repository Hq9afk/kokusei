#pragma once

#include "config/launcher_config.h"

#include <string>

std::string visit_store_app_key(const std::string &desktop_id);

std::string visit_store_file_key(const std::string &file_path);

std::string visit_store_default_path();

VisitStore visit_store_load(const std::string &path = {});

int visit_store_get(const VisitStore &vs, const std::string &key);

void visit_store_record(VisitStore &vs, const std::string &key);
