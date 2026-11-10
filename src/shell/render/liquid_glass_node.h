#pragma once

#include "render/core/color.h"
#include "render/scene/custom_draw_node.h"

// Liquid glass over what the scene has already drawn under this node's rounded rectangle.
// Put it right before the panel it backs, in the same parent and z-index.
class LiquidGlassNode : public CustomDrawNode {
public:
  void setRadius(float radius) {
    if (m_radius == radius) {
      return;
    }
    m_radius = radius;
    markPaintDirty();
  }

  // Straight-alpha colour laid over the glass body.
  void setTint(const Color& tint) {
    if (m_tint == tint) {
      return;
    }
    m_tint = tint;
    markPaintDirty();
  }

  void setFrostRadius(float radius) {
    if (m_frostRadius == radius) {
      return;
    }
    m_frostRadius = radius;
    markPaintDirty();
  }

  void draw(RenderBackend& backend, const CustomDrawContext& context) const override;

private:
  float m_radius = 0.0F;
  float m_frostRadius = 10.0F;
  Color m_tint = rgba(0.0F, 0.0F, 0.0F, 0.0F);
};
