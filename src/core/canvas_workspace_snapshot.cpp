#include "canvas_workspace_snapshot.h"

#include <charconv>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ScrollerCanvasSnapshot {
namespace {

template <typename T>
bool parse_token(std::string_view token, T& out);

template <>
bool parse_token<int>(std::string_view token, int& out) {
    const auto* begin = token.data();
    const auto* end = token.data() + token.size();
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

bool parse_bool_token(std::string_view token, bool& out) {
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
            tokens.push_back(line.substr(first));
            break;
        }
        tokens.push_back(line.substr(first, last - first));
        start = last + 1;
    }
    return tokens;
}

} // namespace

std::string serialize_repository(const RepositorySnapshot& snapshot) {
    std::ostringstream out;
    out << "VERSION " << snapshot.version << '\n';
    out << "ACTIVE " << snapshot.activeCanvasId << '\n';
    for (const auto& canvas : snapshot.canvases) {
        out << "CANVAS " << canvas.canvasId << ' ' << canvas.tileX << ' ' << canvas.tileY << ' ' << canvas.members.size() << '\n';
        for (const auto& member : canvas.members)
            out << "MEMBER " << member.monitorId << ' ' << member.workspaceId << ' ' << (member.special ? 1 : 0) << '\n';
    }
    return out.str();
}

std::optional<RepositorySnapshot> deserialize_repository(std::string_view data) {
    RepositorySnapshot snapshot;
    std::istringstream input{std::string(data)};
    std::string line;
    auto* currentCanvas = static_cast<CanvasWorkspaceSnapshot*>(nullptr);
    bool sawVersion = false;
    bool sawActive = false;

    while (std::getline(input, line)) {
        const auto tokens = split_tokens(line);
        if (tokens.empty())
            continue;

        if (tokens[0] == "VERSION") {
            if (tokens.size() != 2 || !parse_token(tokens[1], snapshot.version) || snapshot.version != kFormatVersion)
                return std::nullopt;
            sawVersion = true;
            continue;
        }

        if (!sawVersion)
            return std::nullopt;

        if (tokens[0] == "ACTIVE") {
            if (tokens.size() != 2 || !parse_token(tokens[1], snapshot.activeCanvasId))
                return std::nullopt;
            sawActive = true;
            continue;
        }

        if (tokens[0] == "CANVAS") {
            CanvasWorkspaceSnapshot canvas;
            int memberCount = 0;
            if (tokens.size() != 5 ||
                !parse_token(tokens[1], canvas.canvasId) ||
                !parse_token(tokens[2], canvas.tileX) ||
                !parse_token(tokens[3], canvas.tileY) ||
                !parse_token(tokens[4], memberCount) ||
                memberCount < 0)
                return std::nullopt;
            canvas.members.reserve(static_cast<size_t>(memberCount));
            snapshot.canvases.push_back(std::move(canvas));
            currentCanvas = &snapshot.canvases.back();
            continue;
        }

        if (tokens[0] == "MEMBER") {
            if (!currentCanvas)
                return std::nullopt;

            MonitorMemberSnapshot member;
            if (tokens.size() != 4 ||
                !parse_token(tokens[1], member.monitorId) ||
                !parse_token(tokens[2], member.workspaceId) ||
                !parse_bool_token(tokens[3], member.special))
                return std::nullopt;
            currentCanvas->members.push_back(std::move(member));
            continue;
        }

        return std::nullopt;
    }

    if (!sawVersion || !sawActive)
        return std::nullopt;

    return snapshot;
}

} // namespace ScrollerCanvasSnapshot
