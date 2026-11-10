#include "shell/render/liquid_glass_node.h"

#include "render/backend/render_backend.h"
#include "shell/render/programs/liquid_glass_program.h"

void LiquidGlassNode::draw(RenderBackend& backend, const CustomDrawContext& context) const {
  auto& program = backend.extension<LiquidGlassProgram>();
  program.ensureInitialized();
  program.draw(
      backend,
      LiquidGlassProgram::Params{
          .surfaceWidth = context.surfaceWidth,
          .surfaceHeight = context.surfaceHeight,
          .pixelScaleX = context.pixelScaleX,
          .pixelScaleY = context.pixelScaleY,
          .width = context.width,
          .height = context.height,
          .radius = m_radius,
          .opacity = context.opacity,
          .tint = rgba(m_tint.r * m_tint.a, m_tint.g * m_tint.a, m_tint.b * m_tint.a, m_tint.a),
          .frostRadius = m_frostRadius,
          .transform = context.transform,
      }
  );
}
