/**
 * @file pass_element.cpp
 * @brief Dedicated pass element wrapper for overview rendering.
 */
#include "pass_element.h"

#include "render.h"

namespace Overview {

OverviewPassElement::OverviewPassElement(PHLMONITOR monitor) : monitor_(monitor) {
}

void OverviewPassElement::draw(const CRegion&) {
    fullRenderMonitor(monitor_);
}

bool OverviewPassElement::needsLiveBlur() {
    return false;
}

bool OverviewPassElement::needsPrecomputeBlur() {
    return false;
}

std::optional<CBox> OverviewPassElement::boundingBox() {
    if (!monitor_)
        return std::nullopt;

    return CBox{{}, monitor_->m_size};
}

CRegion OverviewPassElement::opaqueRegion() {
    if (!monitor_)
        return {};

    return CRegion{CBox{{}, monitor_->m_size}};
}

} // namespace Overview
