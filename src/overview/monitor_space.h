/**
 * @file monitor_space.h
 * @brief Monitor orientation and coordinate-space helpers for overview render.
 */
#pragma once

#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "../core/types.h"
#include "orientation_math.h"

namespace Overview {

using Hyprutils::Math::CBox;
using Hyprutils::Math::Vector2D;

struct MonitorSpace {
    PHLMONITOR          monitor = nullptr;
    Vector2D            origin;
    Vector2D            logicalSize;
    Vector2D            pixelSize;
    Vector2D            transformedSize;
    wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
    double              scale = 1.0;

    static MonitorSpace fromMonitor(PHLMONITOR monitor);
    static MonitorSpace uprightCapture(PHLMONITOR monitor);

    MonitorOrientation  orientation() const;
    bool                isPortrait() const;
    bool                isLandscape() const;
    Vector2D            renderSize() const;

    CBox                toLocalBox(const ScrollerCore::Box& box) const;
    CBox                transformLocalToRender(const CBox& box) const;
    CBox                toRenderBox(const ScrollerCore::Box& box) const;
    CBox                captureSourceBox(const Vector2D& captureSize, const Vector2D& framebufferSize) const;
};

class ScopedUprightMonitorCapture {
  public:
    explicit ScopedUprightMonitorCapture(PHLMONITOR monitor);
    ~ScopedUprightMonitorCapture();

    ScopedUprightMonitorCapture(const ScopedUprightMonitorCapture&) = delete;
    ScopedUprightMonitorCapture& operator=(const ScopedUprightMonitorCapture&) = delete;

  private:
    PHLMONITOR          monitor_ = nullptr;
    wl_output_transform previousTransform_ = WL_OUTPUT_TRANSFORM_NORMAL;
    Vector2D            previousTransformedSize_;
};

} // namespace Overview
