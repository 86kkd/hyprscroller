#include "layout/grid/grid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ScrollerGrid {
namespace {

double safe_half(double value) {
    return std::max(1.0, value * 0.5);
}

bool ranges_intersect(int a0, int a1, int b0, int b1) {
    return a0 < b1 && b0 < a1;
}

bool boxes_intersect(const ScrollerCore::Box& a, const ScrollerCore::Box& b) {
    return a.x + a.w > b.x &&
           a.x < b.x + b.w &&
           a.y + a.h > b.y &&
           a.y < b.y + b.h;
}

double center_x(const GridItem& item) {
    return static_cast<double>(item.column) + static_cast<double>(item.columnSpan) * 0.5;
}

double center_y(const GridItem& item) {
    return static_cast<double>(item.row) + static_cast<double>(item.rowSpan) * 0.5;
}

bool candidate_in_direction(const GridItem& active, const GridItem& candidate, Direction direction) {
    switch (direction) {
    case Direction::Left:
        return center_x(candidate) < center_x(active);
    case Direction::Right:
        return center_x(candidate) > center_x(active);
    case Direction::Up:
        return center_y(candidate) < center_y(active);
    case Direction::Down:
        return center_y(candidate) > center_y(active);
    default:
        return false;
    }
}

double directional_score(const GridItem& active, const GridItem& candidate, Direction direction) {
    const auto dx = center_x(candidate) - center_x(active);
    const auto dy = center_y(candidate) - center_y(active);
    const auto primary = direction == Direction::Left || direction == Direction::Right ? std::abs(dx) : std::abs(dy);
    const auto cross = direction == Direction::Left || direction == Direction::Right ? std::abs(dy) : std::abs(dx);
    return primary * 1000.0 + cross;
}

void advance_position_for_mode(Mode mode, int& column, int& row) {
    if (mode == Mode::Column)
        ++row;
    else
        ++column;
}

} // namespace

GridProfile profile_for_workarea(Mode mode, const ScrollerCore::Box& workarea) {
    if (mode == Mode::Column) {
        return {
            .mode = mode,
            .visibleColumns = 1,
            .visibleRows = 2,
            .unitWidth = std::max(1.0, workarea.w),
            .unitHeight = safe_half(workarea.h),
        };
    }

    return {
        .mode = mode,
        .visibleColumns = 2,
        .visibleRows = 1,
        .unitWidth = safe_half(workarea.w),
        .unitHeight = std::max(1.0, workarea.h),
    };
}

GridProfile profile_for_workarea_extent(const ScrollerCore::Box& workarea) {
    return profile_for_workarea(workarea.h > workarea.w ? Mode::Column : Mode::Row, workarea);
}

ScrollerCore::Box grid_item_logical_box(const GridItem& item,
                                        const GridViewport& viewport,
                                        const GridProfile& profile,
                                        const ScrollerCore::Box& workarea) {
    return {
        workarea.x + static_cast<double>(item.column - viewport.originColumn) * profile.unitWidth,
        workarea.y + static_cast<double>(item.row - viewport.originRow) * profile.unitHeight,
        std::max(1.0, static_cast<double>(item.columnSpan) * profile.unitWidth),
        std::max(1.0, static_cast<double>(item.rowSpan) * profile.unitHeight),
    };
}

