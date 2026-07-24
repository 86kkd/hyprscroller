#!/usr/bin/env bash

# Launch a nested Hyprland overview repro session on a chosen outer monitor
# without touching the user's main login session. The script:
# 1. writes a minimal nested Hyprland config that loads the debug plugin,
# 2. opens the nested compositor as a floating window on the requested monitor,
# 3. creates enough tiled windows to push the first preview off-screen, and
# 4. opens `scroller:toggleoverview` from inside the nested session itself so
#    the visual overview test actually runs before the nested compositor exits.

set -euo pipefail

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: scripts/repro-overview.sh [options]

Options:
  --outer-monitor NAME   Place the floating nested Hyprland window on NAME.
  --window-size WxH      Outer floating window size. Default: 1400x1800.
  --plugin PATH          Plugin .so to load. Default: ./Debug/hyprscroller.so.
  --keep-open            Leave the nested Hyprland instance running after setup.
  --hold-seconds N       Keep overview visible for N seconds before exit. Default: 2.
  -h, --help             Show this help text.

By default the script runs the overview flow and then exits the nested Hyprland
instance automatically. Use --keep-open if you want to inspect it manually.
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

pick_preview_candidates() {
    PREVIEW_CANDIDATES=()

    if command -v loupe >/dev/null 2>&1; then
        PREVIEW_CANDIDATES+=(loupe)
    fi

    if command -v eog >/dev/null 2>&1; then
        PREVIEW_CANDIDATES+=(eog)
    fi

    if command -v pavucontrol >/dev/null 2>&1; then
        PREVIEW_CANDIDATES+=(pavucontrol)
    fi

    if command -v nwg-look >/dev/null 2>&1; then
        PREVIEW_CANDIDATES+=(nwg-look)
    fi

    PREVIEW_CANDIDATES+=(terminal-preview)
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

    for ((attempt = 0; attempt < 40; ++attempt)); do
        if (( "$(nested_client_count)" >= minimum )); then
            return 0
        fi

        sleep 0.25
    done

    return 1
}

wait_for_nested_client_count_gt_stable() {
    local before="$1"

    for ((attempt = 0; attempt < 40; ++attempt)); do
        if (( "$(nested_client_count)" > before )); then
            sleep 0.5
            if (( "$(nested_client_count)" > before )); then
                return 0
            fi
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
    local index="$1"

    case "$TERMINAL_KIND" in
        kitty)
            launch_nested_exec kitty --class "hs-overview-${index}"
            ;;
        alacritty)
            launch_nested_exec alacritty --class "hs-overview-${index}"
            ;;
        *)
            die "unsupported terminal kind: $TERMINAL_KIND"
            ;;
    esac
}

launch_terminal_preview_window() {
    case "$TERMINAL_KIND" in
        kitty)
            launch_nested_exec kitty --class hs-overview-preview "$PREVIEW_TERMINAL_PATH"
            ;;
        alacritty)
            launch_nested_exec alacritty --class hs-overview-preview -e "$PREVIEW_TERMINAL_PATH"
            ;;
        *)
            die "unsupported terminal kind: $TERMINAL_KIND"
            ;;
    esac
}

