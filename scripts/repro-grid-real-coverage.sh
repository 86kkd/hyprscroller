#!/usr/bin/env bash

# Launch a nested Hyprland session and exercise scrollergrid against real
# compositor surfaces: Waybar reserved area, a portrait monitor, managed
# fullscreen/fitsize, and overview.

set -euo pipefail

die() {
    printf 'error: %s\n' "$*" >&2
    if [[ -n "${RUN_DIR:-}" && -n "${NESTED_INSTANCE:-}" ]]; then
        hyprctl -i "$NESTED_INSTANCE" monitors -j >"$RUN_DIR/failure-monitors.json" 2>/dev/null || true
        hyprctl -i "$NESTED_INSTANCE" clients -j >"$RUN_DIR/failure-clients.json" 2>/dev/null || true
        hyprctl -i "$NESTED_INSTANCE" activewindow -j >"$RUN_DIR/failure-activewindow.json" 2>/dev/null || true
        printf 'failure state: %s\n' "$RUN_DIR" >&2
    fi
    exit 1
}

usage() {
    cat <<'EOF'
Usage: scripts/repro-grid-real-coverage.sh [options]

Options:
  --outer-monitor NAME   Place the floating nested Hyprland window on NAME.
  --window-size WxH      Outer floating window size. Default: 1800x1400.
  --plugin PATH    Plugin .so to load. Default: ./Debug/hyprscroller.so.
  --keep-open      Leave the nested Hyprland instance running after setup.
  -h, --help       Show this help text.

The script exits non-zero when scrollergrid fails a real nested Hyprland check:
Waybar workarea reservation, portrait monitor geometry, gaps_in, fullscreen/fitsize,
or overview.
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
        (map(select((.transform % 2) == 1)) | .[0].name) //
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
    command=$(quote_command "$@")
    hyprctl -i "$NESTED_INSTANCE" dispatch exec "$command" >/dev/null
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
        if (( "$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq 'length')" >= expected )); then
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

monitor_reserved_max() {
    local monitor="$1"
    hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r --arg monitor "$monitor" '
        (.[] | select(.name == $monitor) | .reserved) as $reserved
        | if ($reserved | type) == "array" then
            ($reserved | max // 0)
          elif ($reserved | type) == "object" then
            ([$reserved[]] | max // 0)
          else
            0
          end
    '
}

wait_for_waybar_reserved() {
    local monitor="$1"
    local minimum="$2"
    local reserved

    for ((attempt = 0; attempt < 100; ++attempt)); do
        reserved="$(monitor_reserved_max "$monitor")"
        if (( reserved >= minimum )); then
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
    local body="printf '%s\\n' '$title'; exec bash"

    case "$kind" in
        kitty)
            launch_nested_exec kitty --class "$class" --title "$title" bash -lc "$body"
            ;;
        alacritty)
            launch_nested_exec alacritty --class "$class" --title "$title" -e bash -lc "$body"
            ;;
        *)
            die "unsupported terminal kind: $TERMINAL_KIND"
            ;;
    esac
}

launch_terminal_window() {
    local class="$1"
    local title="$2"

    launch_terminal_window_with_kind "$TERMINAL_KIND" "$class" "$title"
}

launch_terminal_window_and_wait() {
    local class="$1"
    local title="$2"
    local fallback

    launch_terminal_window_with_kind "$TERMINAL_KIND" "$class" "$title"
    if wait_for_window_title "$title"; then
        return 0
    fi

    fallback="$(alternate_terminal_kind "$TERMINAL_KIND")"
    if [[ -n "$fallback" ]]; then
        launch_terminal_window_with_kind "$fallback" "$class" "$title"
        wait_for_window_title "$title"
        return
    fi

    return 1
}

