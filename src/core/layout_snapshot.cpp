#include "layout_snapshot.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ScrollerSnapshot {
namespace {

template <typename T>
bool parse_token(std::string_view token, T &out);

template <>
bool parse_token<unsigned long long>(std::string_view token, unsigned long long &out) {
    const auto *begin = token.data();
    const auto *end = token.data() + token.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

bool parse_size_token(std::string_view token, size_t &out) {
    unsigned long long value = 0;
    if (!parse_token(token, value))
        return false;
    out = static_cast<size_t>(value);
    return true;
}

bool parse_key_token(std::string_view token, uintptr_t &out) {
    unsigned long long value = 0;
    if (!parse_token(token, value))
        return false;
    out = static_cast<uintptr_t>(value);
    return true;
}

template <>
bool parse_token<int>(std::string_view token, int &out) {
    const auto *begin = token.data();
    const auto *end = token.data() + token.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

template <>
bool parse_token<double>(std::string_view token, double &out) {
    std::string owned(token);
    char *end = nullptr;
    out = std::strtod(owned.c_str(), &end);
    return end == owned.c_str() + owned.size();
}

bool parse_bool_token(std::string_view token, bool &out) {
    int value = 0;
    if (!parse_token(token, value))
        return false;
    if (value != 0 && value != 1)
        return false;
    out = value != 0;
    return true;
}

std::vector<std::string_view> split_tokens(std::string_view line) {
    std::vector<std::string_view> tokens;
    size_t start = 0;
    while (start < line.size()) {
        const auto first = line.find_first_not_of(" \t\r", start);
        if (first == std::string_view::npos)
            break;
        const auto last = line.find_first_of(" \t\r", first);
        if (last == std::string_view::npos) {
            tokens.emplace_back(line.substr(first));
            break;
        }
        tokens.emplace_back(line.substr(first, last - first));
        start = last + 1;
    }
    return tokens;
}

void append_box(std::ostringstream &out, const ScrollerCore::Box &box) {
    out << box.x << ' ' << box.y << ' ' << box.w << ' ' << box.h;
}

bool parse_box(const std::vector<std::string_view> &tokens, size_t index, ScrollerCore::Box &box) {
    return index + 3 < tokens.size() &&
           parse_token(tokens[index + 0], box.x) &&
           parse_token(tokens[index + 1], box.y) &&
           parse_token(tokens[index + 2], box.w) &&
           parse_token(tokens[index + 3], box.h);
}

} // namespace

std::string serialize_repository(const RepositorySnapshot &snapshots) {
    std::ostringstream out;
    out << "VERSION " << kFormatVersion << '\n';

    std::vector<int> workspaceIds;
    workspaceIds.reserve(snapshots.size());
    for (const auto &[workspaceId, _] : snapshots)
        workspaceIds.push_back(workspaceId);
    std::sort(workspaceIds.begin(), workspaceIds.end());

    for (const auto workspaceId : workspaceIds) {
        const auto it = snapshots.find(workspaceId);
        if (it == snapshots.end())
            continue;

        const auto &canvas = it->second;
        out << "WORKSPACE " << canvas.workspaceId << ' ' << canvas.activeLaneIndex << ' ' << canvas.lanes.size() << '\n';
        for (const auto &lane : canvas.lanes) {
            out << "LANE " << lane.mode << ' ' << lane.reorder << ' ' << (lane.ephemeral ? 1 : 0) << ' '
                << lane.activeStackIndex << ' ' << lane.stacks.size() << '\n';
            for (const auto &stack : lane.stacks) {
                out << "STACK " << stack.width << ' ' << stack.reorder << ' '
                    << (stack.fullscreened ? 1 : 0) << ' ' << (stack.maximized ? 1 : 0) << ' ';
                append_box(out, stack.geom);
                out << ' ';
                append_box(out, stack.memGeom);
                out << ' ' << stack.activeWindowKey << ' ' << stack.windows.size() << '\n';
                for (const auto &window : stack.windows) {
                    out << "WINDOW " << window.key << ' ' << window.heightMode << ' '
                        << window.geomY << ' ' << window.geomH << ' '
                        << window.memY << ' ' << window.memH << '\n';
                }
            }
        }
    }

    return out.str();
}

std::optional<RepositorySnapshot> deserialize_repository(std::string_view data) {
    RepositorySnapshot snapshots;
    std::istringstream input{std::string(data)};
    std::string line;

    bool sawVersion = false;
    CanvasSnapshot *currentCanvas = nullptr;
    LaneSnapshot *currentLane = nullptr;
    StackSnapshot *currentStack = nullptr;

    while (std::getline(input, line)) {
        const auto tokens = split_tokens(line);
        if (tokens.empty())
            continue;

        if (tokens[0] == "VERSION") {
            int version = 0;
            if (tokens.size() != 2 || !parse_token(tokens[1], version) || version != kFormatVersion)
                return std::nullopt;
            sawVersion = true;
            continue;
        }

        if (!sawVersion)
            return std::nullopt;

        if (tokens[0] == "WORKSPACE") {
            CanvasSnapshot canvas;
            size_t laneCount = 0;
            if (tokens.size() != 4 ||
                !parse_token(tokens[1], canvas.workspaceId) ||
                !parse_size_token(tokens[2], canvas.activeLaneIndex) ||
                !parse_size_token(tokens[3], laneCount))
                return std::nullopt;
            canvas.lanes.reserve(laneCount);
            currentCanvas = &snapshots[canvas.workspaceId];
            *currentCanvas = std::move(canvas);
            currentLane = nullptr;
            currentStack = nullptr;
            continue;
        }

        if (tokens[0] == "LANE") {
            if (!currentCanvas)
                return std::nullopt;

            LaneSnapshot lane;
            size_t stackCount = 0;
            if (tokens.size() != 6 ||
                !parse_token(tokens[1], lane.mode) ||
                !parse_token(tokens[2], lane.reorder) ||
                !parse_bool_token(tokens[3], lane.ephemeral) ||
                !parse_size_token(tokens[4], lane.activeStackIndex) ||
                !parse_size_token(tokens[5], stackCount))
                return std::nullopt;
            lane.stacks.reserve(stackCount);
            currentCanvas->lanes.push_back(std::move(lane));
            currentLane = &currentCanvas->lanes.back();
            currentStack = nullptr;
            continue;
        }

        if (tokens[0] == "STACK") {
            if (!currentLane)
                return std::nullopt;

            StackSnapshot stack;
            size_t windowCount = 0;
            if (tokens.size() != 15 ||
                !parse_token(tokens[1], stack.width) ||
                !parse_token(tokens[2], stack.reorder) ||
                !parse_bool_token(tokens[3], stack.fullscreened) ||
                !parse_bool_token(tokens[4], stack.maximized) ||
                !parse_box(tokens, 5, stack.geom) ||
                !parse_box(tokens, 9, stack.memGeom) ||
                !parse_key_token(tokens[13], stack.activeWindowKey) ||
                !parse_size_token(tokens[14], windowCount))
                return std::nullopt;
            stack.windows.reserve(windowCount);
            currentLane->stacks.push_back(std::move(stack));
            currentStack = &currentLane->stacks.back();
            continue;
        }

        if (tokens[0] == "WINDOW") {
            if (!currentStack)
                return std::nullopt;

            WindowSnapshot window;
            if (tokens.size() != 7 ||
                !parse_key_token(tokens[1], window.key) ||
                !parse_token(tokens[2], window.heightMode) ||
                !parse_token(tokens[3], window.geomY) ||
                !parse_token(tokens[4], window.geomH) ||
                !parse_token(tokens[5], window.memY) ||
                !parse_token(tokens[6], window.memH))
                return std::nullopt;
            currentStack->windows.push_back(std::move(window));
            continue;
        }

        return std::nullopt;
    }

    if (!sawVersion)
        return std::nullopt;

    return snapshots;
}

} // namespace ScrollerSnapshot
