#pragma once

#include "render/texture.h"

#include <cstdint>
#include <vector>

class MatrixGrid {
  public:
    void rebuild(int width, int height);
    void tick();

    const Texture &texture() const { return texture_; }
    bool ready() const { return texture_.id != 0; }

  private:
    struct Column {
        float drop = 0.0f;
        bool ever_reset = false;
        float last_head_drop = 0.0f;
        char32_t last_glyph = U' ';
        bool last_head_valid = false;
    };

    void decay();
    float start_drop() const { return -2.0f; }
    char32_t random_glyph() const;

    int width_ = 0;
    int height_ = 0;
    int column_count_ = 0;
    int row_count_ = 0;
    float offset_x_ = 0.0f;
    float offset_y_ = 0.0f;

    std::vector<uint8_t> buffer_;
    int stride_ = 0;
    std::vector<Column> columns_;
    Texture texture_;
};
