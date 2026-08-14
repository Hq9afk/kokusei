#include "render/texture_cache.h"

const Texture *
TextureCache::get(const std::string &key,
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

void TextureCache::evict_if_needed() {
    while (index_.size() > kMaxEntries) {
        index_.erase(order_.back());
        order_.pop_back();
    }
}