ScrollerCore::Box avoid_reserved_edges_for_hidden_box(const ScrollerCore::Box& logicalBox,
                                                      const ScrollerCore::Box& fullBox,
                                                      const ScrollerCore::Box& workareaBox) {
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

RenderedGridItem render_grid_item(const GridItem& item,
                                  const GridViewport& viewport,
                                  const GridProfile& profile,
                                  const ScrollerCore::Box& fullBox,
                                  const ScrollerCore::Box& workareaBox) {
    const auto logical = grid_item_logical_box(item, viewport, profile, workareaBox);
    return {
        .key = item.key,
        .logicalBox = logical,
        .committedBox = avoid_reserved_edges_for_hidden_box(logical, fullBox, workareaBox),
        .visible = boxes_intersect(logical, workareaBox),
    };
}

bool GridModel::empty() const {
    return items.empty();
}

size_t GridModel::size() const {
    return items.size();
}

void GridModel::clear() {
    items.clear();
    activeIndex.reset();
}

std::optional<size_t> GridModel::index_for_key(uintptr_t key) const {
    for (size_t index = 0; index < items.size(); ++index) {
        if (items[index].key == key)
            return index;
    }
    return std::nullopt;
}

bool GridModel::contains(uintptr_t key) const {
    return index_for_key(key).has_value();
}

const GridItem* GridModel::item_for_key(uintptr_t key) const {
    const auto index = index_for_key(key);
    return index ? &items[*index] : nullptr;
}

std::optional<size_t> GridModel::active_index() const {
    return activeIndex;
}

const GridItem* GridModel::active_item() const {
    return activeIndex && *activeIndex < items.size() ? &items[*activeIndex] : nullptr;
}

bool GridModel::cell_range_occupied(int column, int row, int columnSpan, int rowSpan) const {
    for (const auto& item : items) {
        if (ranges_intersect(column, column + columnSpan, item.column, item.column + item.columnSpan) &&
            ranges_intersect(row, row + rowSpan, item.row, item.row + item.rowSpan))
            return true;
    }
    return false;
}

bool GridModel::add_window(uintptr_t key, const GridProfile& profile) {
    if (key == 0 || contains(key))
        return false;

    GridItem item{.key = key};
    if (const auto* active = active_item()) {
        item.column = active->column;
        item.row = active->row;
        advance_position_for_mode(profile.mode, item.column, item.row);
        while (cell_range_occupied(item.column, item.row, item.columnSpan, item.rowSpan))
            advance_position_for_mode(profile.mode, item.column, item.row);
    }

    items.push_back(item);
    activeIndex = items.size() - 1;
    return true;
}

bool GridModel::remove_window(uintptr_t key) {
    const auto index = index_for_key(key);
    if (!index)
        return false;

    items.erase(items.begin() + static_cast<std::ptrdiff_t>(*index));
    if (items.empty()) {
        activeIndex.reset();
        return true;
    }

    if (!activeIndex || *activeIndex == *index) {
        activeIndex = std::min(*index, items.size() - 1);
    } else if (*activeIndex > *index) {
        activeIndex = *activeIndex - 1;
    }
    return true;
}

bool GridModel::focus_window(uintptr_t key) {
    const auto index = index_for_key(key);
    if (!index)
        return false;

    activeIndex = *index;
    return true;
}

bool GridModel::swap_windows(uintptr_t a, uintptr_t b) {
    const auto indexA = index_for_key(a);
    const auto indexB = index_for_key(b);
    if (!indexA || !indexB || *indexA == *indexB)
        return false;

    std::swap(items[*indexA].column, items[*indexB].column);
    std::swap(items[*indexA].row, items[*indexB].row);
    std::swap(items[*indexA].columnSpan, items[*indexB].columnSpan);
    std::swap(items[*indexA].rowSpan, items[*indexB].rowSpan);
    return true;
}

void GridModel::ensure_active_visible(const GridProfile& profile, GridViewport& viewport) const {
    const auto* active = active_item();
    if (!active)
        return;

    if (active->column < viewport.originColumn)
        viewport.originColumn = active->column;
    else if (active->column + active->columnSpan > viewport.originColumn + profile.visibleColumns)
        viewport.originColumn = active->column + active->columnSpan - profile.visibleColumns;

    if (active->row < viewport.originRow)
        viewport.originRow = active->row;
    else if (active->row + active->rowSpan > viewport.originRow + profile.visibleRows)
        viewport.originRow = active->row + active->rowSpan - profile.visibleRows;
}

bool GridModel::shift_viewport(Direction direction, GridViewport& viewport) const {
    switch (direction) {
    case Direction::Left:
        --viewport.originColumn;
        return true;
    case Direction::Right:
        ++viewport.originColumn;
        return true;
    case Direction::Up:
        --viewport.originRow;
        return true;
    case Direction::Down:
        ++viewport.originRow;
        return true;
    default:
        return false;
    }
}

GridMoveResult GridModel::move_focus(Direction direction,
                                     const GridProfile& profile,
                                     GridViewport& viewport,
                                     bool focusWrap) {
    if (!activeIndex || items.empty())
        return GridMoveResult::NoOp;

    if (direction == Direction::Begin) {
        activeIndex = 0;
        ensure_active_visible(profile, viewport);
        return GridMoveResult::Moved;
    }

    if (direction == Direction::End) {
        activeIndex = items.size() - 1;
        ensure_active_visible(profile, viewport);
        return GridMoveResult::Moved;
    }

    const auto& active = items[*activeIndex];
    std::optional<size_t> bestIndex;
    double bestScore = std::numeric_limits<double>::max();
    for (size_t index = 0; index < items.size(); ++index) {
        if (index == *activeIndex || !candidate_in_direction(active, items[index], direction))
            continue;

        const auto score = directional_score(active, items[index], direction);
        if (score < bestScore) {
            bestScore = score;
            bestIndex = index;
        }
    }

    if (bestIndex) {
        activeIndex = *bestIndex;
        ensure_active_visible(profile, viewport);
        return GridMoveResult::Moved;
    }

    if (focusWrap && items.size() > 1) {
        activeIndex = direction == Direction::Left || direction == Direction::Up ? items.size() - 1 : 0;
        ensure_active_visible(profile, viewport);
        return GridMoveResult::Moved;
    }

    return shift_viewport(direction, viewport) ? GridMoveResult::Moved : GridMoveResult::NoOp;
}

std::vector<RenderedGridItem> GridModel::render(const GridViewport& viewport,
                                                const GridProfile& profile,
                                                const ScrollerCore::Box& fullBox,
                                                const ScrollerCore::Box& workareaBox) const {
    std::vector<RenderedGridItem> rendered;
    rendered.reserve(items.size());
    for (const auto& item : items)
        rendered.push_back(render_grid_item(item, viewport, profile, fullBox, workareaBox));
    return rendered;
}

} // namespace ScrollerGrid
