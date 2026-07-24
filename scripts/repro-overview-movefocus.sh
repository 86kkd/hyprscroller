#!/usr/bin/env bash

# Launch a nested Hyprland session and reproduce overview movefocus routing
# without touching the user's main login session. The scripted scenario creates:
# - workspace 1 with two stacked terminals
# - workspace 2 with three stacked terminals
# Then it returns to workspace 1, opens overview from the lower terminal, moves
# selection upward once, accepts via the same toggleoverview dispatcher, and
# verifies that focus stays in workspace 1.

set -euo pipefail

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: scripts/repro-overview-movefocus.sh [options]

Options:
  --outer-monitor NAME   Place the floating nested Hyprland window on NAME.
  --window-size WxH      Outer floating window size. Default: 1600x1200.
  --plugin PATH          Plugin .so to load. Default: ./Debug/hyprscroller.so.
  --keep-open            Leave the nested Hyprland instance running after setup.
  --hold-seconds N       Delay after opening overview before navigation. Default: 1.
  -h, --help             Show this help text.

The script exits non-zero when overview `movefocus u` accepts a window outside
workspace 1. That indicates the movefocus routing bug reproduced successfully.
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

nested_client_count() {
    hyprctl -i "$NESTED_INSTANCE" clients -j | jq 'length'
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

launch_terminal_window() {
    local workspace="$1"
    local index="$2"
    local title="ws${workspace}-${index}"
    local body="printf 'workspace %s window %s\\n' '$workspace' '$index'; exec bash"

    case "$TERMINAL_KIND" in
        kitty)
            launch_nested_exec kitty --class "hs-ov-mf-ws${workspace}" --title "$title" \
                bash -lc "$body"
            ;;
        alacritty)
            launch_nested_exec alacritty --class "hs-ov-mf-ws${workspace}" --title "$title" \
                -e bash -lc "$body"
            ;;
        *)
            die "unsupported terminal kind: $TERMINAL_KIND"
            ;;
    esac
}

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLUGIN_PATH="$REPO_ROOT/Debug/hyprscroller.so"
WINDOW_WIDTH=1600
WINDOW_HEIGHT=1200
KEEP_OPEN=0
OVERVIEW_HOLD_SECONDS=1
OUTER_MONITOR=""
NESTED_INSTANCE=""

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
            [[ $# -ge 2 ]] || die "--window-size requires a value like 1600x1200"
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
require_cmd start-hyprland

[[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]] || die "run this inside an existing Hyprland session"
[[ -f "$PLUGIN_PATH" ]] || die "plugin not found: $PLUGIN_PATH (run 'make debug' first)"

pick_terminal_kind

if [[ -z "$OUTER_MONITOR" ]]; then
    OUTER_MONITOR="$(detect_outer_monitor)"
    [[ -n "$OUTER_MONITOR" ]] || die "could not auto-detect an outer monitor"
fi

RUN_DIR="$(mktemp -d /tmp/hyprscroller-overview-movefocus.XXXXXX)"
CONFIG_PATH="$RUN_DIR/hyprland.conf"
LOG_PATH="$RUN_DIR/hyprland.log"
LAUNCHER_PATH="$RUN_DIR/launch-nested.sh"
RESULT_PATH="$RUN_DIR/result.json"

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

debug {
    disable_logs = false
}
EOF

cat >"$LAUNCHER_PATH" <<EOF
#!/usr/bin/env bash
exec env -u HYPRLAND_INSTANCE_SIGNATURE start-hyprland -- -c "$CONFIG_PATH" >"$LOG_PATH" 2>&1
EOF
chmod +x "$LAUNCHER_PATH"

BEFORE_INSTANCES="$(hyprctl instances -j | jq -c 'map(.instance)')"
LAUNCH_RULES="[monitor $OUTER_MONITOR; float; size $WINDOW_WIDTH $WINDOW_HEIGHT; center]"
hyprctl dispatch exec "$LAUNCH_RULES $LAUNCHER_PATH" >/dev/null

for ((attempt = 0; attempt < 80; ++attempt)); do
    NESTED_INSTANCE="$(hyprctl instances -j | jq -r --argjson before "$BEFORE_INSTANCES" '
        (map(select(.instance as $id | ($before | index($id) | not)))) as $instances
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

hyprctl -i "$NESTED_INSTANCE" dispatch workspace 1 >/dev/null
sleep 0.5
launch_terminal_window 1 top
sleep 0.8
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:setmode col >/dev/null
sleep 0.3
launch_terminal_window 1 bottom
sleep 0.8

hyprctl -i "$NESTED_INSTANCE" dispatch workspace 2 >/dev/null
sleep 0.5
launch_terminal_window 2 top
sleep 0.8
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:setmode col >/dev/null
sleep 0.3
launch_terminal_window 2 middle
sleep 0.8
launch_terminal_window 2 bottom
sleep 0.8

wait_for_nested_client_count 5 || die "timed out waiting for five nested terminals"

sleep 1
hyprctl -i "$NESTED_INSTANCE" dispatch workspace 1 >/dev/null
sleep 0.3
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movefocus d >/dev/null
sleep 0.3
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview >/dev/null
sleep "$OVERVIEW_HOLD_SECONDS"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movefocus u >/dev/null
sleep 0.3
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview >/dev/null
sleep 0.5

jq -n \
  --argjson activeWorkspace "$(hyprctl -i "$NESTED_INSTANCE" activeworkspace -j)" \
  --argjson activeWindow "$(hyprctl -i "$NESTED_INSTANCE" activewindow -j)" \
  '{activeWorkspace: $activeWorkspace, activeWindow: $activeWindow}' >"$RESULT_PATH"

if [[ "$KEEP_OPEN" -eq 1 ]]; then
    :
else
    hyprctl -i "$NESTED_INSTANCE" dispatch exit >/dev/null 2>&1 || true
    wait_for_instance_exit || die "timed out waiting for nested Hyprland to exit"
fi

[[ -f "$RESULT_PATH" ]] || die "nested scenario did not write a result file"

ACTIVE_WORKSPACE_ID="$(jq -r '.activeWorkspace.id // empty' "$RESULT_PATH")"
ACTIVE_WINDOW_TITLE="$(jq -r '.activeWindow.title // empty' "$RESULT_PATH")"
ACTIVE_WINDOW_WORKSPACE="$(jq -r '.activeWindow.workspace.id // empty' "$RESULT_PATH")"

printf 'nested instance:    %s\n' "$NESTED_INSTANCE"
printf 'nested socket:      %s\n' "$NESTED_SOCKET"
printf 'outer monitor:      %s\n' "$OUTER_MONITOR"
printf 'terminal app:       %s\n' "$TERMINAL_KIND"
printf 'run dir:            %s\n' "$RUN_DIR"
printf 'config:             %s\n' "$CONFIG_PATH"
printf 'log:                %s\n' "$LOG_PATH"
printf 'result file:        %s\n' "$RESULT_PATH"
printf 'accepted workspace: %s\n' "${ACTIVE_WORKSPACE_ID:-unknown}"
printf 'accepted window:    %s\n' "${ACTIVE_WINDOW_TITLE:-unknown}"

if [[ "$ACTIVE_WORKSPACE_ID" == "1" && "$ACTIVE_WINDOW_WORKSPACE" == "1" ]]; then
    printf 'result:             movefocus stayed inside workspace 1\n'
    exit 0
fi

printf 'result:             movefocus jumped outside workspace 1 (bug reproduced)\n' >&2
exit 1
