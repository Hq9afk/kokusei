#include "bar/widget/clock_widget.h"

#include "render/text.h"

#include <ctime>

namespace bar_detail {

void update_clock(Texture &clock_texture) {
    char buf[32];
    time_t now = time(nullptr);
    strftime(buf, sizeof(buf), "%a %Y-%m-%d %H:%M:%S", localtime(&now));
    RasterizedText r = rasterize_text(buf);
    if (r.width > 0) {
        clock_texture = make_texture_rgba(r.width, r.height, r.rgba.data());
    }
}

void draw_clock_pill(Node *root, float height, int32_t surface_width,
                     const Texture &clock_texture, const float tint[4],
                     const float pill_bg[4]) {
    if (!clock_texture.id)
        return;
    float clock_pill_w = clock_texture.width + kPillPad * 2;
    float clock_x = (surface_width - clock_pill_w) / 2.0f;
    draw_static_pill_row(root, clock_x, height, {&clock_texture}, tint,
                         pill_bg);
}

} // namespace bar_detail
