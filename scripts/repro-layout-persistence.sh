#!/usr/bin/env bash

# Launch a nested Hyprland session and verify that scroller restores the same
# logical layout after:
# 1. switching away to `master` and back,
# 2. switching away to `dwindle` and back,
# 3. unloading and reloading the plugin inside the nested instance.

set -euo pipefail

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: scripts/repro-layout-persistence.sh [options]

Options:
  --plugin PATH         Plugin .so to load. Default: ./Debug/hyprscroller.so.
  --keep-open           Leave the nested Hyprland instance running after setup.
  -h, --help            Show this help text.

The script exits non-zero when scroller fails to restore the same focused window
and client geometry after a layout switch or plugin reload.
EOF
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
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
    hyprctl -i "$NESTED_INSTANCE" dispatch exec "$1" >/dev/null
}

instance_exists() {
    hyprctl instances -j | jq -e --arg instance "$NESTED_INSTANCE" '
        any(.[]; .instance == $instance)
    ' >/dev/null
}

wait_for_instance_exit() {
    for ((attempt = 0; attempt < 80; ++attempt)); do
        if ! instance_exists; then
            return 0
        fi

        sleep 0.25
    done

    return 1
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

wait_for_window_title() {
    local title="$1"

    for ((attempt = 0; attempt < 80; ++attempt)); do
        if hyprctl -i "$NESTED_INSTANCE" clients -j | jq -e --arg title "$title" '
            any(.[]; .title == $title)
        ' >/dev/null; then
            return 0
        fi

        sleep 0.25
    done

    return 1
}

capture_clients() {
    local output="$1"

    hyprctl -i "$NESTED_INSTANCE" clients -j | jq -S '
        [
            .[]
            | select(.title == "layout-A" or .title == "layout-B" or .title == "layout-C")
            | {
                title,
                workspace: (.workspace.id // .workspace.name // .workspace),
                monitor,
                at,
                size,
                floating
            }
        ]
        | sort_by(.title)
    ' >"$output"
}

capture_activewindow() {
    local output="$1"

    hyprctl -i "$NESTED_INSTANCE" activewindow -j | jq -S '
        {
            title,
            workspace: (.workspace.id // .workspace.name // .workspace),
            monitor,
            at,
            size
        }
    ' >"$output"
}

capture_workspace_state() {
    local prefix="$1"

    hyprctl instances -j >"$RUN_DIR/${prefix}-instances.json"
    hyprctl -i "$NESTED_INSTANCE" workspaces -j >"$RUN_DIR/${prefix}-workspaces.json"
    hyprctl -i "$NESTED_INSTANCE" activeworkspace -j >"$RUN_DIR/${prefix}-activeworkspace.json"
    hyprctl -i "$NESTED_INSTANCE" plugin list >"$RUN_DIR/${prefix}-plugins.txt"
}

focus_title() {
    local title="$1"

    hyprctl -i "$NESTED_INSTANCE" dispatch focuswindow "title:^${title}$" >/dev/null
    sleep 0.3
}

launch_terminal_window() {
    local title="$1"

    case "$TERMINAL_KIND" in
        kitty)
            launch_nested_exec "kitty --class hs-layout-persistence --title $title bash -lc \"printf '%s\\n' '$title'; exec bash\""
            ;;
        alacritty)
            launch_nested_exec "alacritty --class hs-layout-persistence --title $title -e bash -lc \"printf '%s\\n' '$title'; exec bash\""
            ;;
        *)
            die "unsupported terminal kind: $TERMINAL_KIND"
            ;;
    esac
}

snapshot_runtime_state() {
    local output="$1"

    if [[ -f "$RUNTIME_STATE" ]]; then
        cp "$RUNTIME_STATE" "$output"
    fi
}

assert_equal_snapshot() {
    local label="$1"
    local before_clients="$2"
    local after_clients="$3"
    local before_active="$4"
    local after_active="$5"

    if ! cmp -s "$before_clients" "$after_clients"; then
        printf '%s: client geometry mismatch\n' "$label" | tee -a "$SUMMARY_PATH" >&2
        diff -u "$before_clients" "$after_clients" | sed -n '1,200p' >&2 || true
        return 1
    fi

    if ! cmp -s "$before_active" "$after_active"; then
        printf '%s: active window mismatch\n' "$label" | tee -a "$SUMMARY_PATH" >&2
        diff -u "$before_active" "$after_active" | sed -n '1,120p' >&2 || true
        return 1
    fi

    printf '%s: OK\n' "$label" | tee -a "$SUMMARY_PATH"
}

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLUGIN_PATH="$REPO_ROOT/Debug/hyprscroller.so"
KEEP_OPEN=0
NESTED_INSTANCE=""
NESTED_PID=""

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
[[ -f "$PLUGIN_PATH" ]] || die "plugin not found: $PLUGIN_PATH (run 'make debug' first)"

