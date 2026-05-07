#pragma once

#include "types.h"

namespace ScrollerCore {

struct WorkareaPageBox {
    Box  logical;
    Box  committed;
    bool visible = false;
};

bool box_intersects(const Box& a, const Box& b);

// Keep a hidden box in the offscreen page that corresponds to the monitor's
// full bounds instead of clipping it into reserved bars such as Waybar.
Box avoid_reserved_edges_for_hidden_box(const Box& logicalBox,
                                        const Box& fullBox,
                                        const Box& workareaBox);

WorkareaPageBox project_box_to_workarea_page(const Box& logicalBox,
                                             const Box& fullBox,
                                             const Box& workareaBox);

} // namespace ScrollerCore
