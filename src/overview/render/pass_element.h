/**
 * @file overview/render/pass_element.h
 * @brief Dedicated overview pass element that owns a full monitor overview draw.
 */
#pragma once

#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

namespace Overview {

class OverviewPassElement : public IPassElement {
  public:
    explicit OverviewPassElement(PHLMONITOR monitor);
    ~OverviewPassElement() override = default;

    std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override;
    bool                needsLiveBlur(Render::CRenderContext& ctx) override;
    bool                needsPrecomputeBlur(Render::CRenderContext& ctx) override;
    ePassElementType    type() override;
    std::optional<CBox> boundingBox(Render::CRenderContext& ctx) override;
    CRegion             opaqueRegion(Render::CRenderContext& ctx) override;

    const char*         passName() override {
        return "OverviewPassElement";
    }

  private:
    PHLMONITOR monitor_;
};

} // namespace Overview
