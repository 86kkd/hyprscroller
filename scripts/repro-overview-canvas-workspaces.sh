#!/usr/bin/env bash

# Launch a nested Hyprland session and verify the new canvas-workspace flow:
# - outside overview, `scroller:focusmonitor r/l` only moves monitor focus
# - inside overview, `scroller:focusmonitor r` creates/selects a blank canvas
#   workspace and accepting overview switches every visible monitor together

set -euo pipefail

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: scripts/repro-overview-canvas-workspaces.sh [options]

Options:
  --outer-monitor NAME   Place the floating nested Hyprland window on NAME.
  --window-size WxH      Outer floating window size. Default: 1800x1200.
  --plugin PATH          Plugin .so to load. Default: ./Debug/hyprscroller.so.
  --keep-open            Leave the nested Hyprland instance running after setup.
  --hold-seconds N       Delay after opening overview before navigation. Default: 1.
  -h, --help             Show this help text.

The script exits non-zero when normal-mode `scroller:focusmonitor` fails to
move monitor focus, or when overview `scroller:focusmonitor` fails to switch
all visible monitors to a new blank canvas workspace.
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
    command="$(quote_command "$@")"
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

wait_for_nested_monitor_count() {
    local expected="$1"

    for ((attempt = 0; attempt < 60; ++attempt)); do
        if (( "$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq 'length')" >= expected )); then
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

focus_nested_window() {
    local selector="$1"
    hyprctl -i "$NESTED_INSTANCE" dispatch focuswindow "$selector" >/dev/null
}

float_window_by_class() {
    local class="$1"
    focus_nested_window "class:$class"
    sleep 0.2
    hyprctl -i "$NESTED_INSTANCE" dispatch togglefloating >/dev/null
    sleep 0.2
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

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLUGIN_PATH="$REPO_ROOT/Debug/hyprscroller.so"
WINDOW_WIDTH=1800
WINDOW_HEIGHT=1200
KEEP_OPEN=0
OVERVIEW_HOLD_SECONDS=1
OUTER_MONITOR=""
NESTED_INSTANCE=""
SOURCE_CLASS="hs-canvas-source"
TARGET_CLASS="hs-canvas-target"

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
LAUNCH_RULES="[monitor $OUTER_MONITOR; float; size $WINDOW_WIDTH $WINDOW_HEIGHT; center]"
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

hyprctl -i "$NESTED_INSTANCE" output create wayland >/dev/null
wait_for_nested_monitor_count 2 || die "timed out waiting for second nested monitor"

SOURCE_MONITOR="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r '.[0].name')"
TARGET_MONITOR="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r '.[1].name')"
[[ -n "$SOURCE_MONITOR" && -n "$TARGET_MONITOR" ]] || die "could not resolve nested monitor names"

hyprctl -i "$NESTED_INSTANCE" dispatch focusmonitor "$SOURCE_MONITOR" >/dev/null
launch_terminal_window "$SOURCE_CLASS" "canvas-source"
wait_for_nested_client_count 1 || die "timed out waiting for source window"
float_window_by_class "$SOURCE_CLASS"

hyprctl -i "$NESTED_INSTANCE" dispatch focusmonitor "$TARGET_MONITOR" >/dev/null
launch_terminal_window "$TARGET_CLASS" "canvas-target"
wait_for_nested_client_count 2 || die "timed out waiting for target window"
float_window_by_class "$TARGET_CLASS"

sleep 1
INITIAL_MONITORS_JSON="$(hyprctl -i "$NESTED_INSTANCE" monitors -j)"
INITIAL_SOURCE_WORKSPACE="$(printf '%s\n' "$INITIAL_MONITORS_JSON" | jq -r --arg monitor "$SOURCE_MONITOR" '.[] | select(.name == $monitor) | .activeWorkspace.id')"
INITIAL_TARGET_WORKSPACE="$(printf '%s\n' "$INITIAL_MONITORS_JSON" | jq -r --arg monitor "$TARGET_MONITOR" '.[] | select(.name == $monitor) | .activeWorkspace.id')"

hyprctl -i "$NESTED_INSTANCE" dispatch focusmonitor "$SOURCE_MONITOR" >/dev/null
focus_nested_window "class:$SOURCE_CLASS"
sleep 0.3
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:focusmonitor r >/dev/null
sleep 0.4
NORMAL_FOCUSED_MONITOR="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r '.[] | select(.focused == true) | .name')"
NORMAL_FOCUS_OK=0
if [[ "$NORMAL_FOCUSED_MONITOR" == "$TARGET_MONITOR" ]]; then
    NORMAL_FOCUS_OK=1
fi

hyprctl -i "$NESTED_INSTANCE" dispatch scroller:focusmonitor l >/dev/null
sleep 0.4
hyprctl -i "$NESTED_INSTANCE" dispatch focusmonitor "$SOURCE_MONITOR" >/dev/null
focus_nested_window "class:$SOURCE_CLASS"
sleep 0.3
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview >/dev/null
sleep "$OVERVIEW_HOLD_SECONDS"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:focusmonitor r >/dev/null
sleep 0.4
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview accept >/dev/null
sleep 0.7

AFTER_CREATE_MONITORS_JSON="$(hyprctl -i "$NESTED_INSTANCE" monitors -j)"
AFTER_CREATE_SOURCE_WORKSPACE="$(printf '%s\n' "$AFTER_CREATE_MONITORS_JSON" | jq -r --arg monitor "$SOURCE_MONITOR" '.[] | select(.name == $monitor) | .activeWorkspace.id')"
AFTER_CREATE_TARGET_WORKSPACE="$(printf '%s\n' "$AFTER_CREATE_MONITORS_JSON" | jq -r --arg monitor "$TARGET_MONITOR" '.[] | select(.name == $monitor) | .activeWorkspace.id')"

hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview >/dev/null
sleep "$OVERVIEW_HOLD_SECONDS"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:focusmonitor l >/dev/null
sleep 0.4
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:toggleoverview accept >/dev/null
sleep 0.7

RUNTIME_BASE_DIR="${XDG_RUNTIME_DIR:-/tmp}"
RUNTIME_STATE="$RUNTIME_BASE_DIR/hyprscroller-canvas-$NESTED_INSTANCE.state"
RUNTIME_STATE_COPY="$RUN_DIR/runtime-state.txt"
if [[ -f "$RUNTIME_STATE" ]]; then
    cp "$RUNTIME_STATE" "$RUNTIME_STATE_COPY"
else
    : >"$RUNTIME_STATE_COPY"
fi

jq -n \
  --argjson initialMonitors "$INITIAL_MONITORS_JSON" \
  --argjson afterCreateMonitors "$AFTER_CREATE_MONITORS_JSON" \
  --argjson monitors "$(hyprctl -i "$NESTED_INSTANCE" monitors -j)" \
  --argjson workspaces "$(hyprctl -i "$NESTED_INSTANCE" workspaces -j)" \
  --argjson clients "$(hyprctl -i "$NESTED_INSTANCE" clients -j)" \
  --arg normalFocusedMonitor "$NORMAL_FOCUSED_MONITOR" \
  --argjson normalFocusOk "$NORMAL_FOCUS_OK" \
  --arg runtimeStatePath "$RUNTIME_STATE_COPY" \
  '{
      initialMonitors: $initialMonitors,
      afterCreateMonitors: $afterCreateMonitors,
      monitors: $monitors,
      workspaces: $workspaces,
      clients: $clients,
      normalFocusedMonitor: $normalFocusedMonitor,
      normalFocusOk: ($normalFocusOk == 1),
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
printf 'initial source ws:       %s\n' "${INITIAL_SOURCE_WORKSPACE:-unknown}"
printf 'initial target ws:       %s\n' "${INITIAL_TARGET_WORKSPACE:-unknown}"
printf 'after create source ws:  %s\n' "${AFTER_CREATE_SOURCE_WORKSPACE:-unknown}"
printf 'after create target ws:  %s\n' "${AFTER_CREATE_TARGET_WORKSPACE:-unknown}"
printf 'returned source ws:      %s\n' "${FINAL_SOURCE_WORKSPACE:-unknown}"
printf 'returned target ws:      %s\n' "${FINAL_TARGET_WORKSPACE:-unknown}"
printf 'returned source clients: %s\n' "${RETURNED_SOURCE_CLIENTS:-unknown}"
printf 'returned target clients: %s\n' "${RETURNED_TARGET_CLIENTS:-unknown}"
printf 'canvas count:            %s\n' "${CANVAS_COUNT:-0}"

if [[ "$NORMAL_FOCUS_OK" -eq 1 \
   && "$AFTER_CREATE_SOURCE_WORKSPACE" != "$INITIAL_SOURCE_WORKSPACE" \
   && "$AFTER_CREATE_TARGET_WORKSPACE" != "$INITIAL_TARGET_WORKSPACE" \
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
