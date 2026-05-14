#!/usr/bin/env bash

# Launch a nested Hyprland session with two scrollergrid monitors:
# - WAYLAND-1: landscape
# - WAYLAND-2: portrait via transform,1
# Then verify cross-orientation movefocus and movewindow in both directions.

set -euo pipefail

die() {
    printf 'error: %s\n' "$*" >&2
    if [[ -n "${RUN_DIR:-}" ]]; then
        hyprctl clients -j >"$RUN_DIR/failure-outer-clients.json" 2>/dev/null || true
    fi
    if [[ -n "${RUN_DIR:-}" && -n "${NESTED_INSTANCE:-}" ]]; then
        hyprctl -i "$NESTED_INSTANCE" monitors all -j >"$RUN_DIR/failure-monitors.json" 2>/dev/null || true
        hyprctl -i "$NESTED_INSTANCE" clients -j >"$RUN_DIR/failure-clients.json" 2>/dev/null || true
        hyprctl -i "$NESTED_INSTANCE" activewindow -j >"$RUN_DIR/failure-activewindow.json" 2>/dev/null || true
        hyprctl -i "$NESTED_INSTANCE" cursorpos -j >"$RUN_DIR/failure-cursorpos.json" 2>/dev/null || true
        printf 'failure state: %s\n' "$RUN_DIR" >&2
    fi
    exit 1
}

log_step() {
    printf '%s\n' "$*" | tee -a "${SUMMARY_PATH:-/dev/null}" >&2
}

usage() {
    cat <<'EOF'
Usage: scripts/repro-grid-cross-orientation.sh [options]

Options:
  --outer-monitor NAME   Place the floating nested output windows on NAME.
  --window-size WxH      Base landscape output size. Default: 1800x1200.
  --plugin PATH          Plugin .so to load. Default: ./Debug/hyprscroller.so.
  --keep-open            Leave the nested Hyprland instance running after setup.
  -h, --help             Show this help text.

The script exits non-zero when scrollergrid cannot move focus or windows
between a landscape monitor and a portrait monitor in the nested session.
EOF
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

quote_command() {
    local quoted
    printf -v quoted '%q ' "$@"
    printf '%s' "${quoted% }"
}

detect_outer_monitor() {
    hyprctl monitors -j | jq -r '
        (map(select(.focused != true and .disabled != true)) | .[0].name) //
        (map(select((.transform % 2) == 1 and .disabled != true)) | .[0].name) //
        (map(select(.focused)) | .[0].name) //
        empty
    '
}

pick_terminal_kind() {
    if [[ -n "${HYPRSCROLLER_REPRO_TERMINAL:-}" ]]; then
        case "$HYPRSCROLLER_REPRO_TERMINAL" in
            kitty|alacritty)
                command -v "$HYPRSCROLLER_REPRO_TERMINAL" >/dev/null 2>&1 \
                    || die "requested terminal not found: $HYPRSCROLLER_REPRO_TERMINAL"
                TERMINAL_KIND="$HYPRSCROLLER_REPRO_TERMINAL"
                return
                ;;
            *)
                die "unsupported HYPRSCROLLER_REPRO_TERMINAL: $HYPRSCROLLER_REPRO_TERMINAL"
                ;;
        esac
    fi

    if command -v kitty >/dev/null 2>&1; then
        TERMINAL_KIND="kitty"
        return
    fi

    if command -v alacritty >/dev/null 2>&1; then
        TERMINAL_KIND="alacritty"
        return
    fi

    die "need one of: kitty, alacritty"
}

alternate_terminal_kind() {
    case "$1" in
        kitty)
            command -v alacritty >/dev/null 2>&1 && printf 'alacritty'
            ;;
        alacritty)
            command -v kitty >/dev/null 2>&1 && printf 'kitty'
            ;;
    esac
}

launch_nested_exec() {
    local command
    command="$(quote_command "$@")"
    hyprctl -i "$NESTED_INSTANCE" dispatch exec "$command" >/dev/null
}

launch_nested_exec_with_rules() {
    local rules="$1"
    local command
    shift
    command="$(quote_command "$@")"
    hyprctl -i "$NESTED_INSTANCE" dispatch exec "$rules $command" >/dev/null
}

nested_monitor_count() {
    local count
    count="$(hyprctl -i "$NESTED_INSTANCE" monitors -j 2>/dev/null | jq 'length' 2>/dev/null || printf '0')"
    [[ "$count" =~ ^[0-9]+$ ]] || count=0
    printf '%s\n' "$count"
}

