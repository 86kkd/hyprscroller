#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "core/layout_snapshot.h"
#include "core/types.h"

namespace ScrollerGrid {

struct GridProfile {
    Mode   mode = Mode::Row;
    int    visibleColumns = 2;
    int    visibleRows = 1;
    double unitWidth = 0.0;
    double unitHeight = 0.0;
};

struct GridViewport {
    int originColumn = 0;
    int originRow = 0;
};

struct GridItem {
    uintptr_t key = 0;
    int       column = 0;
    int       row = 0;
    int       columnSpan = 1;
    int       rowSpan = 1;
};

struct RenderedGridItem {
    uintptr_t          key = 0;
    ScrollerCore::Box  logicalBox;
    ScrollerCore::Box  committedBox;
    bool               visible = false;
};

enum class GridMoveResult {
    Moved,
    NoOp,
};

GridProfile profile_for_workarea(Mode mode, const ScrollerCore::Box& workarea);
GridProfile profile_for_workarea_extent(const ScrollerCore::Box& workarea);

ScrollerCore::Box grid_item_logical_box(const GridItem& item,
                                        const GridViewport& viewport,
                                        const GridProfile& profile,
                                        const ScrollerCore::Box& workarea);

RenderedGridItem render_grid_item(const GridItem& item,
                                  const GridViewport& viewport,
                                  const GridProfile& profile,
                                  const ScrollerCore::Box& fullBox,
                                  const ScrollerCore::Box& workareaBox);

class GridModel {
public:
    bool empty() const;
    size_t size() const;

    void clear();
    bool add_window(uintptr_t key, const GridProfile& profile);
    bool remove_window(uintptr_t key);
    bool focus_window(uintptr_t key);
    bool contains(uintptr_t key) const;

    const GridItem* active_item() const;
    const GridItem* item_for_key(uintptr_t key) const;
    std::optional<size_t> active_index() const;

    GridMoveResult move_focus(Direction direction,
                              const GridProfile& profile,
                              GridViewport& viewport,
                              bool focusWrap);
    void ensure_active_visible(const GridProfile& profile, GridViewport& viewport) const;
    bool swap_windows(uintptr_t a, uintptr_t b);
    GridMoveResult move_active_window(Direction direction,
                                      const GridProfile& profile,
                                      GridViewport& viewport);
    ScrollerSnapshot::GridSnapshot capture_snapshot(const GridViewport& viewport) const;
    void restore_snapshot(const ScrollerSnapshot::GridSnapshot& snapshot);

    std::vector<RenderedGridItem> render(const GridViewport& viewport,
                                         const GridProfile& profile,
                                         const ScrollerCore::Box& fullBox,
                                         const ScrollerCore::Box& workareaBox) const;

private:
    std::optional<size_t> index_for_key(uintptr_t key) const;
    std::optional<size_t> first_occupied_index(int column, int row, int columnSpan, int rowSpan, std::optional<size_t> ignoredIndex = std::nullopt) const;
    bool cell_range_occupied(int column, int row, int columnSpan, int rowSpan) const;
    bool shift_viewport(Direction direction, GridViewport& viewport) const;

    std::vector<GridItem> items;
    std::optional<size_t> activeIndex;
};

} // namespace ScrollerGrid
