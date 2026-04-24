#!/usr/bin/env bash

# Launch a nested Hyprland session and verify the canvas-workspace overview flow:
# - outside overview, `scroller:focusmonitor r/l` only moves monitor focus
# - inside overview, `scroller:focusmonitor r/l` only changes the focused canvas
#   preview while overview stays open
# - closing overview accepts the focused canvas and switches every visible
#   monitor together

set -euo pipefail

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: scripts/repro-overview-canvas-workspaces.sh [options]

Options:
  --outer-monitor NAME   Place the floating nested output windows on NAME.
  --window-size WxH      Base outer landscape output size. Default: 1800x1200.
  --plugin PATH          Plugin .so to load. Default: ./Debug/hyprscroller.so.
  --keep-open            Leave the nested Hyprland instance running after setup.
  --hold-seconds N       Delay after opening overview before navigation. Default: 1.
  -h, --help             Show this help text.

The script exits non-zero when the nested test windows are not floating, when
the outer nested output windows are not floating and fixed to the expected
size/position, or when overview close fails to switch all visible monitors to
the focused canvas workspace and back again.
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

launch_nested_exec() {
    local command
    command="$(quote_command "$@")"
    hyprctl -i "$NESTED_INSTANCE" dispatch exec "$command" >/dev/null
}

launch_nested_exec_with_rules() {
    local rules="$1"
    shift

    local command
    command="$(quote_command "$@")"
    hyprctl -i "$NESTED_INSTANCE" dispatch exec "$rules $command" >/dev/null
}

nested_client_count() {
    hyprctl -i "$NESTED_INSTANCE" clients -j | jq 'length'
}

nested_monitor_count() {
    local count
    count="$(hyprctl -i "$NESTED_INSTANCE" monitors -j 2>/dev/null | jq 'length' 2>/dev/null || printf '0')"

    if [[ ! "$count" =~ ^[0-9]+$ ]]; then
        count=0
    fi

    printf '%s\n' "$count"
}

wait_for_nested_client_count() {
    local minimum="$1"

    for ((attempt = 0; attempt < 60; ++attempt)); do
        if (( "$(nested_client_count)" >= minimum )); then
            return 0
        fi

        sleep 0.25
    done

    return 1
}

wait_for_nested_monitor_count() {
    local expected="$1"

    for ((attempt = 0; attempt < 60; ++attempt)); do
        if (( "$(nested_monitor_count)" >= expected )); then
            return 0
        fi

        sleep 0.25
    done

    return 1
}

nested_focused_monitor() {
    hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r '.[] | select(.focused == true) | .name'
}

wait_for_nested_focused_monitor() {
    local expected="$1"

    for ((attempt = 0; attempt < 40; ++attempt)); do
        if [[ "$(nested_focused_monitor)" == "$expected" ]]; then
            return 0
        fi

        sleep 0.1
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

focus_nested_window() {
    local selector="$1"
    hyprctl -i "$NESTED_INSTANCE" dispatch focuswindow "$selector" >/dev/null
}

client_is_floating_by_class() {
    local class="$1"
    hyprctl -i "$NESTED_INSTANCE" clients -j | jq -r --arg class "$class" '
        (map(select(.class == $class)) | .[0].floating) // empty
    '
}

ensure_window_floating() {
    local class="$1"
    focus_nested_window "class:$class"
    sleep 0.2

    local floating_state
    floating_state="$(client_is_floating_by_class "$class")"
    if [[ "$floating_state" != "true" ]]; then
        hyprctl -i "$NESTED_INSTANCE" dispatch togglefloating >/dev/null
        sleep 0.2
        floating_state="$(client_is_floating_by_class "$class")"
    fi

    [[ "$floating_state" == "true" ]] || die "window class $class did not end up floating"
    sleep 0.2
}

launch_terminal_window() {
    local class="$1"
    local title="$2"
    local body="printf '%s\\n' '$title'; exec bash"
    local rules='[float; size 960 720; center]'

    case "$TERMINAL_KIND" in
        kitty)
            launch_nested_exec_with_rules "$rules" kitty \
                --class "$class" \
                --title "$title" \
                -o remember_window_size=no \
                -o initial_window_width=960 \
                -o initial_window_height=720 \
                bash -lc "$body"
            ;;
        alacritty)
            launch_nested_exec_with_rules "$rules" alacritty \
                --class "$class" \
                --title "$title" \
                -o window.dimensions.columns=120 \
                -o window.dimensions.lines=32 \
                -e bash -lc "$body"
            ;;
        *)
            die "unsupported terminal kind: $TERMINAL_KIND"
            ;;
    esac
}

