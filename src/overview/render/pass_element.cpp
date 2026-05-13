/**
 * @file overview/render/pass_element.cpp
 * @brief Dedicated pass element wrapper for overview rendering.
 *
 * Hyprland render passes operate on pass elements. Overview contributes one
 * custom pass element per monitor so the compositor can ask the plugin to draw
 * its overlay as part of the normal monitor render pipeline.
 */
#include "overview/render/pass_element.h"

#include "overview/render/render.h"

namespace Overview {

OverviewPassElement::OverviewPassElement(PHLMONITOR monitor) : monitor_(monitor) {
}

std::vector<UP<IPassElement>> OverviewPassElement::draw() {
    // The pass element itself owns no draw logic; it simply forwards to the
    // overview renderer using the monitor it was created for.
    fullRenderMonitor(monitor_);
    return {};
}

bool OverviewPassElement::needsLiveBlur() {
    // Overview draws its own complete overlay and does not require Hyprland to
    // perform live blur sampling behind this pass element.
    return false;
}

bool OverviewPassElement::needsPrecomputeBlur() {
    // Same reasoning as above: no precomputed blur input is needed either.
    return false;
}

ePassElementType OverviewPassElement::type() {
    return EK_CUSTOM;
}

std::optional<CBox> OverviewPassElement::boundingBox() {
    if (!monitor_)
        return std::nullopt;

    // The pass element covers the full monitor because overview composes a
    // monitor-wide scene, not a small sub-rectangle.
    return CBox{{}, monitor_->m_size};
}

CRegion OverviewPassElement::opaqueRegion() {
    // Returning an empty opaque region keeps compositor assumptions conservative:
    // overview should not claim it fully occludes underlying content.
    return {};
}

} // namespace Overview
