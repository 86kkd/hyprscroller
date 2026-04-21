/**
 * @file dispatch_logic.cpp
 * @brief Pure dispatcher-validation helpers shared by canvas runtime code and tests.
 *
 * This file is intentionally narrow: it does not talk to Hyprland globals
 * directly and it does not decide what command should run. Its job is only to
 * answer "is this dispatcher request well-formed and invokable?" so the rest of
 * the canvas layer can reuse one validation policy in both production code and
 * pure tests.
 */
#include "dispatch_logic.h"

#include <spdlog/spdlog.h>

namespace {
// Pick the string that should appear in warnings when validation fails.
const char *dispatcher_context(const char *context, const char *dispatcher) {
    if (context && context[0] != '\0')
        return context;
    if (dispatcher && dispatcher[0] != '\0')
        return dispatcher;
    return "dispatcher";
}

// Validate the request shape before any caller reaches out to Hyprland's
// dispatcher registry. Keeping this logic here avoids duplicating the same
// missing-name / empty-arg / registry-unavailable checks at every call site.
bool validate_dispatch_request(const CanvasLayoutInternal::DispatcherRegistryRuntime &runtime, const char *dispatcher,
                               std::string_view arg, const char *context) {
    const auto *ctx = dispatcher_context(context, dispatcher);
    if (!dispatcher || dispatcher[0] == '\0') {
        spdlog::warn("{}: missing dispatcher name", ctx);
        return false;
    }

    if (arg.empty()) {
        spdlog::warn("{}: empty dispatcher arg dispatcher={}", ctx, dispatcher);
        return false;
    }

    if (!runtime.hasDispatcherRegistry()) {
        spdlog::warn("{}: keybind manager unavailable dispatcher={}", ctx, dispatcher);
        return false;
    }

    if (!runtime.hasDispatcher(dispatcher)) {
        spdlog::warn("{}: dispatcher not found dispatcher={}", ctx, dispatcher);
        return false;
    }

    return true;
}
} // namespace

namespace CanvasLayoutInternal {

bool can_invoke_dispatcher(const DispatcherRegistryRuntime &runtime, const char *dispatcher, std::string_view arg,
                           const char *context) {
    return validate_dispatch_request(runtime, dispatcher, arg, context);
}

bool invoke_dispatcher(const DispatcherRegistryRuntime &runtime, const char *dispatcher, std::string_view arg,
                       const char *context) {
    // `invoke_dispatcher` is intentionally just "validate, then delegate".
    // Route selection stays in higher layers; this helper only centralizes the
    // final defensive checks around registry access.
    if (!validate_dispatch_request(runtime, dispatcher, arg, context))
        return false;

    return runtime.invokeDispatcher(dispatcher, arg);
}

} // namespace CanvasLayoutInternal
