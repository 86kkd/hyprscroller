#!/usr/bin/env bash

# Launch a nested Hyprland session and verify directional handoff between the
# experimental scrollergrid layout and the legacy scroller CanvasLayout.

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
Usage: scripts/repro-grid-canvas-cross-monitor.sh [options]

Options:
  --outer-monitor NAME   Place the floating nested Hyprland window on NAME.
  --window-size WxH      Outer floating window size. Default: 1800x1200.
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

launch_nested_exec_with_rules() {
    local rules="$1"
    local command
    shift
    command=$(quote_command "$@")
    hyprctl -i "$NESTED_INSTANCE" dispatch exec "$rules $command" >/dev/null
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

wait_for_outer_output_window_title() {
    local title="$1"

    for ((attempt = 0; attempt < 100; ++attempt)); do
        if hyprctl clients -j | jq -e --arg title "$title" '
            any(.[]; .class == "aquamarine" and .title == $title)
        ' >/dev/null; then
            return 0
        fi
        sleep 0.1
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

launch_terminal_window() {
    local class="$1"
    local title="$2"
    local rules="${3:-}"

    launch_terminal_window_with_kind "$TERMINAL_KIND" "$class" "$title" "$rules"
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

focus_monitor_workspace() {
    local monitor="$1"
    local workspace="$2"
    move_cursor_to_monitor "$monitor"
    hyprctl -i "$NESTED_INSTANCE" dispatch focusmonitor "$monitor" >/dev/null
    hyprctl -i "$NESTED_INSTANCE" dispatch workspace "$workspace" >/dev/null
    move_cursor_to_monitor "$monitor"
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

position_outer_output_window() {
    local title="$1"
    local selector="title:^${title}$"

    hyprctl --batch "dispatch setfloating ${selector}; dispatch resizewindowpixel exact ${WINDOW_WIDTH} ${WINDOW_HEIGHT},${selector}" >/dev/null
    sleep 0.5
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
WINDOW_WIDTH=1800
WINDOW_HEIGHT=1200
KEEP_OPEN=0
OUTER_MONITOR=""
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

require_cmd start-hyprland
require_cmd hyprctl
require_cmd jq
require_cmd realpath

[[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]] || die "run this inside an existing Hyprland session"
[[ -f "$PLUGIN_PATH" ]] || die "plugin not found: $PLUGIN_PATH"
PLUGIN_PATH="$(realpath "$PLUGIN_PATH")"

pick_terminal_kind
if [[ -z "$OUTER_MONITOR" ]]; then
    OUTER_MONITOR="$(detect_outer_monitor)"
    [[ -n "$OUTER_MONITOR" ]] || die "could not auto-detect an outer monitor"
fi

RUN_DIR="$(mktemp -d /tmp/hyprscroller-grid-canvas-cross.XXXXXX)"
CONFIG_PATH="$RUN_DIR/hyprland.conf"
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

cat >"$LAUNCHER_PATH" <<EOF
#!/usr/bin/env bash
exec env -u HYPRLAND_INSTANCE_SIGNATURE start-hyprland -- -c "$CONFIG_PATH" >"$LOG_PATH" 2>&1
EOF
chmod +x "$LAUNCHER_PATH"

BEFORE_INSTANCES="$(hyprctl instances -j | jq -c 'map(.instance)')"
LAUNCH_RULES="[monitor $OUTER_MONITOR; float; size $WINDOW_WIDTH $WINDOW_HEIGHT; center]"
log_step "launch nested Hyprland on $OUTER_MONITOR"
hyprctl dispatch exec "$LAUNCH_RULES $LAUNCHER_PATH" >/dev/null

for ((attempt = 0; attempt < 80; ++attempt)); do
    NESTED_INSTANCE="$(hyprctl instances -j | jq -r --argjson before "$BEFORE_INSTANCES" '
        (map(select(.instance as $id | ($before | index($id) | not))) | max_by(.time)? | .instance) // empty
    ')"
    [[ -n "$NESTED_INSTANCE" ]] && break
    sleep 0.25
done

[[ -n "$NESTED_INSTANCE" ]] || die "failed to detect nested instance"
wait_for_nested_ready || die "nested Hyprland never became ready"
log_step "nested Hyprland ready: $NESTED_INSTANCE"

SOURCE_MONITOR="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r 'sort_by(.id) | .[0].name')"
[[ -n "$SOURCE_MONITOR" ]] || die "could not resolve source nested monitor"
hyprctl -i "$NESTED_INSTANCE" keyword monitor "$SOURCE_MONITOR,${WINDOW_WIDTH}x${WINDOW_HEIGHT}@60,0x0,1" >/dev/null
wait_for_monitor_usable "$SOURCE_MONITOR" || die "source monitor never reported non-zero geometry"
TARGET_MONITOR_X=$(( $(monitor_coordinate_width "$SOURCE_MONITOR") + 20 ))
log_step "source nested monitor ready: $SOURCE_MONITOR"

TARGET_MONITOR=""
for ((output_attempt = 0; output_attempt < 3; ++output_attempt)); do
    log_step "create second nested wayland output, attempt $(( output_attempt + 1 ))/3"
    hyprctl -i "$NESTED_INSTANCE" output create wayland >/dev/null
    wait_for_nested_monitor_count 2 || die "timed out waiting for second nested monitor"

    candidate_monitor="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r --arg source "$SOURCE_MONITOR" '
        sort_by(.id) | map(select(.name != $source)) | .[-1].name // empty
    ')"
    [[ -n "$candidate_monitor" ]] || continue

    TARGET_OUTER_TITLE="aquamarine - $candidate_monitor"
    log_step "waiting for outer output window: $TARGET_OUTER_TITLE"
    if wait_for_outer_output_window_title "$TARGET_OUTER_TITLE"; then
        log_step "outer output window ready: $TARGET_OUTER_TITLE"
        position_outer_output_window "$TARGET_OUTER_TITLE"
        hyprctl -i "$NESTED_INSTANCE" keyword monitor "$candidate_monitor,${WINDOW_WIDTH}x${WINDOW_HEIGHT}@60,${TARGET_MONITOR_X}x0,1" >/dev/null
        if wait_for_monitor_usable "$candidate_monitor"; then
            TARGET_MONITOR="$candidate_monitor"
            log_step "target nested monitor ready: $TARGET_MONITOR"
            break
        fi
    fi

    hyprctl -i "$NESTED_INSTANCE" output remove "$candidate_monitor" >/dev/null 2>&1 || true
    sleep 0.5
done

[[ -n "$TARGET_MONITOR" ]] \
    || die "outer output window for a second wayland monitor never appeared; this Hyprland nested session cannot expose a second non-zero wayland output"

SOURCE_MONITOR_ID="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r --arg name "$SOURCE_MONITOR" '.[] | select(.name == $name) | .id')"
TARGET_MONITOR_ID="$(hyprctl -i "$NESTED_INSTANCE" monitors -j | jq -r --arg name "$TARGET_MONITOR" '.[] | select(.name == $name) | .id')"
hyprctl -i "$NESTED_INSTANCE" keyword windowrulev2 "workspace 1,class:^${GRID_CLASS}$" >/dev/null
hyprctl -i "$NESTED_INSTANCE" keyword windowrulev2 "workspace 2,class:^${CANVAS_CLASS}$" >/dev/null

focus_monitor_workspace "$SOURCE_MONITOR" 1
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scrollergrid >/dev/null
log_step "launch grid source window"
launch_terminal_window_and_wait "$GRID_CLASS" "$GRID_TITLE" "[workspace 1 silent]" || die "$GRID_TITLE did not appear"
assert_client_location "$GRID_TITLE" 1 "$SOURCE_MONITOR_ID" "grid source placement"

focus_monitor_workspace "$TARGET_MONITOR" 2
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scroller >/dev/null
log_step "launch Canvas target window"
launch_terminal_window_and_wait "$CANVAS_CLASS" "$CANVAS_TITLE" "[workspace 2 silent]" || die "$CANVAS_TITLE did not appear"
if [[ "$(client_workspace_id "$CANVAS_TITLE")" != "2" ]]; then
    focus_title "$CANVAS_TITLE" || die "$CANVAS_TITLE did not become active before placement repair"
    hyprctl -i "$NESTED_INSTANCE" dispatch movetoworkspacesilent 2 >/dev/null
    sleep 0.5
fi
assert_client_location "$CANVAS_TITLE" 2 "$TARGET_MONITOR_ID" "Canvas target placement"
sleep 1

focus_monitor_workspace "$SOURCE_MONITOR" 1
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scrollergrid >/dev/null
focus_title "$GRID_TITLE" || die "$GRID_TITLE did not become active before movewindow"
log_step "dispatch grid->Canvas movewindow"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movewindow r >/dev/null
sleep 0.8
assert_client_location "$GRID_TITLE" 2 "$TARGET_MONITOR_ID" "grid->Canvas movewindow"

focus_monitor_workspace "$TARGET_MONITOR" 2
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scroller >/dev/null
focus_title "$GRID_TITLE" || die "$GRID_TITLE did not become active before Canvas->grid movewindow"
log_step "dispatch Canvas->grid movewindow"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movewindow l >/dev/null
sleep 0.8
assert_client_location "$GRID_TITLE" 1 "$SOURCE_MONITOR_ID" "Canvas->grid movewindow"

focus_monitor_workspace "$SOURCE_MONITOR" 1
hyprctl -i "$NESTED_INSTANCE" keyword general:layout scrollergrid >/dev/null
focus_title "$GRID_TITLE" || die "$GRID_TITLE did not become active before movefocus"
log_step "dispatch grid->Canvas movefocus"
hyprctl -i "$NESTED_INSTANCE" dispatch scroller:movefocus r >/dev/null
sleep 0.6
assert_active_title "$CANVAS_TITLE" "grid->Canvas movefocus"

{
    printf 'nested instance: %s\n' "$NESTED_INSTANCE"
    printf 'plugin path:     %s\n' "$PLUGIN_PATH"
    printf 'config:          %s\n' "$CONFIG_PATH"
    printf 'nested log:      %s\n' "$LOG_PATH"
    printf 'run dir:         %s\n' "$RUN_DIR"
    printf 'outer monitor:   %s\n' "$OUTER_MONITOR"
    printf 'source monitor:  %s\n' "$SOURCE_MONITOR"
    printf 'target monitor:  %s\n' "$TARGET_MONITOR"
} | tee "$RESULT_PATH"

cat "$RESULT_PATH"
cat "$SUMMARY_PATH"
