#!/usr/bin/env bash
# Local Hyprland layout-switch helper tracked in this repository for reference.
# Source path on the maintainer machine:
#   ~/.config/hypr/scripts/ChangeLayout.sh

set -euo pipefail

notif="$HOME/.config/swaync/images/ja.png"
current_layout=$(hyprctl -j getoption general:layout | jq -r '.str')
show_notification=true

if [ "${1:-}" = "init" ]; then
  show_notification=false
fi

set_focus_binds() {
  local layout="${1:-}"

  hyprctl keyword unbind SUPER,h || true
  hyprctl keyword unbind SUPER,j || true
  hyprctl keyword unbind SUPER,k || true
  hyprctl keyword unbind SUPER,l || true
  hyprctl keyword unbind SUPER,n || true
  hyprctl keyword unbind SUPER,m || true
  hyprctl keyword unbind SUPER,comma || true
  hyprctl keyword unbind SUPER,period || true
  hyprctl keyword unbind SUPER,F || true
  hyprctl keyword unbind SUPER,I || true

  case "$layout" in
    scroller)
      hyprctl keyword bindd "SUPER,h,focus left (scroller),scroller:movefocus,l"
      hyprctl keyword bindd "SUPER,j,focus down (scroller),scroller:movefocus,d"
      hyprctl keyword bindd "SUPER,k,focus up (scroller),scroller:movefocus,u"
      hyprctl keyword bindd "SUPER,l,focus right (scroller),scroller:movefocus,r"
      hyprctl keyword bindd "SUPER,n,monitor left (scroller),scroller:focusmonitor,l"
      hyprctl keyword bindd "SUPER,m,monitor down (scroller),scroller:focusmonitor,d"
      hyprctl keyword bindd "SUPER,comma,monitor up (scroller),scroller:focusmonitor,u"
      hyprctl keyword bindd "SUPER,period,monitor right (scroller),scroller:focusmonitor,r"
      hyprctl keyword bindd "SUPER,F,fullscreen (scroller),scroller:togglefullscreen"
      hyprctl keyword bindd "SUPER,I,overview (scroller),scroller:toggleoverview"
      ;;
    *)
      hyprctl keyword bindd "SUPER,h,focus left,movefocus,l"
      hyprctl keyword bindd "SUPER,j,focus down,movefocus,d"
      hyprctl keyword bindd "SUPER,k,focus up,movefocus,u"
      hyprctl keyword bindd "SUPER,l,focus right,movefocus,r"
      hyprctl keyword bindd "SUPER,F,special fullscreenstate,fullscreenstate,2 0"
      hyprctl keyword bindd "SUPER,I,add master,layoutmsg,addmaster"
      ;;
  esac
}

set_move_binds() {
  local layout="${1:-}"

  hyprctl keyword unbind "SUPER CTRL,h" || true
  hyprctl keyword unbind "SUPER CTRL,j" || true
  hyprctl keyword unbind "SUPER CTRL,k" || true
  hyprctl keyword unbind "SUPER CTRL,l" || true

  case "$layout" in
    scroller)
      hyprctl keyword bindd "SUPER CTRL,h,move window left (scroller),scroller:movewindow,l"
      hyprctl keyword bindd "SUPER CTRL,j,move window down (scroller),scroller:movewindow,d"
      hyprctl keyword bindd "SUPER CTRL,k,move window up (scroller),scroller:movewindow,u"
      hyprctl keyword bindd "SUPER CTRL,l,move window right (scroller),scroller:movewindow,r"
      ;;
    *)
      hyprctl keyword bindd "SUPER CTRL,h,move window left,movewindow,l"
      hyprctl keyword bindd "SUPER CTRL,j,move window down,movewindow,d"
      hyprctl keyword bindd "SUPER CTRL,k,move window up,movewindow,u"
      hyprctl keyword bindd "SUPER CTRL,l,move window right,movewindow,r"
      ;;
  esac
}

if [ "${1:-}" = "init" ]; then
  target_layout="$current_layout"
else
  case "$current_layout" in
    master) target_layout="dwindle" ;;
    dwindle) target_layout="scroller" ;;
    scroller) target_layout="master" ;;
    *) target_layout="master" ;;
  esac
fi

case "$target_layout" in
  master)
    hyprctl keyword general:layout master
    set_focus_binds master
    set_move_binds master
    if [ "$show_notification" = true ]; then
      notify-send -e -u low -i "$notif" " Master Layout"
    fi
    ;;
  dwindle)
    hyprctl keyword general:layout dwindle
    set_focus_binds dwindle
    set_move_binds dwindle
    if [ "$show_notification" = true ]; then
      notify-send -e -u low -i "$notif" " Dwindle Layout"
    fi
    ;;
  scroller)
    hyprctl keyword general:layout scroller
    set_focus_binds scroller
    set_move_binds scroller
    if [ "$show_notification" = true ]; then
      notify-send -e -u low -i "$notif" " Scroller Layout"
    fi
    ;;
  *)
    set_focus_binds "$target_layout"
    set_move_binds "$target_layout"
    ;;
esac
