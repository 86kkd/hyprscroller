/**
 * @file pass_element.h
 * @brief Dedicated overview pass element that owns a full monitor overview draw.
 */
#pragma once

#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

namespace Overview {

class OverviewPassElement : public IPassElement {
  public:
    explicit OverviewPassElement(PHLMONITOR monitor);
    ~OverviewPassElement() override = default;

    void                draw(const CRegion& damage) override;
    bool                needsLiveBlur() override;
    bool                needsPrecomputeBlur() override;
    std::optional<CBox> boundingBox() override;
    CRegion             opaqueRegion() override;

    const char*         passName() override {
        return "OverviewPassElement";
    }

  private:
    PHLMONITOR monitor_;
};

} // namespace Overview