outer_client_count() {
    hyprctl clients -j | jq '
        map(select(.class == "aquamarine" and (.title | test("^aquamarine - WAYLAND-[0-9]+$"))))
        | length
    '
}

wait_for_outer_output_window_count() {
    local expected="$1"

    for ((attempt = 0; attempt < 60; ++attempt)); do
        if (( "$(outer_client_count)" >= expected )); then
            return 0
        fi

        sleep 0.25
    done

    return 1
}

collect_outer_windows_json() {
    hyprctl clients -j | jq '
        map(select(.class == "aquamarine" and (.title | test("^aquamarine - WAYLAND-[1-5]$"))))
        | sort_by(.title)
    '
}

monitor_logical_geometry() {
    local monitor_json="$1"
    local -n out_x_ref="$2"
    local -n out_y_ref="$3"
    local -n out_width_ref="$4"
    local -n out_height_ref="$5"

    out_x_ref="$(printf '%s\n' "$monitor_json" | jq -r '.x')"
    out_y_ref="$(printf '%s\n' "$monitor_json" | jq -r '.y')"
    out_width_ref="$(printf '%s\n' "$monitor_json" | jq -r '
        if (.transform % 2) == 1 then .height else .width end
    ')"
    out_height_ref="$(printf '%s\n' "$monitor_json" | jq -r '
        if (.transform % 2) == 1 then .width else .height end
    ')"
}

compute_outer_output_layout() {
    local outer_monitor_json
    outer_monitor_json="$(hyprctl monitors -j | jq -c --arg monitor "$OUTER_MONITOR" '
        map(select(.name == $monitor)) | .[0] // empty
    ')"
    [[ -n "$outer_monitor_json" ]] || die "could not resolve outer monitor geometry for $OUTER_MONITOR"

    monitor_logical_geometry \
        "$outer_monitor_json" \
        OUTER_MONITOR_X \
        OUTER_MONITOR_Y \
        OUTER_MONITOR_WIDTH \
        OUTER_MONITOR_HEIGHT

    local portrait_width="$WINDOW_HEIGHT"
    local portrait_height="$WINDOW_WIDTH"
    local landscape_width="$WINDOW_WIDTH"
    local landscape_height="$WINDOW_HEIGHT"
    local layout_margin=96
    local layout_gap=64
    local layout_scale_percent=100
    local available_width=$(( OUTER_MONITOR_WIDTH - (2 * layout_margin) ))
    local available_height=$(( OUTER_MONITOR_HEIGHT - (2 * layout_margin) - layout_gap ))
    local center_landscape_y=$(( landscape_height + layout_gap + ((portrait_height - landscape_height) / 2) ))
    local total_base_width=$(( portrait_width + layout_gap + landscape_width + layout_gap + portrait_width ))
    local total_base_height=$(( landscape_height + layout_gap + portrait_height + layout_gap + landscape_height ))
    local base_widths=(
        "$portrait_width"
        "$landscape_width"
        "$landscape_width"
        "$portrait_width"
        "$landscape_width"
    )
    local base_heights=(
        "$portrait_height"
        "$landscape_height"
        "$landscape_height"
        "$portrait_height"
        "$landscape_height"
    )
    local base_xs=(
        0
        "$(( portrait_width + layout_gap ))"
        "$(( portrait_width + layout_gap ))"
        "$(( portrait_width + layout_gap + landscape_width + layout_gap ))"
        "$(( portrait_width + layout_gap ))"
    )
    local base_ys=(
        "$(( landscape_height + layout_gap ))"
        0
        "$center_landscape_y"
        "$(( landscape_height + layout_gap ))"
        "$(( landscape_height + layout_gap + portrait_height + layout_gap ))"
    )

    (( available_width > 0 && available_height > 0 )) || die "computed an invalid outer nested output size"

    local scale_num="$available_width"
    local scale_den="$total_base_width"
    if (( available_height * scale_den < scale_num * total_base_height )); then
        scale_num="$available_height"
        scale_den="$total_base_height"
    fi

    scale_num=$(( scale_num * layout_scale_percent ))
    scale_den=$(( scale_den * 100 ))

    local total_scaled_width=$(( (total_base_width * scale_num) / scale_den ))
    local total_scaled_height=$(( (total_base_height * scale_num) / scale_den ))
    local outer_origin_x=$(( (OUTER_MONITOR_WIDTH - total_scaled_width) / 2 ))
    local outer_origin_y=$(( (OUTER_MONITOR_HEIGHT - total_scaled_height) / 2 ))
    local index

    OUTER_OUTPUT_XS=()
    OUTER_OUTPUT_YS=()
    OUTER_OUTPUT_WIDTHS=()
    OUTER_OUTPUT_HEIGHTS=()

    for index in "${!OUTER_OUTPUT_TITLES[@]}"; do
        OUTER_OUTPUT_WIDTHS[$index]=$(( (base_widths[$index] * scale_num) / scale_den ))
        OUTER_OUTPUT_HEIGHTS[$index]=$(( (base_heights[$index] * scale_num) / scale_den ))
        OUTER_OUTPUT_XS[$index]=$(( outer_origin_x + ((base_xs[$index] * scale_num) / scale_den) ))
        OUTER_OUTPUT_YS[$index]=$(( outer_origin_y + ((base_ys[$index] * scale_num) / scale_den) ))

        (( OUTER_OUTPUT_WIDTHS[$index] > 0 && OUTER_OUTPUT_HEIGHTS[$index] > 0 )) \
            || die "computed an invalid outer nested output size for ${OUTER_OUTPUT_TITLES[$index]}"
    done
}

