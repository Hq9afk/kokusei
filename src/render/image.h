#pragma once

#include "render/texture.h"

#include <string>

unsigned char *load_image_decode(const std::string &path, int &width,
                                 int &height);

Texture load_image_texture(const std::string &path);
