/**
 * @file monitor_space.cpp
 * @brief Monitor orientation and coordinate-space helpers for overview render.
 */
#include "monitor_space.h"

#include <hyprland/src/helpers/Monitor.hpp>

namespace Overview {

MonitorSpace MonitorSpace::fromMonitor(PHLMONITOR monitor) {
    MonitorSpace space;
    if (!monitor)
        return space;

    space.monitor = monitor;
    space.origin = monitor->m_position;
    space.logicalSize = monitor->m_size;
    space.pixelSize = monitor->m_pixelSize;
    space.transformedSize = monitor->m_transformedSize;
    space.transform = monitor->m_transform;
    space.scale = monitor->m_scale;
    return space;
}

MonitorSpace MonitorSpace::uprightCapture(PHLMONITOR monitor) {
    auto space = fromMonitor(monitor);
    space.transform = WL_OUTPUT_TRANSFORM_NORMAL;
    space.transformedSize = space.pixelSize;
    return space;
}

MonitorOrientation MonitorSpace::orientation() const {
    return orientation_for_transform(transform);
}

bool MonitorSpace::isPortrait() const {
    return orientation() == MonitorOrientation::Portrait;
}

bool MonitorSpace::isLandscape() const {
    return orientation() == MonitorOrientation::Landscape;
}

Vector2D MonitorSpace::renderSize() const {
    return pixelSize;
}

CBox MonitorSpace::toLocalBox(const ScrollerCore::Box& box) const {
    return {
        box.x - origin.x,
        box.y - origin.y,
        box.w,
        box.h,
    };
}

CBox MonitorSpace::transformLocalToRender(const CBox& box) const {
    const auto transformed = transform_box_to_render_space({box.x, box.y, box.w, box.h}, transform, renderSize().x, renderSize().y);
    return {transformed.x, transformed.y, transformed.w, transformed.h};
}

CBox MonitorSpace::toRenderBox(const ScrollerCore::Box& box) const {
    return transformLocalToRender(toLocalBox(box));
}

CBox MonitorSpace::captureSourceBox(const Vector2D& captureSize, const Vector2D& framebufferSize) const {
    (void)framebufferSize;

    CBox sourceBox{0, 0, captureSize.x, captureSize.y};
    sourceBox.round();
    return sourceBox;
}

ScopedUprightMonitorCapture::ScopedUprightMonitorCapture(PHLMONITOR monitor)
    : monitor_(monitor) {
    if (!monitor_)
        return;

    previousTransform_ = monitor_->m_transform;
    previousTransformedSize_ = monitor_->m_transformedSize;

    const auto captureSpace = MonitorSpace::uprightCapture(monitor_);
    monitor_->m_transform = captureSpace.transform;
    monitor_->m_transformedSize = captureSpace.transformedSize;
}

ScopedUprightMonitorCapture::~ScopedUprightMonitorCapture() {
    if (!monitor_)
        return;

    monitor_->m_transform = previousTransform_;
    monitor_->m_transformedSize = previousTransformedSize_;
}

} // namespace Overview
