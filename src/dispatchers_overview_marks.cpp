/**
 * @file dispatchers_overview_marks.cpp
 * @brief Overview and mark dispatcher entrypoints and registration.
 */
#include "dispatchers_internal.h"

namespace dispatchers::detail {
namespace {

// toggleoverview: enter or accept the global logical overview session.
void dispatch_toggleoverview(std::string arg) {
    auto& overview = overviewSessionForDispatch();

    if (isOverviewCancelArg(arg)) {
        if (overview.active())
            overview.close(false);
        return;
    }

    if (isOverviewAcceptArg(arg)) {
        if (overview.active())
            overview.close(true);
        else
            overview.open();
        return;
    }

    if (overview.active()) {
        overview.close(true);
        return;
    }

    overview.open();
}

// canceloverview: leave the global overview session without accepting.
void dispatch_canceloverview(std::string arg) {
    (void)arg;
    closeOverviewIfActive(false);
}

// marksadd <name>: save focused window under a named mark.
void dispatch_marksadd(std::string arg) {
    withWorkspaceLayout([&](CanvasLayout& layout, int workspace) {
        (void)workspace;
        layout.marks_add(arg);
    });
}

// marksdelete <name>: remove a stored mark entry by name.
void dispatch_marksdelete(std::string arg) {
    withLayout([&](CanvasLayout& layout) {
        layout.marks_delete(arg);
    });
}

// marksvisit <name>: focus and activate the marked window if present.
void dispatch_marksvisit(std::string arg) {
    withLayout([&](CanvasLayout& layout) {
        layout.marks_visit(arg);
    });
}

// marksreset: clear all marks.
void dispatch_marksreset(std::string arg) {
    (void)arg;
    withLayout([&](CanvasLayout& layout) {
        layout.marks_reset();
    });
}

} // namespace

void registerOverviewMarkDispatchers() {
    registerDispatcher("scroller:toggleoverview", dispatch_toggleoverview);
    registerDispatcher("scroller:canceloverview", dispatch_canceloverview);
    registerDispatcher("scroller:marksadd", dispatch_marksadd);
    registerDispatcher("scroller:marksdelete", dispatch_marksdelete);
    registerDispatcher("scroller:marksvisit", dispatch_marksvisit);
    registerDispatcher("scroller:marksreset", dispatch_marksreset);
}

} // namespace dispatchers::detail
