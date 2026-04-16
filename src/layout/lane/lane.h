/**
 * @file lane.h
 * @brief Lane-level workspace controller for scroller layout.
 *
 * `Lane` owns ordered stacks for a single workspace and handles lane-level
 * focus movement, command dispatch behavior, fullscreen/maximize transitions,
 * and geometry updates.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include "../../core/owner_index.h"
#include "../../core/types.h"
#include "../../model/stack.h"

/**
 * @brief Transfer object used when moving an active window between lanes.
 *
 * A moved window needs more than the model window itself: the destination lane also
 * needs the stack width semantics and any free-width value so it can rebuild a
 * destination stack without losing sizing intent.
 */
struct ActiveWindowPayload {
    std::unique_ptr<ScrollerModel::Window> window;
    ScrollerModel::StackWidth              width = ScrollerModel::StackWidth::OneHalf;
    // When width is Free, carry the stack's primary-axis span into the next lane.
    double                  primarySpan = 0.0;

    ActiveWindowPayload() = default;
    ActiveWindowPayload(const ActiveWindowPayload &) = delete;
    ActiveWindowPayload &operator=(const ActiveWindowPayload &) = delete;
    ActiveWindowPayload(ActiveWindowPayload &&other) noexcept = default;
    ActiveWindowPayload &operator=(ActiveWindowPayload &&other) noexcept = default;

    // Release ownership of the moved model window to the destination consumer.
    std::unique_ptr<ScrollerModel::Window> release_window() {
        return std::move(window);
    }

    explicit operator bool() const {
        return static_cast<bool>(window);
    }
};

struct ActiveWindowRestorePlan {
    Direction direction = Direction::End;
    bool      restoreIntoCurrentStack = false;
    bool      insertBeforeCurrent = false;
};

class Lane {
    // A lane owns the ordered stacks visible on one canvas strip.
public:
    Lane(PHLWINDOW window);
    Lane(PHLMONITOR monitor, Mode mode);
    Lane(ScrollerModel::Stack *stack);
    ~Lane();

    // Structural and state queries.
    bool empty() const;
    bool is_single_window_lane() const;
    Mode get_mode() const;
    bool is_ephemeral() const;
    void set_ephemeral(bool value);
    bool has_window(PHLWINDOW window) const;
    template <typename Fn>
    void for_each_window(Fn&& fn) const {
        for (auto col = stacks.first(); col != nullptr; col = col->next())
            col->data()->for_each_window(std::forward<Fn>(fn));
    }
    PHLWINDOW get_active_window() const;
    bool is_active(PHLWINDOW window) const;

    // Window/stack membership changes.
    void add_active_window(PHLWINDOW window);
    ActiveWindowRestorePlan capture_active_window_restore_plan(Direction direction) const;
    ScrollerModel::Stack *extract_active_stack();
    // Remove the active window and transfer ownership of its model payload to the caller.
    ActiveWindowPayload extract_active_window_payload();
    // Consume a previously extracted payload and transfer ownership into this lane.
    void insert_window_payload(ActiveWindowPayload payload, Direction direction);
    // Restore a previously extracted payload back into this lane after a failed handoff.
    void restore_active_window_payload(ActiveWindowPayload payload, const ActiveWindowRestorePlan &plan);
    void set_canvas_geometry(const ScrollerCore::Box &full_box,
                             const ScrollerCore::Box &max_box,
                             int gap_size);

    // Remove a window and re-adapt lanes and stacks, returning true on success.
    bool remove_window(PHLWINDOW window);
    bool swapWindows(PHLWINDOW a, PHLWINDOW b);
    void focus_window(PHLWINDOW window);
    bool active_item_at_edge(Direction direction) const;
    bool active_stack_has_multiple_windows() const;
    FocusMoveResult move_focus(Direction dir, bool focus_wrap);

    // Command-facing stack and window operations.
    void resize_active_stack(int step);
    void resize_active_window(const Vector2D &delta);
    void set_mode(Mode m);
    void align_stack(Direction dir);
    void move_active_stack(Direction dir);
    void move_active_window_to_adjacent_stack(Direction dir);
    void move_active_window_to_new_stack(Direction dir);
    void admit_window_left();
    void expel_window_right();
    Vector2D predict_window_size() const;
    void update_sizes(PHLMONITOR monitor);
    void set_fullscreen_active_window();
    void toggle_fullscreen_active_window();
    void toggle_maximize_active_stack();
    void fit_size(FitSize fitsize);
    void recalculate_lane_geometry();

private:
    static uintptr_t windowKey(PHLWINDOW window) {
        return reinterpret_cast<uintptr_t>(window.get());
    }
    ScrollerModel::Stack *getStackForWindow(PHLWINDOW window) const;
    ListNode<ScrollerModel::Stack *> *getStackNode(ScrollerModel::Stack *stack) const;
    void rememberWindowStack(PHLWINDOW window, ScrollerModel::Stack *stack);
    void forgetWindowStack(PHLWINDOW window);
    void rememberStackWindows(ScrollerModel::Stack *stack);
    void forgetStackWindows(ScrollerModel::Stack *stack);
    void debugVerifyStackCache() const;

    // Calculate lateral gaps for a stack based on neighbor presence.
    Vector2D calculate_gap_x(const ListNode<ScrollerModel::Stack *> *stack) const;

    FocusMoveResult move_focus_backward_stack(Direction direction, bool focus_wrap);
    FocusMoveResult move_focus_forward_stack(Direction direction, bool focus_wrap);
    void move_focus_begin();
    void move_focus_end();

    void center_active_stack();
    void adjust_stacks(ListNode<ScrollerModel::Stack *> *stack);
    // Return the stack span that should be preserved when moving a free-sized stack.
    double stack_primary_span_for_transfer(const ScrollerModel::Stack *stack) const;
    // Rebuild a single-window stack from a transferred payload using this lane's mode/bounds.
    ScrollerModel::Stack *create_stack_from_payload(ActiveWindowPayload payload);
    // Position a freshly inserted stack immediately before/after a reference stack on the lane axis.
    void position_stack_relative_to_reference(ScrollerModel::Stack *stack,
                                             const ScrollerModel::Stack *reference,
                                             Direction direction);

    // Raw monitor bounds for this lane's current canvas placement.
    ScrollerCore::Box full;
    // Workarea bounds after reserved areas and gaps are applied.
    ScrollerCore::Box max;
    // Whether this lane is a temporary navigation-only lane.
    bool ephemeral;
    // Inner gap used between stacked windows.
    int gap;
    // Current reorder policy for relayout decisions.
    ScrollerModel::Reorder reorder;
    // Current navigation/insertion mode for the lane.
    Mode mode;
    // Active stack node inside this lane.
    ListNode<ScrollerModel::Stack *> *active;
    // Ordered stacks owned by this lane.
    List<ScrollerModel::Stack *> stacks;
    // Cached window -> stack index used to avoid repeated whole-lane scans.
    mutable ScrollerCore::OwnerIndex<uintptr_t, ScrollerModel::Stack> stackByWindow;
};