wait_for_nested_ready() {
    for ((attempt = 0; attempt < 80; ++attempt)); do
        if hyprctl -i "$NESTED_INSTANCE" activeworkspace -j >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

wait_for_instance_exit() {
    for ((attempt = 0; attempt < 80; ++attempt)); do
        if ! hyprctl instances -j | jq -e --arg instance "$NESTED_INSTANCE" 'any(.[]; .instance == $instance)' >/dev/null; then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

wait_for_nested_monitor_count() {
    local expected="$1"
    for ((attempt = 0; attempt < 80; ++attempt)); do
        if (( "$(nested_monitor_count)" >= expected )); then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

outer_client_count() {
    hyprctl clients -j | jq '
        map(select(.class == "aquamarine" and (.title == "aquamarine - WAYLAND-1" or .title == "aquamarine - WAYLAND-2")))
        | length
    '
}

wait_for_outer_output_window_count() {
    local expected="$1"
    for ((attempt = 0; attempt < 80; ++attempt)); do
        if (( "$(outer_client_count)" >= expected )); then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

wait_for_monitor_usable() {
    local monitor="$1"
    for ((attempt = 0; attempt < 100; ++attempt)); do
        if hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -e --arg monitor "$monitor" '
            any(.[]; .name == $monitor and ((.width // 0) > 0) and ((.height // 0) > 0))
        ' >/dev/null; then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

wait_for_window_title() {
    local title="$1"
    for ((attempt = 0; attempt < 100; ++attempt)); do
        if hyprctl -i "$NESTED_INSTANCE" clients -j | jq -e --arg title "$title" 'any(.[]; .title == $title)' >/dev/null; then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

launch_terminal_window_with_kind() {
    local kind="$1"
    local class="$2"
    local title="$3"
    local rules="${4:-}"
    local body="printf '%s\\n' '$title'; exec bash"

    case "$kind" in
        kitty)
            if [[ -n "$rules" ]]; then
                launch_nested_exec_with_rules "$rules" kitty --class "$class" --title "$title" bash -lc "$body"
            else
                launch_nested_exec kitty --class "$class" --title "$title" bash -lc "$body"
            fi
            ;;
        alacritty)
            if [[ -n "$rules" ]]; then
                launch_nested_exec_with_rules "$rules" alacritty --class "$class" --title "$title" -e bash -lc "$body"
            else
                launch_nested_exec alacritty --class "$class" --title "$title" -e bash -lc "$body"
            fi
            ;;
        *)
            die "unsupported terminal kind: $TERMINAL_KIND"
            ;;
    esac
}

launch_terminal_window_and_wait() {
    local class="$1"
    local title="$2"
    local rules="${3:-}"
    local fallback

    launch_terminal_window_with_kind "$TERMINAL_KIND" "$class" "$title" "$rules"
    if wait_for_window_title "$title"; then
        return 0
    fi

    fallback="$(alternate_terminal_kind "$TERMINAL_KIND")"
    if [[ -n "$fallback" ]]; then
        launch_terminal_window_with_kind "$fallback" "$class" "$title" "$rules"
        wait_for_window_title "$title"
        return
    fi

    return 1
}

monitor_json() {
    local monitor="$1"
    hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -c --arg monitor "$monitor" '
        .[] | select(.name == $monitor)
    '
}

monitor_coordinate_width() {
    local monitor="$1"
    monitor_json "$monitor" | jq -r '
        if (.transform % 2) == 1 then .height else .width end
    '
}

monitor_coordinate_height() {
    local monitor="$1"
    monitor_json "$monitor" | jq -r '
        if (.transform % 2) == 1 then .width else .height end
    '
}

move_cursor_to_monitor() {
    local monitor="$1"
    local monitor_x monitor_y monitor_w monitor_h cursor_x cursor_y

    monitor_x="$(monitor_json "$monitor" | jq -r '.x')"
    monitor_y="$(monitor_json "$monitor" | jq -r '.y')"
    monitor_w="$(monitor_coordinate_width "$monitor")"
    monitor_h="$(monitor_coordinate_height "$monitor")"
    cursor_x=$(( monitor_x + (monitor_w / 2) ))
    cursor_y=$(( monitor_y + (monitor_h / 2) ))

    hyprctl -i "$NESTED_INSTANCE" dispatch movecursor "$cursor_x" "$cursor_y" >/dev/null
    sleep 0.1
}

focus_monitor_workspace() {
    local monitor="$1"
    local workspace="$2"
    move_cursor_to_monitor "$monitor"
    hyprctl -i "$NESTED_INSTANCE" dispatch focusmonitor "$monitor" >/dev/null
    hyprctl -i "$NESTED_INSTANCE" dispatch workspace "$workspace" >/dev/null
    move_cursor_to_monitor "$monitor"
    sleep 0.3
}

active_window_title() {
    hyprctl -i "$NESTED_INSTANCE" activewindow -j | jq -r '.title // empty'
}

focus_title() {
    local title="$1"
    hyprctl -i "$NESTED_INSTANCE" dispatch focuswindow "title:^${title}$" >/dev/null
    for ((attempt = 0; attempt < 40; ++attempt)); do
        if [[ "$(active_window_title)" == "$title" ]]; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

client_workspace_id() {
    local title="$1"
    hyprctl -i "$NESTED_INSTANCE" clients -j | jq -r --arg title "$title" '
        .[] | select(.title == $title) | .workspace.id
    '
}

client_monitor_id() {
    local title="$1"
    hyprctl -i "$NESTED_INSTANCE" clients -j | jq -r --arg title "$title" '
        .[] | select(.title == $title) | .monitor
    '
}

client_field() {
    local title="$1"
    local field="$2"
    hyprctl -i "$NESTED_INSTANCE" clients -j | jq -r --arg title "$title" --arg field "$field" '
        .[] | select(.title == $title)
        | if $field == "x" then .at[0]
          elif $field == "y" then .at[1]
          elif $field == "w" then .size[0]
          elif $field == "h" then .size[1]
          else empty end
    '
}

assert_ok() {
    local label="$1"
    printf '%s: OK\n' "$label" | tee -a "$SUMMARY_PATH"
}

assert_active_title() {
    local expected="$1"
    local label="$2"
    local actual
    actual="$(active_window_title)"
    [[ "$actual" == "$expected" ]] || die "$label: expected active title '$expected', got '$actual'"
    assert_ok "$label"
}

assert_client_location() {
    local title="$1"
    local expected_workspace="$2"
    local expected_monitor="$3"
    local label="$4"
    local actual_workspace actual_monitor
    actual_workspace="$(client_workspace_id "$title")"
    actual_monitor="$(client_monitor_id "$title")"
    [[ "$actual_workspace" == "$expected_workspace" ]] || die "$label: expected workspace $expected_workspace, got $actual_workspace"
    [[ "$actual_monitor" == "$expected_monitor" ]] || die "$label: expected monitor $expected_monitor, got $actual_monitor"
    assert_ok "$label"
}

assert_client_left_of() {
    local left_title="$1"
    local right_title="$2"
    local label="$3"
    local left_x right_x
    left_x="$(client_field "$left_title" x)"
    right_x="$(client_field "$right_title" x)"
    [[ "$left_x" =~ ^-?[0-9]+$ && "$right_x" =~ ^-?[0-9]+$ ]] \
        || die "$label: could not read client x coordinates"
    (( left_x < right_x )) || die "$label: expected $left_title x=$left_x left of $right_title x=$right_x"
    assert_ok "$label"
}

assert_client_right_of() {
    local right_title="$1"
    local left_title="$2"
    local label="$3"
    local right_x left_x
    right_x="$(client_field "$right_title" x)"
    left_x="$(client_field "$left_title" x)"
    [[ "$right_x" =~ ^-?[0-9]+$ && "$left_x" =~ ^-?[0-9]+$ ]] \
        || die "$label: could not read client x coordinates"
    (( right_x > left_x )) || die "$label: expected $right_title x=$right_x right of $left_title x=$left_x"
    assert_ok "$label"
}

assert_client_above() {
    local top_title="$1"
    local bottom_title="$2"
    local label="$3"
    local top_y bottom_y
    top_y="$(client_field "$top_title" y)"
    bottom_y="$(client_field "$bottom_title" y)"
    [[ "$top_y" =~ ^-?[0-9]+$ && "$bottom_y" =~ ^-?[0-9]+$ ]] \
        || die "$label: could not read client y coordinates"
    (( top_y < bottom_y )) || die "$label: expected $top_title y=$top_y above $bottom_title y=$bottom_y"
    assert_ok "$label"
}

assert_client_below() {
    local bottom_title="$1"
    local top_title="$2"
    local label="$3"
    local bottom_y top_y
    bottom_y="$(client_field "$bottom_title" y)"
    top_y="$(client_field "$top_title" y)"
    [[ "$bottom_y" =~ ^-?[0-9]+$ && "$top_y" =~ ^-?[0-9]+$ ]] \
        || die "$label: could not read client y coordinates"
    (( bottom_y > top_y )) || die "$label: expected $bottom_title y=$bottom_y below $top_title y=$top_y"
    assert_ok "$label"
}

assert_client_fills_monitor() {
    local title="$1"
    local monitor="$2"
    local label="$3"
    local expected_monitor actual_monitor client_w client_h monitor_w monitor_h min_w min_h

    expected_monitor="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r --arg name "$monitor" '.[] | select(.name == $name) | .id')"
    actual_monitor="$(client_monitor_id "$title")"
    [[ "$actual_monitor" == "$expected_monitor" ]] \
        || die "$label: expected monitor $expected_monitor, got $actual_monitor"

    client_w="$(client_field "$title" w)"
    client_h="$(client_field "$title" h)"
    monitor_w="$(monitor_coordinate_width "$monitor")"
    monitor_h="$(monitor_coordinate_height "$monitor")"
    min_w=$(( monitor_w - 180 ))
    min_h=$(( monitor_h - 180 ))
    (( min_w < 1 )) && min_w=1
    (( min_h < 1 )) && min_h=1

    [[ "$client_w" =~ ^[0-9]+$ && "$client_h" =~ ^[0-9]+$ ]] \
        || die "$label: could not read client size"
    (( client_w >= min_w && client_h >= min_h )) \
        || die "$label: expected $title to fill monitor ${monitor_w}x${monitor_h}, got ${client_w}x${client_h}"
    assert_ok "$label"
}

assert_titles_visible_on_monitor() {
    local monitor="$1"
    local label="$2"
    shift 2

    local monitor_id monitor_x monitor_y monitor_w monitor_h
    monitor_id="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r --arg name "$monitor" '.[] | select(.name == $name) | .id')"
    monitor_x="$(monitor_json "$monitor" | jq -r '.x')"
    monitor_y="$(monitor_json "$monitor" | jq -r '.y')"
    monitor_w="$(monitor_coordinate_width "$monitor")"
    monitor_h="$(monitor_coordinate_height "$monitor")"

    local title
    for title in "$@"; do
        hyprctl -i "$NESTED_INSTANCE" clients -j | jq -e \
            --arg title "$title" \
            --argjson monitorId "$monitor_id" \
            --argjson monitorX "$monitor_x" \
            --argjson monitorY "$monitor_y" \
            --argjson monitorW "$monitor_w" \
            --argjson monitorH "$monitor_h" '
                any(.[]; .title == $title
                    and .monitor == $monitorId
                    and (.at[0] < ($monitorX + $monitorW))
                    and ((.at[0] + .size[0]) > $monitorX)
                    and (.at[1] < ($monitorY + $monitorH))
                    and ((.at[1] + .size[1]) > $monitorY))
            ' >/dev/null || die "$label: expected visible '$title' on $monitor"
    done

    assert_ok "$label"
}

assert_active_monitor() {
    local expected_monitor="$1"
    local label="$2"
    local actual_monitor
    actual_monitor="$(hyprctl -i "$NESTED_INSTANCE" activewindow -j | jq -r '.monitor // empty')"
    [[ "$actual_monitor" == "$expected_monitor" ]] \
        || die "$label: expected active monitor $expected_monitor, got $actual_monitor"
    assert_ok "$label"
}

assert_all_titles_present() {
    local titles_json="$1"
    local label="$2"

    hyprctl -i "$NESTED_INSTANCE" clients -j | jq -e --argjson titles "$titles_json" '
        . as $clients | all($titles[]; . as $title | any($clients[]; .title == $title))
    ' >/dev/null || die "$label: one or more expected windows disappeared"

    assert_ok "$label"
}

repair_client_workspace() {
    local title="$1"
    local workspace="$2"

    if [[ "$(client_workspace_id "$title")" == "$workspace" ]]; then
        return 0
    fi

    focus_title "$title" || return 1
    hyprctl -i "$NESTED_INSTANCE" dispatch movetoworkspacesilent "$workspace" >/dev/null
    sleep 0.5
}

compute_outer_output_layout() {
    local outer_monitor_json
    outer_monitor_json="$(hyprctl monitors -j | jq -c --arg monitor "$OUTER_MONITOR" '
        map(select(.name == $monitor)) | .[0] // empty
    ')"
    [[ -n "$outer_monitor_json" ]] || die "could not resolve outer monitor geometry for $OUTER_MONITOR"

    hyprscroller_monitor_logical_geometry \
        "$outer_monitor_json" \
        OUTER_MONITOR_X \
        OUTER_MONITOR_Y \
        OUTER_MONITOR_WIDTH \
        OUTER_MONITOR_HEIGHT

    hyprscroller_compute_two_orientation_outer_layout \
        "$outer_monitor_json" \
        "$WINDOW_WIDTH" \
        "$WINDOW_HEIGHT" \
        96 \
        64 \
        100 \
        OUTER_OUTPUT_XS \
        OUTER_OUTPUT_YS \
        OUTER_OUTPUT_WIDTHS \
        OUTER_OUTPUT_HEIGHTS \
        || die "computed an invalid outer nested output size"
}

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$REPO_ROOT/scripts/lib/nested-monitor-layouts.sh"

PLUGIN_PATH="$REPO_ROOT/Debug/hyprscroller.so"
WINDOW_WIDTH=1800
WINDOW_HEIGHT=1200
KEEP_OPEN=0
OUTER_MONITOR=""
NESTED_INSTANCE=""
NESTED_PID=""
TERMINAL_KIND=""
LANDSCAPE_MONITOR="WAYLAND-1"
PORTRAIT_MONITOR="WAYLAND-2"
LANDSCAPE_TITLE="grid-landscape"
LANDSCAPE_EXTRA_TITLE="grid-landscape-extra"
LANDSCAPE_TAIL_TITLE="grid-landscape-tail"
PORTRAIT_TITLE="grid-portrait"
PORTRAIT_EXTRA_TITLE="grid-portrait-extra"
PORTRAIT_TAIL_TITLE="grid-portrait-tail"
LANDSCAPE_CLASS="hs-grid-landscape"
LANDSCAPE_EXTRA_CLASS="hs-grid-landscape-extra"
LANDSCAPE_TAIL_CLASS="hs-grid-landscape-tail"
PORTRAIT_CLASS="hs-grid-portrait"
PORTRAIT_EXTRA_CLASS="hs-grid-portrait-extra"
PORTRAIT_TAIL_CLASS="hs-grid-portrait-tail"
ALL_GRID_TITLES_JSON='["grid-landscape","grid-landscape-extra","grid-landscape-tail","grid-portrait","grid-portrait-extra","grid-portrait-tail"]'
OUTER_MONITOR_X=0
OUTER_MONITOR_Y=0
OUTER_MONITOR_WIDTH=0
OUTER_MONITOR_HEIGHT=0
OUTER_OUTPUT_TITLES=()
OUTER_OUTPUT_XS=()
OUTER_OUTPUT_YS=()
OUTER_OUTPUT_WIDTHS=()
OUTER_OUTPUT_HEIGHTS=()
OUTER_OUTPUT_EVENT_WATCHER_PID=""
OUTER_OUTPUT_EVENT_SOCKET=""
OUTER_OUTPUT_EVENT_STATE=""
OUTER_OUTPUT_EVENT_READY=""

hyprscroller_two_orientation_outer_titles OUTER_OUTPUT_TITLES

cleanup() {
    if [[ -n "${OUTER_OUTPUT_EVENT_WATCHER_PID:-}" ]]; then
        kill "$OUTER_OUTPUT_EVENT_WATCHER_PID" >/dev/null 2>&1 || true
        wait "$OUTER_OUTPUT_EVENT_WATCHER_PID" >/dev/null 2>&1 || true
        OUTER_OUTPUT_EVENT_WATCHER_PID=""
    fi

    if [[ "${KEEP_OPEN:-0}" -eq 1 ]]; then
        return
    fi

    if [[ -n "${NESTED_INSTANCE:-}" ]]; then
        hyprctl -i "$NESTED_INSTANCE" dispatch exit >/dev/null 2>&1 || true
        wait_for_instance_exit || true
    fi

    if [[ -n "${NESTED_PID:-}" ]]; then
        kill "$NESTED_PID" >/dev/null 2>&1 || true
    fi
}

trap cleanup EXIT INT TERM

while [[ $# -gt 0 ]]; do
    case "$1" in
        --outer-monitor)
            [[ $# -ge 2 ]] || die "--outer-monitor requires a value"
            OUTER_MONITOR="$2"
            shift 2
            ;;
        --window-size)
            [[ $# -ge 2 ]] || die "--window-size requires a value like 1800x1200"
            WINDOW_WIDTH="${2%x*}"
            WINDOW_HEIGHT="${2#*x}"
            [[ "$WINDOW_WIDTH" =~ ^[0-9]+$ && "$WINDOW_HEIGHT" =~ ^[0-9]+$ ]] || die "invalid --window-size: $2"
            shift 2
            ;;
        --plugin)
            [[ $# -ge 2 ]] || die "--plugin requires a path"
            PLUGIN_PATH="$2"
            shift 2
            ;;
        --keep-open)
            KEEP_OPEN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
done

require_cmd Hyprland
require_cmd hyprctl
require_cmd jq
require_cmd python3
require_cmd realpath

[[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]] || die "run this inside an existing Hyprland session"
[[ -f "$PLUGIN_PATH" ]] || die "plugin not found: $PLUGIN_PATH"
PLUGIN_PATH="$(realpath "$PLUGIN_PATH")"

pick_terminal_kind
if [[ -z "$OUTER_MONITOR" ]]; then
    OUTER_MONITOR="$(detect_outer_monitor)"
    [[ -n "$OUTER_MONITOR" ]] || die "could not auto-detect an outer monitor"
fi
compute_outer_output_layout

RUN_DIR="$(mktemp -d /tmp/hyprscroller-grid-cross-orientation.XXXXXX)"
CONFIG_PATH="$RUN_DIR/hyprland.conf"
LOG_PATH="$RUN_DIR/hyprland.log"
LAUNCHER_PATH="$RUN_DIR/launch-nested.sh"
SUMMARY_PATH="$RUN_DIR/summary.txt"
RESULT_PATH="$RUN_DIR/result.txt"
OUTER_OUTPUT_EVENT_STATE="$RUN_DIR/outer-output-events.tsv"
OUTER_OUTPUT_EVENT_READY="$RUN_DIR/outer-output-events.ready"

hyprscroller_install_outer_output_map_time_float_rules OUTER_OUTPUT_TITLES \
    || die "failed to install map-time float rules for outer nested output windows"

cat >"$CONFIG_PATH" <<EOF
monitor = , preferred, auto, 1

plugin = $PLUGIN_PATH

env = XCURSOR_SIZE,24

general {
    layout = scrollergrid
    gaps_in = 4
    gaps_out = 8
    border_size = 2
}

decoration {
    rounding = 6
}

misc {
    disable_hyprland_logo = true
    disable_splash_rendering = true
    force_default_wallpaper = 0
}

input {
    kb_layout = us
}

cursor {
    no_hardware_cursors = true
}

debug {
    disable_logs = false
}
EOF

cat >"$LAUNCHER_PATH" <<EOF
#!/usr/bin/env bash
exec Hyprland -c "$CONFIG_PATH" >"$LOG_PATH" 2>&1
EOF
chmod +x "$LAUNCHER_PATH"

OUTER_OUTPUT_EVENT_SOCKET="$(hyprscroller_outer_event_socket_path)" \
    || die "could not resolve outer Hyprland event socket"
hyprscroller_start_outer_output_event_watcher \
    OUTER_OUTPUT_EVENT_WATCHER_PID \
    "$OUTER_OUTPUT_EVENT_SOCKET" \
    "$OUTER_OUTPUT_EVENT_STATE" \
    "$OUTER_OUTPUT_EVENT_READY" \
    "$OUTER_MONITOR_X" \
    "$OUTER_MONITOR_Y" \
    OUTER_OUTPUT_TITLES \
    OUTER_OUTPUT_XS \
    OUTER_OUTPUT_YS \
    OUTER_OUTPUT_WIDTHS \
    OUTER_OUTPUT_HEIGHTS
hyprscroller_wait_for_outer_output_event_watcher_ready "$OUTER_OUTPUT_EVENT_READY" \
    || die "timed out waiting for outer output event watcher"

BEFORE_MAX_TIME="$(hyprctl instances -j | jq '[.[].time] | max // 0')"
LAUNCH_RULES="[monitor $OUTER_MONITOR; float; size ${OUTER_OUTPUT_WIDTHS[0]} ${OUTER_OUTPUT_HEIGHTS[0]}; center]"
log_step "launch nested Hyprland on $OUTER_MONITOR"
hyprctl dispatch exec "$LAUNCH_RULES $LAUNCHER_PATH" >/dev/null

for ((attempt = 0; attempt < 80; ++attempt)); do
    NESTED_INSTANCE="$(hyprctl instances -j | jq -r --argjson before "$BEFORE_MAX_TIME" '
        (map(select(.time > $before)) | max_by(.time)? | .instance) // empty
    ')"
    [[ -n "$NESTED_INSTANCE" ]] && break
    sleep 0.25
done

[[ -n "$NESTED_INSTANCE" ]] || die "failed to detect nested instance"
wait_for_nested_ready || die "nested Hyprland never became ready"
log_step "nested Hyprland ready: $NESTED_INSTANCE"

wait_for_outer_output_window_count 1 || die "timed out waiting for first outer nested output window"
hyprscroller_position_outer_window_index \
    "$OUTER_MONITOR_X" \
    "$OUTER_MONITOR_Y" \
    0 \
    OUTER_OUTPUT_TITLES \
    OUTER_OUTPUT_XS \
    OUTER_OUTPUT_YS \
    OUTER_OUTPUT_WIDTHS \
    OUTER_OUTPUT_HEIGHTS \
    || die "failed to apply first outer nested output geometry"

log_step "create portrait nested wayland output"
hyprscroller_create_positioned_wayland_outputs \
    "$NESTED_INSTANCE" \
    1 \
    2 \
    wait_for_nested_monitor_count \
    wait_for_outer_output_window_count \
    "$OUTER_MONITOR_X" \
    "$OUTER_MONITOR_Y" \
    OUTER_OUTPUT_TITLES \
    OUTER_OUTPUT_XS \
    OUTER_OUTPUT_YS \
    OUTER_OUTPUT_WIDTHS \
    OUTER_OUTPUT_HEIGHTS \
    || die "timed out creating two nested wayland outputs"

hyprscroller_wait_for_outer_output_position_events "$OUTER_OUTPUT_EVENT_STATE" 2 \
    || die "outer output event watcher did not position both nested output windows"
hyprscroller_position_outer_windows \
    "$OUTER_MONITOR_X" \
    "$OUTER_MONITOR_Y" \
    OUTER_OUTPUT_TITLES \
    OUTER_OUTPUT_XS \
    OUTER_OUTPUT_YS \
    OUTER_OUTPUT_WIDTHS \
    OUTER_OUTPUT_HEIGHTS \
    || die "failed to apply outer nested output geometry"

hyprscroller_apply_two_orientation_layout "$NESTED_INSTANCE" "${OUTER_OUTPUT_WIDTHS[0]}" "${OUTER_OUTPUT_HEIGHTS[0]}" 0
wait_for_monitor_usable "$LANDSCAPE_MONITOR" || die "$LANDSCAPE_MONITOR never reported non-zero geometry"
wait_for_monitor_usable "$PORTRAIT_MONITOR" || die "$PORTRAIT_MONITOR never reported non-zero geometry"
log_step "nested monitors ready: $LANDSCAPE_MONITOR landscape, $PORTRAIT_MONITOR portrait"

LANDSCAPE_MONITOR_ID="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r --arg name "$LANDSCAPE_MONITOR" '.[] | select(.name == $name) | .id')"
PORTRAIT_MONITOR_ID="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r --arg name "$PORTRAIT_MONITOR" '.[] | select(.name == $name) | .id')"

focus_monitor_workspace "$LANDSCAPE_MONITOR" 1
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scrollergrid >/dev/null
log_step "launch landscape grid windows"
launch_terminal_window_and_wait "$LANDSCAPE_CLASS" "$LANDSCAPE_TITLE" "[workspace 1 silent]" \
    || die "$LANDSCAPE_TITLE did not appear"
repair_client_workspace "$LANDSCAPE_TITLE" 1 || die "could not place $LANDSCAPE_TITLE on workspace 1"
sleep 0.6
assert_client_location "$LANDSCAPE_TITLE" 1 "$LANDSCAPE_MONITOR_ID" "landscape source placement"
assert_client_fills_monitor "$LANDSCAPE_TITLE" "$LANDSCAPE_MONITOR" "landscape first window spans page"
launch_terminal_window_and_wait "$LANDSCAPE_EXTRA_CLASS" "$LANDSCAPE_EXTRA_TITLE" "[workspace 1 silent]" \
    || die "$LANDSCAPE_EXTRA_TITLE did not appear"
repair_client_workspace "$LANDSCAPE_EXTRA_TITLE" 1 || die "could not place $LANDSCAPE_EXTRA_TITLE on workspace 1"
sleep 0.8
assert_client_location "$LANDSCAPE_EXTRA_TITLE" 1 "$LANDSCAPE_MONITOR_ID" "landscape extra placement"
assert_client_left_of "$LANDSCAPE_TITLE" "$LANDSCAPE_EXTRA_TITLE" "landscape second insert shrinks first window"

focus_monitor_workspace "$PORTRAIT_MONITOR" 2
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scrollergrid >/dev/null
log_step "launch portrait grid windows"
launch_terminal_window_and_wait "$PORTRAIT_CLASS" "$PORTRAIT_TITLE" "[workspace 2 silent]" \
    || die "$PORTRAIT_TITLE did not appear"
repair_client_workspace "$PORTRAIT_TITLE" 2 || die "could not place $PORTRAIT_TITLE on workspace 2"
sleep 0.6
assert_client_location "$PORTRAIT_TITLE" 2 "$PORTRAIT_MONITOR_ID" "portrait target placement"
assert_client_fills_monitor "$PORTRAIT_TITLE" "$PORTRAIT_MONITOR" "portrait first window spans page"
launch_terminal_window_and_wait "$PORTRAIT_EXTRA_CLASS" "$PORTRAIT_EXTRA_TITLE" "[workspace 2 silent]" \
    || die "$PORTRAIT_EXTRA_TITLE did not appear"
repair_client_workspace "$PORTRAIT_EXTRA_TITLE" 2 || die "could not place $PORTRAIT_EXTRA_TITLE on workspace 2"
sleep 0.8
assert_client_location "$PORTRAIT_EXTRA_TITLE" 2 "$PORTRAIT_MONITOR_ID" "portrait extra placement"
assert_client_above "$PORTRAIT_TITLE" "$PORTRAIT_EXTRA_TITLE" "portrait second insert shrinks first window"

focus_monitor_workspace "$LANDSCAPE_MONITOR" 1
focus_title "$LANDSCAPE_TITLE" || die "$LANDSCAPE_TITLE did not become active before same-monitor movewindow r"
log_step "dispatch landscape same-monitor movewindow right/left"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movewindow r >/dev/null
sleep 0.6
assert_client_right_of "$LANDSCAPE_TITLE" "$LANDSCAPE_EXTRA_TITLE" "landscape same-monitor movewindow right"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movewindow l >/dev/null
sleep 0.6
assert_client_left_of "$LANDSCAPE_TITLE" "$LANDSCAPE_EXTRA_TITLE" "landscape same-monitor movewindow left"

focus_monitor_workspace "$PORTRAIT_MONITOR" 2
focus_title "$PORTRAIT_TITLE" || die "$PORTRAIT_TITLE did not become active before same-monitor movewindow down"
log_step "dispatch portrait same-monitor movewindow down/up"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movewindow d >/dev/null
sleep 0.6
assert_client_below "$PORTRAIT_TITLE" "$PORTRAIT_EXTRA_TITLE" "portrait same-monitor movewindow down"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movewindow u >/dev/null
sleep 0.6
assert_client_above "$PORTRAIT_TITLE" "$PORTRAIT_EXTRA_TITLE" "portrait same-monitor movewindow up"

focus_monitor_workspace "$LANDSCAPE_MONITOR" 1
focus_title "$LANDSCAPE_EXTRA_TITLE" || die "$LANDSCAPE_EXTRA_TITLE did not become active before launching landscape tail"
log_step "launch third landscape grid window"
launch_terminal_window_and_wait "$LANDSCAPE_TAIL_CLASS" "$LANDSCAPE_TAIL_TITLE" "[workspace 1 silent]" \
    || die "$LANDSCAPE_TAIL_TITLE did not appear"
repair_client_workspace "$LANDSCAPE_TAIL_TITLE" 1 || die "could not place $LANDSCAPE_TAIL_TITLE on workspace 1"
sleep 0.8
assert_client_location "$LANDSCAPE_TAIL_TITLE" 1 "$LANDSCAPE_MONITOR_ID" "landscape tail placement"

focus_monitor_workspace "$PORTRAIT_MONITOR" 2
focus_title "$PORTRAIT_EXTRA_TITLE" || die "$PORTRAIT_EXTRA_TITLE did not become active before launching portrait tail"
log_step "launch third portrait grid window"
launch_terminal_window_and_wait "$PORTRAIT_TAIL_CLASS" "$PORTRAIT_TAIL_TITLE" "[workspace 2 silent]" \
    || die "$PORTRAIT_TAIL_TITLE did not appear"
repair_client_workspace "$PORTRAIT_TAIL_TITLE" 2 || die "could not place $PORTRAIT_TAIL_TITLE on workspace 2"
sleep 0.8
assert_client_location "$PORTRAIT_TAIL_TITLE" 2 "$PORTRAIT_MONITOR_ID" "portrait tail placement"

focus_monitor_workspace "$LANDSCAPE_MONITOR" 1
focus_title "$LANDSCAPE_TAIL_TITLE" || die "$LANDSCAPE_TAIL_TITLE did not become active before movefocus r"
log_step "dispatch landscape->portrait movefocus"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movefocus r >/dev/null
sleep 0.6
assert_active_monitor "$PORTRAIT_MONITOR_ID" "landscape->portrait movefocus"

focus_monitor_workspace "$PORTRAIT_MONITOR" 2
focus_title "$PORTRAIT_TITLE" || die "$PORTRAIT_TITLE did not become active before movefocus l"
log_step "dispatch portrait->landscape movefocus"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movefocus l >/dev/null
sleep 0.6
assert_active_monitor "$LANDSCAPE_MONITOR_ID" "portrait->landscape movefocus"

focus_monitor_workspace "$LANDSCAPE_MONITOR" 1
focus_title "$LANDSCAPE_TAIL_TITLE" || die "$LANDSCAPE_TAIL_TITLE did not become active before movewindow r"
log_step "dispatch landscape->portrait movewindow"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movewindow r >/dev/null
sleep 0.8
assert_client_location "$LANDSCAPE_TAIL_TITLE" 2 "$PORTRAIT_MONITOR_ID" "landscape->portrait movewindow"
assert_titles_visible_on_monitor "$LANDSCAPE_MONITOR" "landscape remaining pair stays visible after movewindow" \
    "$LANDSCAPE_TITLE" "$LANDSCAPE_EXTRA_TITLE"

focus_monitor_workspace "$PORTRAIT_MONITOR" 2
focus_title "$LANDSCAPE_TAIL_TITLE" || die "$LANDSCAPE_TAIL_TITLE did not become active before movewindow l"
log_step "dispatch portrait->landscape movewindow"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movewindow l >/dev/null
sleep 0.8
assert_client_location "$LANDSCAPE_TAIL_TITLE" 1 "$LANDSCAPE_MONITOR_ID" "portrait->landscape movewindow"

focus_monitor_workspace "$PORTRAIT_MONITOR" 2
focus_title "$PORTRAIT_TAIL_TITLE" || die "$PORTRAIT_TAIL_TITLE did not become active before portrait pair movewindow l"
log_step "dispatch portrait tail out to validate portrait viewport settle"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movewindow l >/dev/null
sleep 0.8
assert_client_location "$PORTRAIT_TAIL_TITLE" 1 "$LANDSCAPE_MONITOR_ID" "portrait tail movewindow to landscape"
assert_titles_visible_on_monitor "$PORTRAIT_MONITOR" "portrait remaining pair stays visible after movewindow" \
    "$PORTRAIT_TITLE" "$PORTRAIT_EXTRA_TITLE"

focus_monitor_workspace "$PORTRAIT_MONITOR" 2
focus_title "$PORTRAIT_EXTRA_TITLE" || die "$PORTRAIT_EXTRA_TITLE did not become active before portrait restore movewindow l"
log_step "dispatch portrait extra out to validate portrait restore"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movewindow l >/dev/null
sleep 0.8
assert_client_location "$PORTRAIT_EXTRA_TITLE" 1 "$LANDSCAPE_MONITOR_ID" "portrait extra movewindow to landscape"
assert_client_fills_monitor "$PORTRAIT_TITLE" "$PORTRAIT_MONITOR" "portrait remaining window restores full page"

assert_all_titles_present "$ALL_GRID_TITLES_JSON" "multi-window cross-orientation retained windows"

log_step "dispatch overview open and accept"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview >/dev/null
sleep 1
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movefocus r >/dev/null
sleep 0.3
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview accept >/dev/null
sleep 0.6
[[ -n "$(active_window_title)" ]] || die "overview accept left no active window"
assert_all_titles_present "$ALL_GRID_TITLES_JSON" "overview retained multi-window grid windows"
assert_ok "overview open and accept with multiple grid windows"

{
    printf 'nested instance:      %s\n' "$NESTED_INSTANCE"
    printf 'plugin path:          %s\n' "$PLUGIN_PATH"
    printf 'config:               %s\n' "$CONFIG_PATH"
    printf 'nested log:           %s\n' "$LOG_PATH"
    printf 'run dir:              %s\n' "$RUN_DIR"
    printf 'outer monitor:        %s\n' "$OUTER_MONITOR"
    printf 'landscape monitor:    %s\n' "$LANDSCAPE_MONITOR"
    printf 'portrait monitor:     %s\n' "$PORTRAIT_MONITOR"
    printf 'outer event state:    %s\n' "$OUTER_OUTPUT_EVENT_STATE"
} >"$RESULT_PATH"

cat "$RESULT_PATH"
cat "$SUMMARY_PATH"