enable_outer_output_rules() {
    compute_outer_output_layout
}

position_outer_output_window() {
    local title="$1"
    local width="$2"
    local height="$3"
    local x="$4"
    local y="$5"

    hyprctl dispatch setfloating "title:^${title}$" >/dev/null
    hyprctl dispatch resizewindowpixel "exact ${width} ${height},title:^${title}$" >/dev/null
    hyprctl dispatch movewindowpixel "exact ${x} ${y},title:^${title}$" >/dev/null
}

apply_five_monitor_cross_layout() {
    local landscape_width="$1"
    local landscape_height="$2"
    local portrait_width="$landscape_height"
    local portrait_height="$landscape_width"
    local row_gap=$(( landscape_height / 4 ))
    local middle_x="$portrait_width"
    local middle_y=$(( landscape_height + row_gap ))
    local side_y=$(( middle_y + (landscape_height / 2) - (portrait_height / 2) ))
    local right_x=$(( portrait_width + landscape_width ))
    local bottom_y=$(( middle_y + landscape_height + row_gap ))

    hyprctl -i "$NESTED_INSTANCE" keyword monitor \
        "WAYLAND-1,${landscape_width}x${landscape_height}@60,0x${side_y},1,transform,3" >/dev/null
    hyprctl -i "$NESTED_INSTANCE" keyword monitor \
        "WAYLAND-2,${landscape_width}x${landscape_height}@60,${middle_x}x0,1,transform,0" >/dev/null
    hyprctl -i "$NESTED_INSTANCE" keyword monitor \
        "WAYLAND-3,${landscape_width}x${landscape_height}@60,${middle_x}x${middle_y},1,transform,0" >/dev/null
    hyprctl -i "$NESTED_INSTANCE" keyword monitor \
        "WAYLAND-4,${landscape_width}x${landscape_height}@60,${right_x}x${side_y},1,transform,1" >/dev/null
    hyprctl -i "$NESTED_INSTANCE" keyword monitor \
        "WAYLAND-5,${landscape_width}x${landscape_height}@60,${middle_x}x${bottom_y},1,transform,0" >/dev/null
}

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLUGIN_PATH="$REPO_ROOT/Debug/hyprscroller.so"
WINDOW_WIDTH=1800
WINDOW_HEIGHT=1200
KEEP_OPEN=0
OVERVIEW_HOLD_SECONDS=1
OUTER_MONITOR=""
NESTED_INSTANCE=""
NESTED_MONITOR_NAMES=(
    "WAYLAND-1"
    "WAYLAND-2"
    "WAYLAND-3"
    "WAYLAND-4"
    "WAYLAND-5"
)
OUTER_OUTPUT_TITLES=(
    "aquamarine - WAYLAND-1"
    "aquamarine - WAYLAND-2"
    "aquamarine - WAYLAND-3"
    "aquamarine - WAYLAND-4"
    "aquamarine - WAYLAND-5"
)
SOURCE_CLASS="hs-canvas-source"
TARGET_CLASS="hs-canvas-target"
OUTER_MONITOR_X=0
OUTER_MONITOR_Y=0
OUTER_MONITOR_WIDTH=0
OUTER_MONITOR_HEIGHT=0
OUTER_OUTPUT_XS=()
OUTER_OUTPUT_YS=()
OUTER_OUTPUT_WIDTHS=()
OUTER_OUTPUT_HEIGHTS=()

