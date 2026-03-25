#include "dispatch_logic.h"

#include <spdlog/spdlog.h>

namespace {
const char *dispatcher_context(const char *context, const char *dispatcher) {
    if (context && context[0] != '\0')
        return context;
    if (dispatcher && dispatcher[0] != '\0')
        return dispatcher;
    return "dispatcher";
}

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
    if (!validate_dispatch_request(runtime, dispatcher, arg, context))
        return false;

    return runtime.invokeDispatcher(dispatcher, arg);
}

} // namespace CanvasLayoutInternal
