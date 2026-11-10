#pragma once

#include "render/backend/render_backend.h"
#include "render/backend/render_extension.h"
#include "render/core/mat3.h"
#include "render/core/shader_program.h"

#include <GLES2/gl2.h>
#include <array>
#include <cstdint>
#include <memory>

// Liquid glass over whatever is already drawn under a rounded rectangle: copies that region
// of the bound framebuffer, frosts the copy, and draws it back bent at the rim (a port of the
// Umbriel window backdrop shader in /etc/nixos/home/umbriel/effects/liquid-glass-backdrop.glsl).
class LiquidGlassProgram final : public RenderExtension {
public:
  struct Params {
    float surfaceWidth = 0.0F;
    float surfaceHeight = 0.0F;
    float pixelScaleX = 1.0F;
    float pixelScaleY = 1.0F;
    float width = 0.0F;
    float height = 0.0F;
    float radius = 0.0F;
    float opacity = 1.0F;
    // Premultiplied colour laid over the glass body.
    Color tint = rgba(0.0F, 0.0F, 0.0F, 0.0F);
    // Frost strength, logical px.
    float frostRadius = 10.0F;
    Mat3 transform = Mat3::identity();
  };

  void ensureInitialized();
  void destroy() override;
  void abandon() noexcept override;

  void draw(RenderBackend& backend, const Params& params);

private:
  struct Buffers {
    std::unique_ptr<RenderFramebuffer> raw;
    std::unique_ptr<RenderFramebuffer> scratch;
    std::unique_ptr<RenderFramebuffer> frost;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t lastUse = 0;
  };

  // A few panels of different sizes can share the program without reallocating every frame.
  Buffers* ensureBuffers(RenderBackend& backend, std::uint32_t width, std::uint32_t height);

  ShaderProgram m_program;
  GLint m_positionLocation = -1;
  GLint m_surfaceSizeLocation = -1;
  GLint m_sizeLocation = -1;
  GLint m_transformLocation = -1;
  GLint m_rawLocation = -1;
  GLint m_frostLocation = -1;
  GLint m_regionLocation = -1;
  GLint m_scaleLocation = -1;
  GLint m_flipLocation = -1;
  GLint m_radiusLocation = -1;
  GLint m_tintLocation = -1;
  GLint m_opacityLocation = -1;

  std::array<Buffers, 4> m_buffers;
  std::uint64_t m_useCounter = 0;
};
