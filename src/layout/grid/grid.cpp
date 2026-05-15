#include "layout/grid/grid.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/workarea_pager.h"

namespace ScrollerGrid {
namespace {

double safe_half(double value) {
    return std::max(1.0, value * 0.5);
}

bool ranges_intersect(int a0, int a1, int b0, int b1) {
    return a0 < b1 && b0 < a1;
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

int span_from_extent(double extent, double unit, int fallback, int maxSpan) {
    if (unit <= 0.0)
        return std::clamp(fallback, 1, std::max(1, maxSpan));

    return std::clamp(static_cast<int>(std::round(extent / unit)), 1, std::max(1, maxSpan));
}

int span_from_legacy_stack(const ScrollerSnapshot::StackSnapshot& stack, const GridProfile& profile) {
    if (profile.mode == Mode::Column)
        return span_from_extent(stack.geom.h, profile.unitHeight, 1, profile.visibleRows);

    return span_from_extent(stack.geom.w, profile.unitWidth, 1, profile.visibleColumns);
}

int full_page_columns(const GridProfile& profile) {
    return std::max(1, profile.visibleColumns);
}

int full_page_rows(const GridProfile& profile) {
    return std::max(1, profile.visibleRows);
}

bool spans_full_visible_page(const GridItem& item, const GridProfile& profile) {
    return item.columnSpan == full_page_columns(profile) &&
           item.rowSpan == full_page_rows(profile);
}

ScrollerCore::Box apply_viewport_gaps(const ScrollerCore::Box& box,
                                      const GridItem& item,
                                      const GridViewport& viewport,
                                      const GridProfile& profile,
                                      double gap) {
    if (gap <= 0.0)
        return box;

    const auto viewportColumnEnd = viewport.originColumn + std::max(1, profile.visibleColumns);
    const auto viewportRowEnd = viewport.originRow + std::max(1, profile.visibleRows);
    const auto itemColumnEnd = item.column + std::max(1, item.columnSpan);
    const auto itemRowEnd = item.row + std::max(1, item.rowSpan);

    const auto leftGap = item.column > viewport.originColumn ? gap : 0.0;
    const auto rightGap = itemColumnEnd < viewportColumnEnd ? gap : 0.0;
    const auto topGap = item.row > viewport.originRow ? gap : 0.0;
    const auto bottomGap = itemRowEnd < viewportRowEnd ? gap : 0.0;

    return {
        box.x + leftGap,
        box.y + topGap,
        std::max(1.0, box.w - leftGap - rightGap),
        std::max(1.0, box.h - topGap - bottomGap),
    };
}

void clamp_viewport_axis_to_occupied_range(int minStart, int maxEnd, int visibleCells, int& origin) {
    visibleCells = std::max(1, visibleCells);
    const auto maxOrigin = std::max(minStart, maxEnd - visibleCells);
    origin = std::clamp(origin, minStart, maxOrigin);
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

ScrollerCore::Box apply_window_border_inset(const ScrollerCore::Box& box, double border) {
    if (border <= 0.0)
        return box;

    return {
        box.x + border,
        box.y + border,
        std::max(1.0, box.w - 2.0 * border),
        std::max(1.0, box.h - 2.0 * border),
    };
}

RenderedGridItem render_grid_item(const GridItem& item,
                                  const GridViewport& viewport,
                                  const GridProfile& profile,
                                  const ScrollerCore::Box& fullBox,
                                  const ScrollerCore::Box& workareaBox,
                                  double gap) {
    const auto logical = apply_viewport_gaps(grid_item_logical_box(item, viewport, profile, workareaBox),
                                             item,
                                             viewport,
                                             profile,
                                             gap);
    const auto pageBox = ScrollerCore::project_box_to_workarea_page(logical, fullBox, workareaBox);
    return {
        .key = item.key,
        .logicalBox = pageBox.logical,
        .committedBox = pageBox.committed,
        .visible = pageBox.visible,
    };
}

ScrollerSnapshot::GridSnapshot migrate_legacy_snapshot_to_grid(const ScrollerSnapshot::CanvasSnapshot& snapshot,
                                                               const GridProfile& profile) {
    ScrollerSnapshot::GridSnapshot grid;
    grid.enabled = true;
    grid.mode = static_cast<int>(profile.mode);

    const auto laneStep = profile.mode == Mode::Column ? std::max(1, profile.visibleColumns)
                                                       : std::max(1, profile.visibleRows);
    for (size_t laneIndex = 0; laneIndex < snapshot.lanes.size(); ++laneIndex) {
        const auto& lane = snapshot.lanes[laneIndex];
        int localPosition = 0;
        for (size_t stackIndex = 0; stackIndex < lane.stacks.size(); ++stackIndex) {
            const auto& stack = lane.stacks[stackIndex];
            const auto span = stack.maximized
                ? (profile.mode == Mode::Column ? std::max(1, profile.visibleRows) : std::max(1, profile.visibleColumns))
                : span_from_legacy_stack(stack, profile);
            for (const auto& window : stack.windows) {
                if (window.key == 0)
                    continue;

                ScrollerSnapshot::GridItemSnapshot item;
                item.key = window.key;
                if (profile.mode == Mode::Column) {
                    item.column = static_cast<int>(laneIndex) * laneStep;
                    item.row = localPosition;
                    item.columnSpan = 1;
                    item.rowSpan = span;
                    localPosition += item.rowSpan;
                } else {
                    item.column = localPosition;
                    item.row = static_cast<int>(laneIndex) * laneStep;
                    item.columnSpan = span;
                    item.rowSpan = 1;
                    localPosition += item.columnSpan;
                }

                if (laneIndex == snapshot.activeLaneIndex &&
                    stackIndex == lane.activeStackIndex &&
                    (stack.activeWindowKey == 0 || stack.activeWindowKey == window.key)) {
                    grid.activeItemIndex = static_cast<int>(grid.items.size());
                }
                if (stack.fullscreened && grid.fullscreenKey == 0 &&
                    (stack.activeWindowKey == 0 || stack.activeWindowKey == window.key)) {
                    grid.fullscreenKey = window.key;
                }
                grid.items.push_back(item);
            }
        }
    }

    if (grid.activeItemIndex < 0 && !grid.items.empty())
        grid.activeItemIndex = 0;

    if (grid.activeItemIndex >= 0 && static_cast<size_t>(grid.activeItemIndex) < grid.items.size()) {
        const auto& active = grid.items[static_cast<size_t>(grid.activeItemIndex)];
        if (profile.mode == Mode::Column) {
            grid.viewportColumn = active.column;
            grid.viewportRow = active.row + active.rowSpan > profile.visibleRows
                ? active.row + active.rowSpan - profile.visibleRows
                : active.row;
        } else {
            grid.viewportColumn = active.column + active.columnSpan > profile.visibleColumns
                ? active.column + active.columnSpan - profile.visibleColumns
                : active.column;
            grid.viewportRow = active.row;
        }
    }

    return grid;
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

std::optional<size_t> GridModel::focus_candidate_index(Direction direction) const {
    if (!activeIndex || *activeIndex >= items.size())
        return std::nullopt;

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

    return bestIndex;
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

bool GridModel::has_focus_candidate(Direction direction) const {
    return focus_candidate_index(direction).has_value();
}

bool GridModel::active_item_at_edge(Direction direction) const {
    if (direction == Direction::Begin || direction == Direction::End || direction == Direction::Center)
        return false;

    return active_item() != nullptr && !has_focus_candidate(direction);
}

std::optional<size_t> GridModel::first_occupied_index(int column, int row, int columnSpan, int rowSpan, std::optional<size_t> ignoredIndex) const {
    for (size_t index = 0; index < items.size(); ++index) {
        if (ignoredIndex && *ignoredIndex == index)
            continue;

        const auto& item = items[index];
        if (ranges_intersect(column, column + columnSpan, item.column, item.column + item.columnSpan) &&
            ranges_intersect(row, row + rowSpan, item.row, item.row + item.rowSpan))
            return index;
    }
    return std::nullopt;
}

bool GridModel::cell_range_occupied(int column, int row, int columnSpan, int rowSpan) const {
    return first_occupied_index(column, row, columnSpan, rowSpan).has_value();
}

bool GridModel::add_window(uintptr_t key, const GridProfile& profile) {
    if (key == 0 || contains(key))
        return false;

    GridItem item{.key = key};
    if (items.empty()) {
        item.columnSpan = full_page_columns(profile);
        item.rowSpan = full_page_rows(profile);
    } else if (activeIndex && *activeIndex < items.size()) {
        auto& active = items[*activeIndex];
        if (items.size() == 1 && spans_full_visible_page(active, profile)) {
            active.columnSpan = 1;
            active.rowSpan = 1;
        }

        item.column = active.column;
        item.row = active.row;
        advance_position_for_mode(profile.mode, item.column, item.row);
        while (cell_range_occupied(item.column, item.row, item.columnSpan, item.rowSpan))
            advance_position_for_mode(profile.mode, item.column, item.row);
    } else {
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

void GridModel::expand_single_item_to_page(const GridProfile& profile, GridViewport& viewport) {
    if (items.size() != 1)
        return;

    auto& item = items.front();
    item.columnSpan = full_page_columns(profile);
    item.rowSpan = full_page_rows(profile);
    if (!activeIndex || *activeIndex >= items.size())
        activeIndex = 0;
    ensure_active_visible(profile, viewport);
}

void GridModel::settle_after_removal(const GridProfile& profile, GridViewport& viewport) {
    if (items.empty())
        return;

    if (items.size() == 1)
        expand_single_item_to_page(profile, viewport);

    if (!activeIndex || *activeIndex >= items.size())
        activeIndex = 0;

    ensure_active_visible(profile, viewport);

    auto minColumn = items.front().column;
    auto maxColumnEnd = items.front().column + items.front().columnSpan;
    auto minRow = items.front().row;
    auto maxRowEnd = items.front().row + items.front().rowSpan;
    for (const auto& item : items) {
        minColumn = std::min(minColumn, item.column);
        maxColumnEnd = std::max(maxColumnEnd, item.column + item.columnSpan);
        minRow = std::min(minRow, item.row);
        maxRowEnd = std::max(maxRowEnd, item.row + item.rowSpan);
    }

    clamp_viewport_axis_to_occupied_range(minColumn, maxColumnEnd, profile.visibleColumns, viewport.originColumn);
    clamp_viewport_axis_to_occupied_range(minRow, maxRowEnd, profile.visibleRows, viewport.originRow);
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

GridMoveResult GridModel::move_active_window(Direction direction,
                                             const GridProfile& profile,
                                             GridViewport& viewport) {
    if (!activeIndex || *activeIndex >= items.size())
        return GridMoveResult::NoOp;

    int columnDelta = 0;
    int rowDelta = 0;
    switch (direction) {
    case Direction::Left:
        columnDelta = -1;
        break;
    case Direction::Right:
        columnDelta = 1;
        break;
    case Direction::Up:
        rowDelta = -1;
        break;
    case Direction::Down:
        rowDelta = 1;
        break;
    default:
        return GridMoveResult::NoOp;
    }

    auto& active = items[*activeIndex];
    const auto nextColumn = active.column + columnDelta;
    const auto nextRow = active.row + rowDelta;
    if (const auto occupied = first_occupied_index(nextColumn, nextRow, active.columnSpan, active.rowSpan, activeIndex)) {
        std::swap(active.column, items[*occupied].column);
        std::swap(active.row, items[*occupied].row);
        std::swap(active.columnSpan, items[*occupied].columnSpan);
        std::swap(active.rowSpan, items[*occupied].rowSpan);
    } else {
        active.column = nextColumn;
        active.row = nextRow;
    }

    ensure_active_visible(profile, viewport);
    return GridMoveResult::Moved;
}

GridMoveResult GridModel::move_active_window_to_page(Direction direction,
                                                     const GridProfile& profile,
                                                     GridViewport& viewport) {
    if (!activeIndex || *activeIndex >= items.size())
        return GridMoveResult::NoOp;

    int columnDelta = 0;
    int rowDelta = 0;
    switch (direction) {
    case Direction::Left:
        columnDelta = -std::max(1, profile.visibleColumns);
        break;
    case Direction::Right:
        columnDelta = std::max(1, profile.visibleColumns);
        break;
    case Direction::Up:
        rowDelta = -std::max(1, profile.visibleRows);
        break;
    case Direction::Down:
        rowDelta = std::max(1, profile.visibleRows);
        break;
    default:
        return GridMoveResult::NoOp;
    }

    auto& active = items[*activeIndex];
    auto nextColumn = active.column + columnDelta;
    auto nextRow = active.row + rowDelta;
    for (int attempts = 0; attempts < 1024 &&
         first_occupied_index(nextColumn, nextRow, active.columnSpan, active.rowSpan, activeIndex); ++attempts) {
        nextColumn += columnDelta == 0 ? 0 : (columnDelta > 0 ? 1 : -1);
        nextRow += rowDelta == 0 ? 0 : (rowDelta > 0 ? 1 : -1);
    }

    if (first_occupied_index(nextColumn, nextRow, active.columnSpan, active.rowSpan, activeIndex))
        return GridMoveResult::NoOp;

    active.column = nextColumn;
    active.row = nextRow;
    ensure_active_visible(profile, viewport);
    return GridMoveResult::Moved;
}

GridMoveResult GridModel::set_active_span(int columnSpan,
                                          int rowSpan,
                                          const GridProfile& profile,
                                          GridViewport& viewport) {
    if (!activeIndex || *activeIndex >= items.size())
        return GridMoveResult::NoOp;

    columnSpan = std::max(1, columnSpan);
    rowSpan = std::max(1, rowSpan);

    auto& active = items[*activeIndex];
    if (first_occupied_index(active.column, active.row, columnSpan, rowSpan, activeIndex))
        return GridMoveResult::NoOp;

    active.columnSpan = columnSpan;
    active.rowSpan = rowSpan;
    ensure_active_visible(profile, viewport);
    return GridMoveResult::Moved;
}

GridMoveResult GridModel::resize_active_item(int step,
                                             const GridProfile& profile,
                                             GridViewport& viewport) {
    if (!activeIndex || *activeIndex >= items.size() || step == 0)
        return GridMoveResult::NoOp;

    const auto& active = items[*activeIndex];
    if (profile.mode == Mode::Column) {
        const auto maxSpan = std::max(1, profile.visibleRows);
        auto nextSpan = active.rowSpan + step;
        if (nextSpan > maxSpan)
            nextSpan = 1;
        if (nextSpan < 1)
            nextSpan = maxSpan;
        return set_active_span(active.columnSpan, nextSpan, profile, viewport);
    }

    const auto maxSpan = std::max(1, profile.visibleColumns);
    auto nextSpan = active.columnSpan + step;
    if (nextSpan > maxSpan)
        nextSpan = 1;
    if (nextSpan < 1)
        nextSpan = maxSpan;
    return set_active_span(nextSpan, active.rowSpan, profile, viewport);
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

    if (const auto bestIndex = focus_candidate_index(direction)) {
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

GridMoveResult GridModel::align_active(Direction direction,
                                       const GridProfile& profile,
                                       GridViewport& viewport) {
    const auto* active = active_item();
    if (!active)
        return GridMoveResult::NoOp;

    switch (direction) {
    case Direction::Left:
        viewport.originColumn = active->column;
        break;
    case Direction::Right:
        viewport.originColumn = active->column + active->columnSpan - std::max(1, profile.visibleColumns);
        break;
    case Direction::Up:
        viewport.originRow = active->row;
        break;
    case Direction::Down:
        viewport.originRow = active->row + active->rowSpan - std::max(1, profile.visibleRows);
        break;
    case Direction::Center:
        viewport.originColumn = active->column - (std::max(1, profile.visibleColumns) - active->columnSpan) / 2;
        viewport.originRow = active->row - (std::max(1, profile.visibleRows) - active->rowSpan) / 2;
        break;
    case Direction::Begin:
        viewport.originColumn = 0;
        viewport.originRow = 0;
        break;
    case Direction::End:
        ensure_active_visible(profile, viewport);
        break;
    default:
        return GridMoveResult::NoOp;
    }

    return GridMoveResult::Moved;
}

std::vector<RenderedGridItem> GridModel::render(const GridViewport& viewport,
                                                const GridProfile& profile,
                                                const ScrollerCore::Box& fullBox,
                                                const ScrollerCore::Box& workareaBox,
                                                double gap) const {
    std::vector<RenderedGridItem> rendered;
    rendered.reserve(items.size());
    for (const auto& item : items)
        rendered.push_back(render_grid_item(item, viewport, profile, fullBox, workareaBox, gap));
    return rendered;
}

ScrollerSnapshot::GridSnapshot GridModel::capture_snapshot(const GridViewport& viewport) const {
    ScrollerSnapshot::GridSnapshot snapshot;
    snapshot.enabled = true;
    snapshot.activeItemIndex = activeIndex ? static_cast<int>(*activeIndex) : -1;
    snapshot.viewportColumn = viewport.originColumn;
    snapshot.viewportRow = viewport.originRow;
    snapshot.items.reserve(items.size());
    for (const auto& item : items) {
        snapshot.items.push_back({
            .key = item.key,
            .column = item.column,
            .row = item.row,
            .columnSpan = item.columnSpan,
            .rowSpan = item.rowSpan,
        });
    }
    return snapshot;
}

void GridModel::restore_snapshot(const ScrollerSnapshot::GridSnapshot& snapshot) {
    items.clear();
    items.reserve(snapshot.items.size());
    for (const auto& item : snapshot.items) {
        if (item.key == 0)
            continue;

        items.push_back({
            .key = item.key,
            .column = item.column,
            .row = item.row,
            .columnSpan = std::max(1, item.columnSpan),
            .rowSpan = std::max(1, item.rowSpan),
        });
    }

    if (snapshot.activeItemIndex >= 0 && static_cast<size_t>(snapshot.activeItemIndex) < items.size())
        activeIndex = static_cast<size_t>(snapshot.activeItemIndex);
    else if (!items.empty())
        activeIndex = 0;
    else
        activeIndex.reset();
}

} // namespace ScrollerGrid