cleanup() {
    if [[ "${KEEP_OPEN:-0}" -eq 1 ]]; then
        return
    fi

    if [[ -n "${NESTED_INSTANCE:-}" ]]; then
        hyprctl -i "$NESTED_INSTANCE" dispatch exit >/dev/null 2>&1 || true
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
        --hold-seconds)
            [[ $# -ge 2 ]] || die "--hold-seconds requires an integer value"
            OVERVIEW_HOLD_SECONDS="$2"
            [[ "$OVERVIEW_HOLD_SECONDS" =~ ^[0-9]+$ ]] || die "invalid --hold-seconds: $2"
            shift 2
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

require_cmd hyprctl
require_cmd jq
require_cmd Hyprland

[[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]] || die "run this inside an existing Hyprland session"
[[ -f "$PLUGIN_PATH" ]] || die "plugin not found: $PLUGIN_PATH (run 'cmake --build ./Debug -j' first)"

pick_terminal_kind

if [[ -z "$OUTER_MONITOR" ]]; then
    OUTER_MONITOR="$(detect_outer_monitor)"
    [[ -n "$OUTER_MONITOR" ]] || die "could not auto-detect an outer monitor"
fi

enable_outer_output_rules

RUN_DIR="$(mktemp -d /tmp/hyprscroller-overview-canvas.XXXXXX)"
CONFIG_PATH="$RUN_DIR/hyprland.conf"
LOG_PATH="$RUN_DIR/hyprland.log"
LAUNCHER_PATH="$RUN_DIR/launch-nested.sh"
RESULT_PATH="$RUN_DIR/result.json"

cat >"$CONFIG_PATH" <<EOF
monitor = , preferred, auto, 1

plugin = $PLUGIN_PATH

env = XCURSOR_SIZE,24

windowrulev2 = float,class:^($SOURCE_CLASS|$TARGET_CLASS)$
windowrulev2 = center,class:^($SOURCE_CLASS|$TARGET_CLASS)$

general {
    layout = scroller
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

debug {
    disable_logs = false
}
EOF

cat >"$LAUNCHER_PATH" <<EOF
#!/usr/bin/env bash
exec Hyprland -c "$CONFIG_PATH" >"$LOG_PATH" 2>&1
EOF
chmod +x "$LAUNCHER_PATH"

BEFORE_MAX_TIME="$(hyprctl instances -j | jq '[.[].time] | max // 0')"
LAUNCH_RULES="[monitor $OUTER_MONITOR; float]"
hyprctl dispatch exec "$LAUNCH_RULES $LAUNCHER_PATH" >/dev/null

for ((attempt = 0; attempt < 80; ++attempt)); do
    NESTED_INSTANCE="$(hyprctl instances -j | jq -r --argjson before "$BEFORE_MAX_TIME" '
        (map(select(.time > $before))) as $instances
        | if ($instances | length) == 0 then
            empty
          else
            ($instances | max_by(.time).instance)
          end
    ')"

    if [[ -n "$NESTED_INSTANCE" ]]; then
        break
    fi

    sleep 0.25
done

[[ -n "$NESTED_INSTANCE" ]] || die "timed out waiting for nested Hyprland instance"

NESTED_SOCKET="$(hyprctl instances -j | jq -r --arg instance "$NESTED_INSTANCE" '
    map(select(.instance == $instance)) | .[0].wl_socket // empty
')"
[[ -n "$NESTED_SOCKET" ]] || die "could not resolve nested Wayland socket"

wait_for_outer_output_window_count 1 || die "timed out waiting for the first outer nested output window"
for expected_monitor_count in 2 3 4 5; do
    hyprctl -i "$NESTED_INSTANCE" output create wayland >/dev/null
    wait_for_nested_monitor_count "$expected_monitor_count" || die "timed out waiting for nested monitor count $expected_monitor_count"
    wait_for_outer_output_window_count "$expected_monitor_count" || die "timed out waiting for outer nested output window count $expected_monitor_count"
done

for index in "${!OUTER_OUTPUT_TITLES[@]}"; do
    position_outer_output_window \
        "${OUTER_OUTPUT_TITLES[$index]}" \
        "${OUTER_OUTPUT_WIDTHS[$index]}" \
        "${OUTER_OUTPUT_HEIGHTS[$index]}" \
        "$(( OUTER_MONITOR_X + OUTER_OUTPUT_XS[$index] ))" \
        "$(( OUTER_MONITOR_Y + OUTER_OUTPUT_YS[$index] ))"
done
sleep 0.5

apply_five_monitor_cross_layout "$WINDOW_WIDTH" "$WINDOW_HEIGHT"
sleep 0.5

for monitor_name in "${NESTED_MONITOR_NAMES[@]}"; do
    hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -e --arg monitor "$monitor_name" 'any(.[]; .name == $monitor)' >/dev/null \
        || die "could not resolve nested monitor $monitor_name"
done

SOURCE_MONITOR="WAYLAND-1"
TARGET_MONITOR="WAYLAND-3"

hyprctl -i "$NESTED_INSTANCE" dispatch focusmonitor "$SOURCE_MONITOR" >/dev/null
launch_terminal_window "$SOURCE_CLASS" "canvas-source"
wait_for_nested_client_count 1 || die "timed out waiting for source window"
ensure_window_floating "$SOURCE_CLASS"

hyprctl -i "$NESTED_INSTANCE" dispatch focusmonitor "$TARGET_MONITOR" >/dev/null
launch_terminal_window "$TARGET_CLASS" "canvas-target"
wait_for_nested_client_count 2 || die "timed out waiting for target window"
ensure_window_floating "$TARGET_CLASS"

sleep 1
INITIAL_MONITORS_JSON="$(hyprctl -i "$NESTED_INSTANCE" monitors -j)"
INITIAL_SOURCE_WORKSPACE="$(printf '%s\n' "$INITIAL_MONITORS_JSON" | jq -r --arg monitor "$SOURCE_MONITOR" '.[] | select(.name == $monitor) | .activeWorkspace.id')"
INITIAL_TARGET_WORKSPACE="$(printf '%s\n' "$INITIAL_MONITORS_JSON" | jq -r --arg monitor "$TARGET_MONITOR" '.[] | select(.name == $monitor) | .activeWorkspace.id')"
INITIAL_SOURCE_TRANSFORM="$(printf '%s\n' "$INITIAL_MONITORS_JSON" | jq -r --arg monitor "$SOURCE_MONITOR" '.[] | select(.name == $monitor) | .transform')"
INITIAL_TARGET_TRANSFORM="$(printf '%s\n' "$INITIAL_MONITORS_JSON" | jq -r --arg monitor "$TARGET_MONITOR" '.[] | select(.name == $monitor) | .transform')"

hyprctl -i "$NESTED_INSTANCE" dispatch focusmonitor "$SOURCE_MONITOR" >/dev/null
focus_nested_window "class:$SOURCE_CLASS"
sleep 0.3
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:focusmonitor r >/dev/null
wait_for_nested_focused_monitor "$TARGET_MONITOR" || true
NORMAL_FOCUSED_MONITOR="$(nested_focused_monitor)"
NORMAL_FOCUS_OK=0
if [[ "$NORMAL_FOCUSED_MONITOR" == "$TARGET_MONITOR" ]]; then
    NORMAL_FOCUS_OK=1
fi

hyprctl -i "$NESTED_INSTANCE" dispatch scroller:focusmonitor l >/dev/null
wait_for_nested_focused_monitor "$SOURCE_MONITOR" || true
hyprctl -i "$NESTED_INSTANCE" dispatch focusmonitor "$SOURCE_MONITOR" >/dev/null
focus_nested_window "class:$SOURCE_CLASS"
sleep 0.3
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview >/dev/null
sleep "$OVERVIEW_HOLD_SECONDS"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:focusmonitor r >/dev/null
sleep 0.4
DURING_CREATE_PREVIEW_MONITORS_JSON="$(hyprctl -i "$NESTED_INSTANCE" monitors -j)"
DURING_CREATE_SOURCE_WORKSPACE="$(printf '%s\n' "$DURING_CREATE_PREVIEW_MONITORS_JSON" | jq -r --arg monitor "$SOURCE_MONITOR" '.[] | select(.name == $monitor) | .activeWorkspace.id')"
DURING_CREATE_TARGET_WORKSPACE="$(printf '%s\n' "$DURING_CREATE_PREVIEW_MONITORS_JSON" | jq -r --arg monitor "$TARGET_MONITOR" '.[] | select(.name == $monitor) | .activeWorkspace.id')"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview >/dev/null
sleep 0.7

AFTER_CREATE_MONITORS_JSON="$(hyprctl -i "$NESTED_INSTANCE" monitors -j)"
AFTER_CREATE_SOURCE_WORKSPACE="$(printf '%s\n' "$AFTER_CREATE_MONITORS_JSON" | jq -r --arg monitor "$SOURCE_MONITOR" '.[] | select(.name == $monitor) | .activeWorkspace.id')"
AFTER_CREATE_TARGET_WORKSPACE="$(printf '%s\n' "$AFTER_CREATE_MONITORS_JSON" | jq -r --arg monitor "$TARGET_MONITOR" '.[] | select(.name == $monitor) | .activeWorkspace.id')"

hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview >/dev/null
sleep "$OVERVIEW_HOLD_SECONDS"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:focusmonitor l >/dev/null
sleep 0.4
DURING_RETURN_PREVIEW_MONITORS_JSON="$(hyprctl -i "$NESTED_INSTANCE" monitors -j)"
DURING_RETURN_SOURCE_WORKSPACE="$(printf '%s\n' "$DURING_RETURN_PREVIEW_MONITORS_JSON" | jq -r --arg monitor "$SOURCE_MONITOR" '.[] | select(.name == $monitor) | .activeWorkspace.id')"
DURING_RETURN_TARGET_WORKSPACE="$(printf '%s\n' "$DURING_RETURN_PREVIEW_MONITORS_JSON" | jq -r --arg monitor "$TARGET_MONITOR" '.[] | select(.name == $monitor) | .activeWorkspace.id')"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview >/dev/null
sleep 0.7

RUNTIME_BASE_DIR="${XDG_RUNTIME_DIR:-/tmp}"
RUNTIME_STATE="$RUNTIME_BASE_DIR/hyprscroller-canvas-$NESTED_INSTANCE.state"
RUNTIME_STATE_COPY="$RUN_DIR/runtime-state.txt"
if [[ -f "$RUNTIME_STATE" ]]; then
    cp "$RUNTIME_STATE" "$RUNTIME_STATE_COPY"
else
    : >"$RUNTIME_STATE_COPY"
fi

OUTER_WINDOWS_JSON="$(collect_outer_windows_json)"
[[ "$(printf '%s\n' "$OUTER_WINDOWS_JSON" | jq 'length')" -eq 5 ]] || die "could not resolve all outer nested output window state"

jq -n \
  --argjson initialMonitors "$INITIAL_MONITORS_JSON" \
  --argjson duringCreatePreviewMonitors "$DURING_CREATE_PREVIEW_MONITORS_JSON" \
  --argjson afterCreateMonitors "$AFTER_CREATE_MONITORS_JSON" \
  --argjson duringReturnPreviewMonitors "$DURING_RETURN_PREVIEW_MONITORS_JSON" \
  --argjson monitors "$(hyprctl -i "$NESTED_INSTANCE" monitors -j)" \
  --argjson workspaces "$(hyprctl -i "$NESTED_INSTANCE" workspaces -j)" \
  --argjson clients "$(hyprctl -i "$NESTED_INSTANCE" clients -j)" \
  --argjson outerWindows "$OUTER_WINDOWS_JSON" \
  --arg normalFocusedMonitor "$NORMAL_FOCUSED_MONITOR" \
  --argjson normalFocusOk "$NORMAL_FOCUS_OK" \
  --argjson sourceFloating "$( [[ "$(client_is_floating_by_class "$SOURCE_CLASS")" == "true" ]] && printf 'true' || printf 'false' )" \
  --argjson targetFloating "$( [[ "$(client_is_floating_by_class "$TARGET_CLASS")" == "true" ]] && printf 'true' || printf 'false' )" \
  --arg runtimeStatePath "$RUNTIME_STATE_COPY" \
  '{
      initialMonitors: $initialMonitors,
      duringCreatePreviewMonitors: $duringCreatePreviewMonitors,
      afterCreateMonitors: $afterCreateMonitors,
      duringReturnPreviewMonitors: $duringReturnPreviewMonitors,
      monitors: $monitors,
      workspaces: $workspaces,
      clients: $clients,
      outerWindows: $outerWindows,
      normalFocusedMonitor: $normalFocusedMonitor,
      normalFocusOk: ($normalFocusOk == 1),
      sourceFloating: $sourceFloating,
      targetFloating: $targetFloating,
      runtimeStatePath: $runtimeStatePath
  }' >"$RESULT_PATH"

if [[ "$KEEP_OPEN" -eq 1 ]]; then
    :
else
    hyprctl -i "$NESTED_INSTANCE" dispatch exit >/dev/null 2>&1 || true
    wait_for_instance_exit || die "timed out waiting for nested Hyprland to exit"
fi

[[ -f "$RESULT_PATH" ]] || die "nested scenario did not write a result file"

FINAL_SOURCE_WORKSPACE="$(jq -r --arg monitor "$SOURCE_MONITOR" '.monitors[] | select(.name == $monitor) | .activeWorkspace.id' "$RESULT_PATH")"
FINAL_TARGET_WORKSPACE="$(jq -r --arg monitor "$TARGET_MONITOR" '.monitors[] | select(.name == $monitor) | .activeWorkspace.id' "$RESULT_PATH")"
RETURNED_SOURCE_CLIENTS="$(jq -r --argjson ws "$FINAL_SOURCE_WORKSPACE" '[.clients[] | select(.workspace.id == $ws)] | length' "$RESULT_PATH")"
RETURNED_TARGET_CLIENTS="$(jq -r --argjson ws "$FINAL_TARGET_WORKSPACE" '[.clients[] | select(.workspace.id == $ws)] | length' "$RESULT_PATH")"
CLIENTS_ON_FINAL_SOURCE="$(jq -r --argjson ws "$FINAL_SOURCE_WORKSPACE" '[.clients[] | select(.workspace.id == $ws)] | length' "$RESULT_PATH")"
CLIENTS_ON_FINAL_TARGET="$(jq -r --argjson ws "$FINAL_TARGET_WORKSPACE" '[.clients[] | select(.workspace.id == $ws)] | length' "$RESULT_PATH")"
SOURCE_FLOATING="$(jq -r '.sourceFloating' "$RESULT_PATH")"
TARGET_FLOATING="$(jq -r '.targetFloating' "$RESULT_PATH")"
SOURCE_CLIENT_SIZE="$(jq -r '.clients[] | select(.class == "hs-canvas-source") | (.size | @json)' "$RESULT_PATH")"
TARGET_CLIENT_SIZE="$(jq -r '.clients[] | select(.class == "hs-canvas-target") | (.size | @json)' "$RESULT_PATH")"
INITIAL_MONITOR_COUNT="$(jq -r '.initialMonitors | length' "$RESULT_PATH")"
ALL_INITIAL_TRANSFORMS_OK="$(jq -r '
    ((.initialMonitors[] | select(.name == "WAYLAND-1") | .transform) == 3) and
    ((.initialMonitors[] | select(.name == "WAYLAND-2") | .transform) == 0) and
    ((.initialMonitors[] | select(.name == "WAYLAND-3") | .transform) == 0) and
    ((.initialMonitors[] | select(.name == "WAYLAND-4") | .transform) == 1) and
    ((.initialMonitors[] | select(.name == "WAYLAND-5") | .transform) == 0)
' "$RESULT_PATH")"
PREVIEW_CREATE_UNCHANGED_ALL="$(jq -r '
    [.initialMonitors[] as $m |
        ((.duringCreatePreviewMonitors[] | select(.name == $m.name) | .activeWorkspace.id) == $m.activeWorkspace.id)
    ] | all
' "$RESULT_PATH")"
AFTER_CREATE_CHANGED_ALL="$(jq -r '
    [.initialMonitors[] as $m |
        ((.afterCreateMonitors[] | select(.name == $m.name) | .activeWorkspace.id) != $m.activeWorkspace.id)
    ] | all
' "$RESULT_PATH")"
PREVIEW_RETURN_MATCHES_CREATE_ALL="$(jq -r '
    [.initialMonitors[] as $m |
        ((.duringReturnPreviewMonitors[] | select(.name == $m.name) | .activeWorkspace.id) ==
         (.afterCreateMonitors[] | select(.name == $m.name) | .activeWorkspace.id))
    ] | all
' "$RESULT_PATH")"
FINAL_RETURNED_ALL="$(jq -r '
    [.initialMonitors[] as $m |
        ((.monitors[] | select(.name == $m.name) | .activeWorkspace.id) == $m.activeWorkspace.id)
    ] | all
' "$RESULT_PATH")"
OUTER_WINDOW_LAYOUT_OK=1
CANVAS_COUNT="$(grep -c '^CANVAS ' "$RUNTIME_STATE_COPY" || true)"

printf 'nested instance:         %s\n' "$NESTED_INSTANCE"
printf 'nested socket:           %s\n' "$NESTED_SOCKET"
printf 'outer monitor:           %s\n' "$OUTER_MONITOR"
printf 'source monitor:          %s\n' "$SOURCE_MONITOR"
printf 'target monitor:          %s\n' "$TARGET_MONITOR"
printf 'terminal app:            %s\n' "$TERMINAL_KIND"
printf 'run dir:                 %s\n' "$RUN_DIR"
printf 'config:                  %s\n' "$CONFIG_PATH"
printf 'log:                     %s\n' "$LOG_PATH"
printf 'result file:             %s\n' "$RESULT_PATH"
printf 'runtime state:           %s\n' "$RUNTIME_STATE_COPY"
printf 'normal focus landed on:  %s\n' "${NORMAL_FOCUSED_MONITOR:-unknown}"
printf 'normal focus ok:         %s\n' "${NORMAL_FOCUS_OK:-0}"
printf 'initial monitor count:   %s\n' "${INITIAL_MONITOR_COUNT:-unknown}"
printf 'initial source ws:       %s\n' "${INITIAL_SOURCE_WORKSPACE:-unknown}"
printf 'initial target ws:       %s\n' "${INITIAL_TARGET_WORKSPACE:-unknown}"
printf 'initial source transform:%s\n' "${INITIAL_SOURCE_TRANSFORM:-unknown}"
printf 'initial target transform:%s\n' "${INITIAL_TARGET_TRANSFORM:-unknown}"
printf 'preview create source:   %s\n' "${DURING_CREATE_SOURCE_WORKSPACE:-unknown}"
printf 'preview create target:   %s\n' "${DURING_CREATE_TARGET_WORKSPACE:-unknown}"
printf 'after create source ws:  %s\n' "${AFTER_CREATE_SOURCE_WORKSPACE:-unknown}"
printf 'after create target ws:  %s\n' "${AFTER_CREATE_TARGET_WORKSPACE:-unknown}"
printf 'preview return source:   %s\n' "${DURING_RETURN_SOURCE_WORKSPACE:-unknown}"
printf 'preview return target:   %s\n' "${DURING_RETURN_TARGET_WORKSPACE:-unknown}"
printf 'returned source ws:      %s\n' "${FINAL_SOURCE_WORKSPACE:-unknown}"
printf 'returned target ws:      %s\n' "${FINAL_TARGET_WORKSPACE:-unknown}"
printf 'returned source clients: %s\n' "${RETURNED_SOURCE_CLIENTS:-unknown}"
printf 'returned target clients: %s\n' "${RETURNED_TARGET_CLIENTS:-unknown}"
printf 'source floating:         %s\n' "${SOURCE_FLOATING:-unknown}"
printf 'target floating:         %s\n' "${TARGET_FLOATING:-unknown}"
printf 'source client size:      %s\n' "${SOURCE_CLIENT_SIZE:-unknown}"
printf 'target client size:      %s\n' "${TARGET_CLIENT_SIZE:-unknown}"
printf 'initial transforms ok:   %s\n' "${ALL_INITIAL_TRANSFORMS_OK:-unknown}"
printf 'preview create stable:   %s\n' "${PREVIEW_CREATE_UNCHANGED_ALL:-unknown}"
printf 'after create changed:    %s\n' "${AFTER_CREATE_CHANGED_ALL:-unknown}"
printf 'preview return matches:  %s\n' "${PREVIEW_RETURN_MATCHES_CREATE_ALL:-unknown}"
printf 'final returned all:      %s\n' "${FINAL_RETURNED_ALL:-unknown}"
printf 'canvas count:            %s\n' "${CANVAS_COUNT:-0}"

for index in "${!OUTER_OUTPUT_TITLES[@]}"; do
    title="${OUTER_OUTPUT_TITLES[$index]}"
    actual_floating="$(jq -r --arg title "$title" '.outerWindows[] | select(.title == $title) | .floating' "$RESULT_PATH")"
    actual_size="$(jq -r --arg title "$title" '.outerWindows[] | select(.title == $title) | (.size | @json)' "$RESULT_PATH")"
    actual_at="$(jq -r --arg title "$title" '.outerWindows[] | select(.title == $title) | (.at | @json)' "$RESULT_PATH")"
    expected_size="[${OUTER_OUTPUT_WIDTHS[$index]},${OUTER_OUTPUT_HEIGHTS[$index]}]"
    expected_at="[$(( OUTER_MONITOR_X + OUTER_OUTPUT_XS[$index] )),$(( OUTER_MONITOR_Y + OUTER_OUTPUT_YS[$index] ))]"

    printf '%s floating: %s\n' "$title" "${actual_floating:-unknown}"
    printf '%s size:     %s (expected %s)\n' "$title" "${actual_size:-unknown}" "$expected_size"
    printf '%s at:       %s (expected %s)\n' "$title" "${actual_at:-unknown}" "$expected_at"

    if [[ "$actual_floating" != "true" || "$actual_size" != "$expected_size" || "$actual_at" != "$expected_at" ]]; then
        OUTER_WINDOW_LAYOUT_OK=0
    fi
done

if [[ "$INITIAL_MONITOR_COUNT" == "5" \
   && "$ALL_INITIAL_TRANSFORMS_OK" == "true" \
   && "$PREVIEW_CREATE_UNCHANGED_ALL" == "true" \
   && "$AFTER_CREATE_CHANGED_ALL" == "true" \
   && "$PREVIEW_RETURN_MATCHES_CREATE_ALL" == "true" \
   && "$FINAL_RETURNED_ALL" == "true" \
   && "$SOURCE_FLOATING" == "true" \
   && "$TARGET_FLOATING" == "true" \
   && "$OUTER_WINDOW_LAYOUT_OK" == "1" \
   && "$CLIENTS_ON_FINAL_SOURCE" == "1" \
   && "$CLIENTS_ON_FINAL_TARGET" == "1" \
   && "$FINAL_SOURCE_WORKSPACE" == "$INITIAL_SOURCE_WORKSPACE" \
   && "$FINAL_TARGET_WORKSPACE" == "$INITIAL_TARGET_WORKSPACE" \
   && "$CANVAS_COUNT" -ge 2 ]]; then
    printf 'result:                  canvas workspace create/return flow passed\n'
    exit 0
fi

printf 'result:                  canvas workspace flow failed\n' >&2
exit 1
