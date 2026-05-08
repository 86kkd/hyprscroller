#!/usr/bin/env bash

# Launch a nested Hyprland session and verify directional handoff between the
# experimental scrollergrid layout and the legacy scroller CanvasLayout.

set -euo pipefail

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: scripts/repro-grid-canvas-cross-monitor.sh [options]

Options:
  --plugin PATH    Plugin .so to load. Default: ./Debug/hyprscroller.so.
  --keep-open      Leave the nested Hyprland instance running after setup.
  -h, --help       Show this help text.

The script exits non-zero when grid/Canvas cross-monitor focus or window
movement fails in the nested session.
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

wait_for_window_title() {
    local title="$1"
    for ((attempt = 0; attempt < 80; ++attempt)); do
        if hyprctl -i "$NESTED_INSTANCE" clients -j | jq -e --arg title "$title" 'any(.[]; .title == $title)' >/dev/null; then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

launch_terminal_window() {
    local class="$1"
    local title="$2"
    local body="printf '%s\\n' '$title'; exec bash"

    case "$TERMINAL_KIND" in
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
    sleep 0.3
}

active_window_title() {
    hyprctl -i "$NESTED_INSTANCE" activewindow -j | jq -r '.title // empty'
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

assert_active_title() {
    local expected="$1"
    local label="$2"
    local actual
    actual="$(active_window_title)"
    [[ "$actual" == "$expected" ]] || die "$label: expected active title '$expected', got '$actual'"
    printf '%s: OK\n' "$label" | tee -a "$SUMMARY_PATH"
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
    printf '%s: OK\n' "$label" | tee -a "$SUMMARY_PATH"
}

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLUGIN_PATH="$REPO_ROOT/Debug/hyprscroller.so"
KEEP_OPEN=0
NESTED_INSTANCE=""
NESTED_PID=""
TERMINAL_KIND=""
GRID_TITLE="grid-source"
CANVAS_TITLE="canvas-target"
GRID_CLASS="hs-grid-source"
CANVAS_CLASS="hs-canvas-target"

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

[[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]] || die "run this inside an existing Hyprland session"
[[ -f "$PLUGIN_PATH" ]] || die "plugin not found: $PLUGIN_PATH"

pick_terminal_kind

RUN_DIR="$(mktemp -d /tmp/hyprscroller-grid-canvas-cross.XXXXXX)"
CONFIG_PATH="$RUN_DIR/hyprland.conf"
LOG_PATH="$RUN_DIR/hyprland.log"
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

debug {
    disable_logs = false
}
EOF

BEFORE_MAX_TIME="$(hyprctl instances -j | jq '[.[].time] | max // 0')"
Hyprland -c "$CONFIG_PATH" >"$LOG_PATH" 2>&1 &
NESTED_PID=$!

for ((attempt = 0; attempt < 80; ++attempt)); do
    NESTED_INSTANCE="$(hyprctl instances -j | jq -r --argjson before "$BEFORE_MAX_TIME" '
        (map(select(.time > $before)) | max_by(.time)? | .instance) // empty
    ')"
    [[ -n "$NESTED_INSTANCE" ]] && break
    sleep 0.25
done

[[ -n "$NESTED_INSTANCE" ]] || die "failed to detect nested instance"
wait_for_nested_ready || die "nested Hyprland never became ready"

hyprctl -i "$NESTED_INSTANCE" output create wayland >/dev/null
wait_for_nested_monitor_count 2 || die "timed out waiting for second nested monitor"

SOURCE_MONITOR="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r '.[0].name')"
TARGET_MONITOR="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r '.[1].name')"
SOURCE_MONITOR_ID="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r --arg name "$SOURCE_MONITOR" '.[] | select(.name == $name) | .id')"
TARGET_MONITOR_ID="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r --arg name "$TARGET_MONITOR" '.[] | select(.name == $name) | .id')"

focus_monitor_workspace "$SOURCE_MONITOR" 1
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scrollergrid >/dev/null
launch_terminal_window "$GRID_CLASS" "$GRID_TITLE"
wait_for_window_title "$GRID_TITLE" || die "$GRID_TITLE did not appear"

focus_monitor_workspace "$TARGET_MONITOR" 2
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scroller >/dev/null
launch_terminal_window "$CANVAS_CLASS" "$CANVAS_TITLE"
wait_for_window_title "$CANVAS_TITLE" || die "$CANVAS_TITLE did not appear"
sleep 1

focus_monitor_workspace "$SOURCE_MONITOR" 1
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scrollergrid >/dev/null
focus_title "$GRID_TITLE"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movefocus r >/dev/null
sleep 0.6
assert_active_title "$CANVAS_TITLE" "grid->Canvas movefocus"

focus_monitor_workspace "$SOURCE_MONITOR" 1
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scrollergrid >/dev/null
focus_title "$GRID_TITLE"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movewindow r >/dev/null
sleep 0.8
assert_client_location "$GRID_TITLE" 2 "$TARGET_MONITOR_ID" "grid->Canvas movewindow"

focus_monitor_workspace "$TARGET_MONITOR" 2
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scroller >/dev/null
focus_title "$GRID_TITLE"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movewindow l >/dev/null
sleep 0.8
assert_client_location "$GRID_TITLE" 1 "$SOURCE_MONITOR_ID" "Canvas->grid movewindow"

{
    printf 'nested instance: %s\n' "$NESTED_INSTANCE"
    printf 'plugin path:     %s\n' "$PLUGIN_PATH"
    printf 'config:          %s\n' "$CONFIG_PATH"
    printf 'nested log:      %s\n' "$LOG_PATH"
    printf 'run dir:         %s\n' "$RUN_DIR"
    printf 'source monitor:  %s\n' "$SOURCE_MONITOR"
    printf 'target monitor:  %s\n' "$TARGET_MONITOR"
} | tee "$RESULT_PATH"

cat "$RESULT_PATH"
cat "$SUMMARY_PATH"
