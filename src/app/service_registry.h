#pragma once

#include <memory>
#include <vector>

#include "app/service.h"

std::vector<std::unique_ptr<Service>> build_services();
