#!/usr/bin/env bash

hyprscroller_five_monitor_names() {
    local -n out_ref="$1"
    out_ref=(
        "WAYLAND-1"
        "WAYLAND-2"
        "WAYLAND-3"
        "WAYLAND-4"
        "WAYLAND-5"
    )
}

hyprscroller_five_outer_titles() {
    local -n out_ref="$1"
    out_ref=(
        "aquamarine - WAYLAND-1"
        "aquamarine - WAYLAND-2"
        "aquamarine - WAYLAND-3"
        "aquamarine - WAYLAND-4"
        "aquamarine - WAYLAND-5"
    )
}

hyprscroller_monitor_logical_geometry() {
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

hyprscroller_compute_five_monitor_outer_layout() {
    local outer_monitor_json="$1"
    local landscape_width="$2"
    local landscape_height="$3"
    local layout_margin="$4"
    local layout_gap="$5"
    local layout_scale_percent="$6"
    local -n out_xs_ref="$7"
    local -n out_ys_ref="$8"
    local -n out_widths_ref="$9"
    local -n out_heights_ref="${10}"

    local outer_monitor_x=0
    local outer_monitor_y=0
    local outer_monitor_width=0
    local outer_monitor_height=0
    local portrait_width="$landscape_height"
    local portrait_height="$landscape_width"
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

    hyprscroller_monitor_logical_geometry \
        "$outer_monitor_json" \
        outer_monitor_x \
        outer_monitor_y \
        outer_monitor_width \
        outer_monitor_height

    local available_width=$(( outer_monitor_width - (2 * layout_margin) ))
    local available_height=$(( outer_monitor_height - (2 * layout_margin) - layout_gap ))

    (( available_width > 0 && available_height > 0 )) || return 1

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
    local outer_origin_x=$(( (outer_monitor_width - total_scaled_width) / 2 ))
    local outer_origin_y=$(( (outer_monitor_height - total_scaled_height) / 2 ))
    local index

    out_xs_ref=()
    out_ys_ref=()
    out_widths_ref=()
    out_heights_ref=()

    for index in "${!base_widths[@]}"; do
        out_widths_ref[$index]=$(( (base_widths[$index] * scale_num) / scale_den ))
        out_heights_ref[$index]=$(( (base_heights[$index] * scale_num) / scale_den ))
        out_xs_ref[$index]=$(( outer_origin_x + ((base_xs[$index] * scale_num) / scale_den) ))
        out_ys_ref[$index]=$(( outer_origin_y + ((base_ys[$index] * scale_num) / scale_den) ))

        (( out_widths_ref[$index] > 0 && out_heights_ref[$index] > 0 )) || return 1
    done
}

hyprscroller_outer_event_socket_path() {
    local signature="${HYPRLAND_INSTANCE_SIGNATURE:-}"
    local runtime_dir="${XDG_RUNTIME_DIR:-/tmp}"
    local candidate

    [[ -n "$signature" ]] || return 1

    for candidate in \
        "$runtime_dir/hypr/$signature/.socket2.sock" \
        "/tmp/hypr/$signature/.socket2.sock"; do
        if [[ -S "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

hyprscroller_install_outer_output_map_time_float_rules() {
    local -n titles_ref="$1"
    local title

    for title in "${titles_ref[@]}"; do
        hyprctl keyword windowrulev2 "float,title:^${title}$" >/dev/null || return 1
    done
}

hyprscroller_stream_hyprland_events() {
    local event_socket="$1"

    python3 -c '
import socket
import sys

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(sys.argv[1])
print("__HYPRSCROLLER_EVENT_SOCKET_READY__", flush=True)

with sock.makefile("r", encoding="utf-8", errors="replace") as stream:
    for line in stream:
        sys.stdout.write(line)
        sys.stdout.flush()
' "$event_socket"
}

hyprscroller_position_outer_window_selector() {
    local selector="$1"
    local outer_monitor_x="$2"
    local outer_monitor_y="$3"
    local index="$4"
    local -n selector_xs_ref="$5"
    local -n selector_ys_ref="$6"
    local -n selector_widths_ref="$7"
    local -n selector_heights_ref="$8"

    hyprctl --batch "dispatch setfloating ${selector}; dispatch resizewindowpixel exact ${selector_widths_ref[$index]} ${selector_heights_ref[$index]},${selector}; dispatch movewindowpixel exact $(( outer_monitor_x + selector_xs_ref[$index] )) $(( outer_monitor_y + selector_ys_ref[$index] )),${selector}" >/dev/null
}

hyprscroller_position_outer_window_index() {
    local outer_monitor_x="$1"
    local outer_monitor_y="$2"
    local index="$3"
    local -n index_titles_ref="$4"
    local -n index_xs_ref="$5"
    local -n index_ys_ref="$6"
    local -n index_widths_ref="$7"
    local -n index_heights_ref="$8"

    hyprscroller_position_outer_window_selector \
        "title:^${index_titles_ref[$index]}$" \
        "$outer_monitor_x" \
        "$outer_monitor_y" \
        "$index" \
        index_xs_ref \
        index_ys_ref \
        index_widths_ref \
        index_heights_ref
}

hyprscroller_start_outer_output_event_watcher() {
    local -n watcher_pid_ref="$1"
    local event_socket="$2"
    local positioned_state_path="$3"
    local ready_path="$4"
    local outer_monitor_x="$5"
    local outer_monitor_y="$6"
    local -n titles_ref="$7"
    local -n xs_ref="$8"
    local -n ys_ref="$9"
    local -n widths_ref="${10}"
    local -n heights_ref="${11}"

    local titles=("${titles_ref[@]}")
    local xs=("${xs_ref[@]}")
    local ys=("${ys_ref[@]}")
    local widths=("${widths_ref[@]}")
    local heights=("${heights_ref[@]}")

    : >"$positioned_state_path"

    (
        trap 'exit 0' INT TERM

        local event
        local payload
        local address
        local workspace
        local class
        local title
        local index
        local position_attempt
        local positioned_ok
        local positioned_titles="|"
        local positioned_count=0

        hyprscroller_stream_hyprland_events "$event_socket" 2>/dev/null | while IFS= read -r event; do
            if [[ "$event" == "__HYPRSCROLLER_EVENT_SOCKET_READY__" ]]; then
                : >"$ready_path"
                continue
            fi

            [[ "$event" == openwindow\>\>* ]] || continue

            payload="${event#openwindow>>}"
            IFS=',' read -r address workspace class title <<<"$payload"
            [[ "$class" == "aquamarine" ]] || continue

            index=-1
            for candidate_index in "${!titles[@]}"; do
                if [[ "$title" == "${titles[$candidate_index]}" ]]; then
                    index="$candidate_index"
                    break
                fi
            done

            (( index >= 0 )) || continue

            positioned_ok=0
            for ((position_attempt = 0; position_attempt < 10; ++position_attempt)); do
                if hyprscroller_position_outer_window_selector \
                    "address:${address}" \
                    "$outer_monitor_x" \
                    "$outer_monitor_y" \
                    "$index" \
                    xs \
                    ys \
                    widths \
                    heights; then
                    positioned_ok=1
                    break
                fi

                sleep 0.02
            done

            if (( positioned_ok == 1 )); then
                if [[ "$positioned_titles" != *"|$title|"* ]]; then
                    positioned_titles="${positioned_titles}${title}|"
                    positioned_count=$(( positioned_count + 1 ))
                fi

                printf '%s\t%s\n' "$address" "$title" >>"$positioned_state_path"
            fi

            if (( positioned_count >= ${#titles[@]} )); then
                break
            fi
        done
    ) &

    watcher_pid_ref="$!"
}

hyprscroller_wait_for_outer_output_event_watcher_ready() {
    local ready_path="$1"

    for ((attempt = 0; attempt < 50; ++attempt)); do
        if [[ -f "$ready_path" ]]; then
            return 0
        fi

        sleep 0.02
    done

    return 1
}

hyprscroller_outer_output_position_event_count() {
    local positioned_state_path="$1"

    if [[ ! -f "$positioned_state_path" ]]; then
        printf '0\n'
        return
    fi

    awk -F '\t' 'NF >= 2 && !seen[$2]++ { count++ } END { print count + 0 }' "$positioned_state_path"
}

hyprscroller_wait_for_outer_output_position_events() {
    local positioned_state_path="$1"
    local expected_count="$2"

    for ((attempt = 0; attempt < 100; ++attempt)); do
        if (( "$(hyprscroller_outer_output_position_event_count "$positioned_state_path")" >= expected_count )); then
            return 0
        fi

        sleep 0.05
    done

    return 1
}

hyprscroller_create_positioned_wayland_outputs() {
    local instance="$1"
    local start_count="$2"
    local target_count="$3"
    local wait_monitor_fn="$4"
    local wait_outer_fn="$5"
    local outer_monitor_x="$6"
    local outer_monitor_y="$7"
    local -n titles_ref="$8"
    local -n xs_ref="$9"
    local -n ys_ref="${10}"
    local -n widths_ref="${11}"
    local -n heights_ref="${12}"
    local expected_monitor_count
    local index
    local actual_monitor_count
    local actual_outer_count

    for ((expected_monitor_count = start_count + 1; expected_monitor_count <= target_count; ++expected_monitor_count)); do
        hyprctl -i "$instance" output create wayland >/dev/null

        if ! "$wait_monitor_fn" "$expected_monitor_count"; then
            actual_monitor_count="$(hyprctl -i "$instance" monitors -j 2>/dev/null | jq 'length' 2>/dev/null || printf 'unavailable')"
            printf 'helper: nested monitor count stalled at %s while waiting for %s\n' \
                "$actual_monitor_count" "$expected_monitor_count" >&2
            return 1
        fi

        if [[ -n "$wait_outer_fn" ]] && ! "$wait_outer_fn" "$expected_monitor_count"; then
            actual_outer_count="$(hyprctl clients -j 2>/dev/null | jq '
                map(select(.class == "aquamarine" and (.title | test("^aquamarine - WAYLAND-[0-9]+$"))))
                | length
            ' 2>/dev/null || printf 'unavailable')"
            printf 'helper: outer aquamarine window count stalled at %s while waiting for %s\n' \
                "$actual_outer_count" "$expected_monitor_count" >&2
            return 1
        fi

        index=$(( expected_monitor_count - 1 ))
        hyprscroller_position_outer_window_index \
            "$outer_monitor_x" \
            "$outer_monitor_y" \
            "$index" \
            titles_ref \
            xs_ref \
            ys_ref \
            widths_ref \
            heights_ref \
            || return 1
    done
}

hyprscroller_position_outer_windows() {
    local outer_monitor_x="$1"
    local outer_monitor_y="$2"
    local -n titles_ref="$3"
    local -n xs_ref="$4"
    local -n ys_ref="$5"
    local -n widths_ref="$6"
    local -n heights_ref="$7"
    local index

    for index in "${!titles_ref[@]}"; do
        hyprscroller_position_outer_window_index \
            "$outer_monitor_x" \
            "$outer_monitor_y" \
            "$index" \
            titles_ref \
            xs_ref \
            ys_ref \
            widths_ref \
            heights_ref \
            || return 1
    done
}

hyprscroller_apply_five_monitor_cross_layout() {
    local instance="$1"
    local landscape_width="$2"
    local landscape_height="$3"
    local portrait_width="$landscape_height"
    local portrait_height="$landscape_width"
    local row_gap=$(( landscape_height / 4 ))
    local middle_x="$portrait_width"
    local middle_y=$(( landscape_height + row_gap ))
    local side_y=$(( middle_y + (landscape_height / 2) - (portrait_height / 2) ))
    local right_x=$(( portrait_width + landscape_width ))
    local bottom_y=$(( middle_y + landscape_height + row_gap ))

    hyprctl -i "$instance" keyword monitor \
        "WAYLAND-1,${landscape_width}x${landscape_height}@60,0x${side_y},1,transform,3" >/dev/null
    hyprctl -i "$instance" keyword monitor \
        "WAYLAND-2,${landscape_width}x${landscape_height}@60,${middle_x}x0,1,transform,0" >/dev/null
    hyprctl -i "$instance" keyword monitor \
        "WAYLAND-3,${landscape_width}x${landscape_height}@60,${middle_x}x${middle_y},1,transform,0" >/dev/null
    hyprctl -i "$instance" keyword monitor \
        "WAYLAND-4,${landscape_width}x${landscape_height}@60,${right_x}x${side_y},1,transform,1" >/dev/null
    hyprctl -i "$instance" keyword monitor \
        "WAYLAND-5,${landscape_width}x${landscape_height}@60,${middle_x}x${bottom_y},1,transform,0" >/dev/null
}
