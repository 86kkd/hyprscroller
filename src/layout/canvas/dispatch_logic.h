#pragma once

#include <string_view>

namespace CanvasLayoutInternal {

struct DispatcherRegistryRuntime {
    virtual ~DispatcherRegistryRuntime() = default;
    virtual bool hasDispatcherRegistry() const = 0;
    virtual bool hasDispatcher(const char *dispatcher) const = 0;
    virtual bool invokeDispatcher(const char *dispatcher, std::string_view arg) const = 0;
};

bool can_invoke_dispatcher(const DispatcherRegistryRuntime &runtime, const char *dispatcher, std::string_view arg, const char *context = nullptr);
bool invoke_dispatcher(const DispatcherRegistryRuntime &runtime, const char *dispatcher, std::string_view arg, const char *context = nullptr);

} // namespace CanvasLayoutInternal
