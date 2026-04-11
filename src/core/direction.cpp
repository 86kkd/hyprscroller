#include "direction.h"

namespace ScrollerCore {

const char *direction_name(Direction direction) {
    switch (direction) {
    case Direction::Left:
        return "left";
    case Direction::Right:
        return "right";
    case Direction::Up:
        return "up";
    case Direction::Down:
        return "down";
    case Direction::Begin:
        return "begin";
    case Direction::End:
        return "end";
    case Direction::Center:
        return "center";
    default:
        return "unknown";
    }
}

const char *direction_dispatch_arg(Direction direction) {
    switch (direction) {
    case Direction::Left:
        return "l";
    case Direction::Right:
        return "r";
    case Direction::Up:
        return "u";
    case Direction::Down:
        return "d";
    case Direction::Begin:
        return "b";
    case Direction::End:
        return "e";
    default:
        return nullptr;
    }
}

Direction opposite_direction(Direction direction) {
    switch (direction) {
    case Direction::Left:
        return Direction::Right;
    case Direction::Right:
        return Direction::Left;
    case Direction::Up:
        return Direction::Down;
    case Direction::Down:
        return Direction::Up;
    default:
        return direction;
    }
}

std::optional<Direction> parse_direction_arg(std::string_view arg) {
    if (arg == "l" || arg == "left")
        return Direction::Left;
    if (arg == "r" || arg == "right")
        return Direction::Right;
    if (arg == "u" || arg == "up")
        return Direction::Up;
    if (arg == "d" || arg == "dn" || arg == "down")
        return Direction::Down;
    if (arg == "b" || arg == "begin" || arg == "beginning")
        return Direction::Begin;
    if (arg == "e" || arg == "end")
        return Direction::End;
    if (arg == "c" || arg == "center" || arg == "centre")
        return Direction::Center;
    return std::nullopt;
}

std::optional<FitSize> parse_fit_size_arg(std::string_view arg) {
    if (arg == "active")
        return FitSize::Active;
    if (arg == "visible")
        return FitSize::Visible;
    if (arg == "all")
        return FitSize::All;
    if (arg == "toend")
        return FitSize::ToEnd;
    if (arg == "tobeg" || arg == "tobeginning")
        return FitSize::ToBeg;
    return std::nullopt;
}

std::optional<Mode> parse_mode_arg(std::string_view arg) {
    if (arg == "r" || arg == "row")
        return Mode::Row;
    if (arg == "c" || arg == "col" || arg == "column")
        return Mode::Column;
    return std::nullopt;
}

} // namespace ScrollerCore