focus_monitor_workspace() {
    local monitor="$1"
    local workspace="$2"
    hyprctl -i "$NESTED_INSTANCE" dispatch focusmonitor "$monitor" >/dev/null
    hyprctl -i "$NESTED_INSTANCE" dispatch workspace "$workspace" >/dev/null
    sleep 0.3
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

active_window_title() {
    hyprctl -i "$NESTED_INSTANCE" activewindow -j | jq -r '.title // empty'
}

monitor_json() {
    local monitor="$1"
    hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -c --arg monitor "$monitor" '
        .[] | select(.name == $monitor)
    '
}

monitor_id() {
    local monitor="$1"
    monitor_json "$monitor" | jq -r '.id'
}

monitor_logical_field() {
    local monitor="$1"
    local field="$2"
    monitor_json "$monitor" | jq -r --arg field "$field" '
        if $field == "x" then .x
        elif $field == "y" then .y
        elif $field == "w" then (if (.transform % 2) == 1 then .height else .width end)
        elif $field == "h" then (if (.transform % 2) == 1 then .width else .height end)
        else empty end
    '
}

monitor_coordinate_width() {
    local monitor="$1"
    monitor_json "$monitor" | jq -r '
        if (.transform % 2) == 1 then .height else .width end
    '
}

assert_ok() {
    local label="$1"
    printf '%s: OK\n' "$label" | tee -a "$SUMMARY_PATH"
}

assert_monitor_portrait() {
    local monitor="$1"
    local label="$2"
    local width height transform

    width="$(monitor_logical_field "$monitor" w)"
    height="$(monitor_logical_field "$monitor" h)"
    transform="$(monitor_json "$monitor" | jq -r '.transform')"

    (( width < height )) || die "$label: expected portrait logical geometry, got ${width}x${height}"
    (( transform % 2 == 1 )) || die "$label: expected a rotated monitor transform, got $transform"
    assert_ok "$label"
}

assert_any_title_respects_workarea() {
    local titles_json="$1"
    local monitor="$2"
    local label="$3"
    local reserved mon_y mon_id

    reserved="$(monitor_reserved_max "$monitor")"
    mon_y="$(monitor_logical_field "$monitor" y)"
    mon_id="$(monitor_id "$monitor")"

    hyprctl -i "$NESTED_INSTANCE" clients -j | jq -e \
      --argjson titles "$titles_json" \
      --argjson monY "$mon_y" \
      --argjson monId "$mon_id" \
      --argjson reserved "$reserved" '
        [
          .[]
          | select(.title as $title | $titles | index($title))
          | select(.monitor == $monId)
          | select(.at[1] >= ($monY + $reserved - 2))
        ]
        | length > 0
    ' >/dev/null || die "$label: no grid window on the portrait monitor sits below the reserved strip"

    assert_ok "$label"
}

assert_titles_avoid_top_reserved_strip() {
    local monitor="$1"
    local titles_json="$2"
    local label="$3"
    local reserved mon_y mon_id

    reserved="$(monitor_reserved_max "$monitor")"
    mon_y="$(monitor_logical_field "$monitor" y)"
    mon_id="$(monitor_id "$monitor")"

    hyprctl -i "$NESTED_INSTANCE" clients -j | jq -e \
      --argjson titles "$titles_json" \
      --argjson monY "$mon_y" \
      --argjson monId "$mon_id" \
      --argjson reserved "$reserved" '
        [
          .[]
          | select(.title as $title | $titles | index($title))
          | select(.monitor == $monId)
          | select((.at[1] < ($monY + $reserved)) and ((.at[1] + .size[1]) > $monY))
        ]
        | length == 0
    ' >/dev/null || die "$label: a grid window overlaps the Waybar reserved strip"

    assert_ok "$label"
}

assert_any_title_offscreen() {
    local monitor="$1"
    local titles_json="$2"
    local label="$3"
    local mon_x mon_y mon_w mon_h

    mon_x="$(monitor_logical_field "$monitor" x)"
    mon_y="$(monitor_logical_field "$monitor" y)"
    mon_w="$(monitor_logical_field "$monitor" w)"
    mon_h="$(monitor_logical_field "$monitor" h)"

    hyprctl -i "$NESTED_INSTANCE" clients -j | jq -e \
      --argjson titles "$titles_json" \
      --argjson monX "$mon_x" \
      --argjson monY "$mon_y" \
      --argjson monW "$mon_w" \
      --argjson monH "$mon_h" '
        [
          .[]
          | select(.title as $title | $titles | index($title))
          | select(
              ((.at[0] + .size[0]) <= $monX)
              or (.at[0] >= ($monX + $monW))
              or ((.at[1] + .size[1]) <= $monY)
              or (.at[1] >= ($monY + $monH))
            )
        ]
        | length > 0
    ' >/dev/null || die "$label: expected at least one rendered grid client outside the portrait monitor"

    assert_ok "$label"
}

assert_column_grid_inner_gap() {
    local titles_json="$1"
    local monitor="$2"
    local minimum_gap="$3"
    local label="$4"
    local mon_id

    mon_id="$(monitor_id "$monitor")"

    hyprctl -i "$NESTED_INSTANCE" clients -j | jq -e \
      --argjson titles "$titles_json" \
      --argjson monId "$mon_id" \
      --argjson minimumGap "$minimum_gap" '
        [
          .[]
          | select(.title as $title | $titles | index($title))
          | select(.monitor == $monId)
        ]
        | sort_by(.at[1])
        | if length < 2 then
            false
          else
            (.[1].at[1] - (.[0].at[1] + .[0].size[1])) >= $minimumGap
          end
    ' >/dev/null || die "$label: visible grid windows did not keep gaps_in spacing"

    assert_ok "$label"
}

assert_fullscreen_expands_and_restores() {
    local title="$1"
    local label="$2"
    local before_title fullscreen_title restored_title fit_title

    focus_title "$title" || die "$label: could not focus '$title'"
    before_title="$(active_window_title)"
    [[ -n "$before_title" ]] || die "$label: active window was empty before fullscreen"

    hyprctl -i "$NESTED_INSTANCE" dispatch scroller:togglefullscreen >/dev/null
    sleep 0.6
    fullscreen_title="$(active_window_title)"
    [[ -n "$fullscreen_title" ]] || die "$label: active window was empty after fullscreen"

    hyprctl -i "$NESTED_INSTANCE" dispatch scroller:togglefullscreen >/dev/null
    sleep 0.6
    restored_title="$(active_window_title)"
    [[ -n "$restored_title" ]] || die "$label: active window was empty after fullscreen restore"

    hyprctl -i "$NESTED_INSTANCE" dispatch scroller:fitsize all >/dev/null
    sleep 0.6
    fit_title="$(active_window_title)"
    [[ -n "$fit_title" ]] || die "$label: active window was empty after fitsize all"

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

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLUGIN_PATH="$REPO_ROOT/Debug/hyprscroller.so"
WINDOW_WIDTH=1800
WINDOW_HEIGHT=1400
KEEP_OPEN=0
OUTER_MONITOR=""
NESTED_INSTANCE=""
NESTED_PID=""
TERMINAL_KIND=""
WAYBAR_HEIGHT=48
GRID_TITLES_JSON='["grid-real-a","grid-real-b","grid-real-c"]'
GRID_VISIBLE_GAP_TITLES_JSON='["grid-real-b","grid-real-c"]'
GRID_WITH_XFER_TITLES_JSON='["grid-real-a","grid-real-b","grid-real-c","grid-real-xfer"]'

cleanup() {
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
            [[ $# -ge 2 ]] || die "--window-size requires a value like 1800x1400"
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
require_cmd waybar
require_cmd realpath

[[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]] || die "run this inside an existing Hyprland session"
[[ -f "$PLUGIN_PATH" ]] || die "plugin not found: $PLUGIN_PATH"
PLUGIN_PATH="$(realpath "$PLUGIN_PATH")"

pick_terminal_kind
if [[ -z "$OUTER_MONITOR" ]]; then
    OUTER_MONITOR="$(detect_outer_monitor)"
    [[ -n "$OUTER_MONITOR" ]] || die "could not auto-detect an outer monitor"
fi

RUN_DIR="$(mktemp -d /tmp/hyprscroller-grid-real-coverage.XXXXXX)"
CONFIG_PATH="$RUN_DIR/hyprland.conf"
WAYBAR_CONFIG_PATH="$RUN_DIR/waybar.json"
WAYBAR_STYLE_PATH="$RUN_DIR/waybar.css"
LOG_PATH="$RUN_DIR/hyprland.log"
LAUNCHER_PATH="$RUN_DIR/launch-nested.sh"
SUMMARY_PATH="$RUN_DIR/summary.txt"
RESULT_PATH="$RUN_DIR/result.txt"

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

cat >"$WAYBAR_CONFIG_PATH" <<EOF
{
  "layer": "top",
  "position": "top",
  "height": $WAYBAR_HEIGHT,
  "exclusive": true,
  "passthrough": false,
  "modules-left": ["custom/hyprscroller"],
  "custom/hyprscroller": {
    "format": "hyprscroller grid reserved area"
  }
}
EOF

cat >"$WAYBAR_STYLE_PATH" <<EOF
* {
  border: none;
  border-radius: 0;
  font-family: monospace;
  font-size: 12px;
  min-height: ${WAYBAR_HEIGHT}px;
}

window#waybar {
  background: rgba(18, 18, 18, 0.96);
  color: #f7f7f7;
}
EOF

cat >"$LAUNCHER_PATH" <<EOF
#!/usr/bin/env bash
exec Hyprland -c "$CONFIG_PATH" >"$LOG_PATH" 2>&1
EOF
chmod +x "$LAUNCHER_PATH"

BEFORE_MAX_TIME="$(hyprctl instances -j | jq '[.[].time] | max // 0')"
LAUNCH_RULES="[monitor $OUTER_MONITOR; float; size $WINDOW_WIDTH $WINDOW_HEIGHT; center]"
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

SOURCE_MONITOR="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r 'sort_by(.id) | .[0].name')"
[[ -n "$SOURCE_MONITOR" ]] || die "could not resolve nested monitor name"
wait_for_monitor_usable "$SOURCE_MONITOR" || die "nested monitor never reported non-zero geometry"

hyprctl -i "$NESTED_INSTANCE" keyword monitor "$SOURCE_MONITOR,${WINDOW_WIDTH}x${WINDOW_HEIGHT}@60,0x0,1,transform,1" >/dev/null
wait_for_monitor_usable "$SOURCE_MONITOR" || die "portrait monitor never reported non-zero geometry"
sleep 0.5

assert_monitor_portrait "$SOURCE_MONITOR" "portrait monitor transform"

launch_nested_exec waybar -c "$WAYBAR_CONFIG_PATH" -s "$WAYBAR_STYLE_PATH"
wait_for_waybar_reserved "$SOURCE_MONITOR" $(( WAYBAR_HEIGHT - 8 )) \
    || die "Waybar did not reserve the portrait monitor workarea"
assert_ok "real Waybar reserved area"

focus_monitor_workspace "$SOURCE_MONITOR" 1
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scrollergrid >/dev/null
for title in grid-real-a grid-real-b grid-real-c; do
    launch_terminal_window_and_wait "hs-$title" "$title" || die "$title did not appear"
    sleep 0.25
done
sleep 1

focus_title grid-real-c || die "could not focus grid-real-c"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movefocus u >/dev/null
sleep 0.4
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movefocus d >/dev/null
sleep 0.4

assert_column_grid_inner_gap "$GRID_VISIBLE_GAP_TITLES_JSON" "$SOURCE_MONITOR" 6 "grid honors gaps_in between visible windows"
assert_any_title_respects_workarea "$GRID_TITLES_JSON" "$SOURCE_MONITOR" "visible grid window avoids Waybar"
assert_any_title_offscreen "$SOURCE_MONITOR" "$GRID_TITLES_JSON" "grid offscreen commit exercised"
assert_titles_avoid_top_reserved_strip "$SOURCE_MONITOR" "$GRID_TITLES_JSON" "grid offscreen windows avoid Waybar"
launch_terminal_window_and_wait "hs-grid-real-xfer" "grid-real-xfer" || die "grid-real-xfer did not appear"
sleep 0.8
assert_fullscreen_expands_and_restores "grid-real-xfer" "grid fullscreen and fitsize"

hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview >/dev/null
sleep 1
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movefocus l >/dev/null
sleep 0.3
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview accept >/dev/null
sleep 0.6
[[ -n "$(active_window_title)" ]] || die "grid overview accept left no active window"
assert_all_titles_present "$GRID_WITH_XFER_TITLES_JSON" "grid overview retained windows"
assert_ok "grid overview open and accept"

{
    printf 'nested instance:       %s\n' "$NESTED_INSTANCE"
    printf 'plugin path:           %s\n' "$PLUGIN_PATH"
    printf 'config:                %s\n' "$CONFIG_PATH"
    printf 'waybar config:         %s\n' "$WAYBAR_CONFIG_PATH"
    printf 'waybar style:          %s\n' "$WAYBAR_STYLE_PATH"
    printf 'nested log:            %s\n' "$LOG_PATH"
    printf 'run dir:               %s\n' "$RUN_DIR"
    printf 'outer monitor:         %s\n' "$OUTER_MONITOR"
    printf 'portrait monitor:      %s\n' "$SOURCE_MONITOR"
    printf 'portrait reserved max: %s\n' "$(monitor_reserved_max "$SOURCE_MONITOR")"
} >"$RESULT_PATH"

cat "$RESULT_PATH"
cat "$SUMMARY_PATH"