pick_terminal_kind

RUN_DIR="$(mktemp -d /tmp/hyprscroller-layout-persistence.XXXXXX)"
CONFIG_PATH="$RUN_DIR/hyprland.conf"
LOG_PATH="$RUN_DIR/hyprland.log"
RESULT_PATH="$RUN_DIR/result.txt"
SUMMARY_PATH="$RUN_DIR/summary.txt"
RUNTIME_STATE=""

cat >"$CONFIG_PATH" <<EOF
monitor = , preferred, auto, 1

plugin = $PLUGIN_PATH

env = XCURSOR_SIZE,24

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

cursor {
    no_hardware_cursors = true
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
    if [[ -n "$NESTED_INSTANCE" ]]; then
        break
    fi

    sleep 0.25
done

[[ -n "$NESTED_INSTANCE" ]] || die "failed to detect nested instance"
wait_for_nested_ready || die "nested Hyprland never became ready"

launch_terminal_window layout-A
wait_for_window_title layout-A || die "layout-A did not appear"
sleep 0.5

launch_terminal_window layout-B
wait_for_window_title layout-B || die "layout-B did not appear"
sleep 0.5

launch_terminal_window layout-C
wait_for_window_title layout-C || die "layout-C did not appear"
sleep 1

focus_title layout-C
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:admitwindow >/dev/null
sleep 0.4
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:createlane r >/dev/null
sleep 0.4
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movefocus u >/dev/null
sleep 0.4
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:setmode col >/dev/null
sleep 0.8

capture_clients "$RUN_DIR/before-clients.json"
capture_activewindow "$RUN_DIR/before-activewindow.json"
capture_workspace_state before
RUNTIME_STATE="${XDG_RUNTIME_DIR:-/tmp}/hyprscroller-layout-${NESTED_INSTANCE}.state"
snapshot_runtime_state "$RUN_DIR/before-runtime.state"

hyprctl -i "$NESTED_INSTANCE" keyword general:layout master >/dev/null
sleep 0.8
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scroller >/dev/null
sleep 1
capture_clients "$RUN_DIR/after-master-clients.json"
capture_activewindow "$RUN_DIR/after-master-activewindow.json"
capture_workspace_state after-master
assert_equal_snapshot "master->scroller" \
    "$RUN_DIR/before-clients.json" \
    "$RUN_DIR/after-master-clients.json" \
    "$RUN_DIR/before-activewindow.json" \
    "$RUN_DIR/after-master-activewindow.json"

hyprctl -i "$NESTED_INSTANCE" keyword general:layout dwindle >/dev/null
sleep 0.8
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scroller >/dev/null
sleep 1
capture_clients "$RUN_DIR/after-dwindle-clients.json"
capture_activewindow "$RUN_DIR/after-dwindle-activewindow.json"
capture_workspace_state after-dwindle
assert_equal_snapshot "dwindle->scroller" \
    "$RUN_DIR/before-clients.json" \
    "$RUN_DIR/after-dwindle-clients.json" \
    "$RUN_DIR/before-activewindow.json" \
    "$RUN_DIR/after-dwindle-activewindow.json"

hyprctl -i "$NESTED_INSTANCE" plugin unload "$PLUGIN_PATH" >/dev/null
sleep 0.8
hyprctl -i "$NESTED_INSTANCE" plugin load "$PLUGIN_PATH" >/dev/null
sleep 0.8
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scroller >/dev/null
sleep 1
capture_clients "$RUN_DIR/after-reload-clients.json"
capture_activewindow "$RUN_DIR/after-reload-activewindow.json"
capture_workspace_state after-reload
assert_equal_snapshot "plugin reload" \
    "$RUN_DIR/before-clients.json" \
    "$RUN_DIR/after-reload-clients.json" \
    "$RUN_DIR/before-activewindow.json" \
    "$RUN_DIR/after-reload-activewindow.json"

snapshot_runtime_state "$RUN_DIR/after-runtime.state"
tail -n 200 "$HOME/.hyprland/plugins/hyprscroller/hyprscroller.log" >"$RUN_DIR/hyprscroller-log-tail.txt" || true

{
    printf 'nested instance: %s\n' "$NESTED_INSTANCE"
    printf 'plugin path:     %s\n' "$PLUGIN_PATH"
    printf 'config:          %s\n' "$CONFIG_PATH"
    printf 'nested log:      %s\n' "$LOG_PATH"
    printf 'run dir:         %s\n' "$RUN_DIR"
    printf 'runtime state:   %s\n' "$RUNTIME_STATE"
} | tee "$RESULT_PATH"

cat "$RESULT_PATH"
cat "$SUMMARY_PATH"
