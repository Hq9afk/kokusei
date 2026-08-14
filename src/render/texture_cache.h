#pragma once

#include "render/text.h"
#include "render/texture.h"
#include <functional>
#include <list>
#include <string>
#include <unordered_map>

class TextureCache {
  public:
    const Texture *get(const std::string &key,
                       const std::function<RasterizedText()> &rasterize);

    void clear() {
        index_.clear();
        order_.clear();
    }

  private:
    struct Entry {
        Texture tex;
        std::list<std::string>::iterator order_it;
    };

    void evict_if_needed();

    static constexpr std::size_t kMaxEntries = 512;
    std::unordered_map<std::string, Entry> index_;
    std::list<std::string> order_;
};