launch_preview_window() {
    local before_count kind
    before_count="$(nested_client_count)"

    # Try real image viewers first so the repro includes a clearly recognizable
    # preview surface. If they fail to stay mapped inside the nested session,
    # fall back to a colored terminal window instead of silently producing only
    # terminal cards.
    for kind in "${PREVIEW_CANDIDATES[@]}"; do
        case "$kind" in
            loupe)
                launch_nested_exec loupe "$PATTERN_PATH"
                ;;
            eog)
                launch_nested_exec eog "$PATTERN_PATH"
                ;;
            pavucontrol)
                launch_nested_exec pavucontrol
                ;;
            nwg-look)
                launch_nested_exec nwg-look
                ;;
            terminal-preview)
                launch_terminal_preview_window
                ;;
            *)
                die "unsupported preview kind: $kind"
                ;;
        esac

        if wait_for_nested_client_count_gt_stable "$before_count"; then
            PREVIEW_KIND="$kind"
            return 0
        fi
    done

    die "failed to launch a preview window inside nested Hyprland"
}

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATTERN_PATH="$REPO_ROOT/tests/assets/overview-pattern.svg"
PLUGIN_PATH="$REPO_ROOT/Debug/hyprscroller.so"
WINDOW_WIDTH=1400
WINDOW_HEIGHT=1800
KEEP_OPEN=0
OVERVIEW_HOLD_SECONDS=2
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
            [[ $# -ge 2 ]] || die "--window-size requires a value like 1400x1800"
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
[[ -f "$PATTERN_PATH" ]] || die "pattern asset missing: $PATTERN_PATH"

pick_terminal_kind
pick_preview_candidates

if [[ -z "$OUTER_MONITOR" ]]; then
    OUTER_MONITOR="$(detect_outer_monitor)"
    [[ -n "$OUTER_MONITOR" ]] || die "could not auto-detect an outer monitor"
fi

RUN_DIR="$(mktemp -d /tmp/hyprscroller-overview-repro.XXXXXX)"
CONFIG_PATH="$RUN_DIR/hyprland.conf"
LOG_PATH="$RUN_DIR/hyprland.log"
LAUNCHER_PATH="$RUN_DIR/launch-nested.sh"
PREVIEW_TERMINAL_PATH="$RUN_DIR/preview-terminal.sh"
FINISHER_PATH="$RUN_DIR/finish-overview.sh"

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

cat >"$PREVIEW_TERMINAL_PATH" <<'EOF'
#!/usr/bin/env bash
clear
printf 'hyprscroller overview preview fallback\n\n'
for _row in 1 2 3 4 5 6; do
    printf '\033[41m%*s\033[42m%*s\033[0m\n' 24 '' 24 ''
done
for _row in 1 2 3 4 5 6; do
    printf '\033[44m%*s\033[43m%*s\033[0m\n' 24 '' 24 ''
done
printf '\n'
exec bash
EOF
chmod +x "$PREVIEW_TERMINAL_PATH"

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

launch_preview_window

for index in 1 2 3 4; do
    launch_terminal_window "$index"
    sleep 1
done

wait_for_nested_client_count 5 || die "timed out waiting for preview window plus four terminals"

cat >"$FINISHER_PATH" <<EOF
#!/usr/bin/env bash
sleep 1
hyprctl dispatch scroller:toggleoverview
sleep $OVERVIEW_HOLD_SECONDS
$(if [[ "$KEEP_OPEN" -eq 1 ]]; then printf ':\n'; else printf 'hyprctl dispatch exit\n'; fi)
EOF
chmod +x "$FINISHER_PATH"

launch_nested_exec "$FINISHER_PATH"

if [[ "$KEEP_OPEN" -eq 1 ]]; then
    sleep $((OVERVIEW_HOLD_SECONDS + 1))
else
    wait_for_instance_exit || die "timed out waiting for nested Hyprland to exit"
fi

printf 'nested instance: %s\n' "$NESTED_INSTANCE"
printf 'nested socket:   %s\n' "$NESTED_SOCKET"
printf 'outer monitor:   %s\n' "$OUTER_MONITOR"
printf 'preview app:     %s\n' "$PREVIEW_KIND"
printf 'terminal app:    %s\n' "$TERMINAL_KIND"
printf 'run dir:         %s\n' "$RUN_DIR"
printf 'config:          %s\n' "$CONFIG_PATH"
printf 'log:             %s\n' "$LOG_PATH"
if [[ "$KEEP_OPEN" -eq 1 ]]; then
    printf 'exit command:    hyprctl -i %q dispatch exit\n' "$NESTED_INSTANCE"
else
    printf 'result:          nested overview test completed and exited\n'
fi
