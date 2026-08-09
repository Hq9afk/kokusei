#pragma once

#include "text.hpp"
#include "texture.hpp"
#include <functional>
#include <list>
#include <string>
#include <unordered_map>

class TextureCache {
  public:
    const Texture *get(const std::string &key,
                       const std::function<RasterizedText()> &rasterize) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            order_.splice(order_.begin(), order_, it->second.order_it);
            return &it->second.tex;
        }
        RasterizedText raster = rasterize();
        if (raster.width <= 0 || raster.height <= 0)
            return nullptr;
        order_.push_front(key);
        Entry &entry = index_[key];
        entry.tex = make_texture_from_raster(raster);
        entry.order_it = order_.begin();
        evict_if_needed();
        return &entry.tex;
    }

    void clear() {
        index_.clear();
        order_.clear();
    }

  private:
    struct Entry {
        Texture tex;
        std::list<std::string>::iterator order_it;
    };

    void evict_if_needed() {
        while (index_.size() > kMaxEntries) {
            index_.erase(order_.back());
            order_.pop_back();
        }
    }

    static constexpr std::size_t kMaxEntries = 512;
    std::unordered_map<std::string, Entry> index_;
    std::list<std::string> order_;
};

