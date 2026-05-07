#include "workarea_pager.h"

#include <algorithm>

namespace ScrollerCore {

bool box_intersects(const Box& a, const Box& b) {
    return a.x + a.w > b.x &&
           a.x < b.x + b.w &&
           a.y + a.h > b.y &&
           a.y < b.y + b.h;
}

Box avoid_reserved_edges_for_hidden_box(const Box& logicalBox,
                                        const Box& fullBox,
                                        const Box& workareaBox) {
    auto committed = logicalBox;

    const auto reservedLeft = std::max(0.0, workareaBox.x - fullBox.x);
    const auto reservedTop = std::max(0.0, workareaBox.y - fullBox.y);
    const auto reservedRight = std::max(0.0, (fullBox.x + fullBox.w) - (workareaBox.x + workareaBox.w));
    const auto reservedBottom = std::max(0.0, (fullBox.y + fullBox.h) - (workareaBox.y + workareaBox.h));

    if (reservedLeft > 0.0 && committed.x + committed.w <= workareaBox.x)
        committed.x -= reservedLeft;
    else if (reservedRight > 0.0 && committed.x >= workareaBox.x + workareaBox.w)
        committed.x += reservedRight;

    if (reservedTop > 0.0 && committed.y + committed.h <= workareaBox.y)
        committed.y -= reservedTop;
    else if (reservedBottom > 0.0 && committed.y >= workareaBox.y + workareaBox.h)
        committed.y += reservedBottom;

    return committed;
}

WorkareaPageBox project_box_to_workarea_page(const Box& logicalBox,
                                             const Box& fullBox,
                                             const Box& workareaBox) {
    return {
        .logical = logicalBox,
        .committed = avoid_reserved_edges_for_hidden_box(logicalBox, fullBox, workareaBox),
        .visible = box_intersects(logicalBox, workareaBox),
    };
}

} // namespace ScrollerCore
