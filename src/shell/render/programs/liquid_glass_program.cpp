#include "shell/render/programs/liquid_glass_program.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace {

  constexpr char kVertexShaderSource[] = R"(
precision highp float;

attribute vec2 a_position;
uniform vec2 u_surface_size;
uniform vec2 u_size;
uniform mat3 u_transform;
varying vec2 v_local;

vec2 to_ndc(vec2 pixel_pos) {
    vec2 normalized = pixel_pos / u_surface_size;
    return vec2(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0);
}

void main() {
    vec2 local = a_position * u_size;
    vec3 pixel = u_transform * vec3(local, 1.0);
    v_local = local;
    gl_Position = vec4(to_ndc(pixel.xy), 0.0, 1.0);
}
)";

  // Same glass as Umbriel's liquid-glass-backdrop.glsl: a mirrored fold near the edge (total
  // internal reflection), colour fringe, blur that weakens towards the edge, Apple-like vibrancy
  // and lift, a faint dark-and-light hairline. Distances are logical px; v_local is y-down.
  constexpr char kFragmentShaderSource[] = R"(
precision highp float;

uniform sampler2D u_raw;
uniform sampler2D u_frost;
uniform vec4 u_region;   // copied region: origin and size in framebuffer px (y-up)
uniform vec2 u_scale;    // framebuffer px per logical px
uniform vec2 u_size;     // logical
uniform float u_radius;  // logical
uniform vec4 u_tint;     // premultiplied
uniform float u_opacity;
varying vec2 v_local;

const float FOLD = 11.0;
const float CREASE = 1.5;
const float MAG = 1.2;
const float FADE = 80.0;
const float KERNEL = 5.0;
const float EDGE_BLUR = 2.0;
const float SPLIT = 0.12;
const float MILK = 0.02;
const float LIFT = 1.08;
const float VIBRANCY = 1.25;
const float LINE = 0.10;
const float LINE_TOP = 0.08;
const float OUTLINE = 0.22;

float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// Texture coordinate of the fragment moved by o logical px (y-down).
vec2 at(vec2 o) {
    vec2 frag = gl_FragCoord.xy + vec2(o.x, -o.y) * u_scale;
    return (frag - u_region.xy) / u_region.zw;
}

vec3 graded(vec2 o, float w) {
    vec3 frost = texture2D(u_frost, at(o)).rgb;
    if (w >= 1.0) {
        return frost;
    }
    float rad = mix(EDGE_BLUR, KERNEL, w);
    vec3 acc = texture2D(u_raw, at(o)).rgb;
    for (int i = 0; i < 19; i++) {
        float a = float(i) * 2.39996;
        float rr = rad * sqrt((float(i) + 0.5) / 19.0);
        acc += texture2D(u_raw, at(o + vec2(cos(a), sin(a)) * rr)).rgb;
    }
    return mix(acc / 20.0, frost, smoothstep(0.3, 1.0, w));
}

void main() {
    vec2 halfSize = u_size * 0.5;
    vec2 p = v_local - halfSize;
    float r = min(u_radius, min(halfSize.x, halfSize.y));
    float d = sdRoundBox(p, halfSize, r);
    float coverage = clamp(0.5 - d * max(u_scale.x, u_scale.y), 0.0, 1.0);
    if (coverage <= 0.0) {
        discard;
    }
    float s = max(-d, 0.0);

    float h = 3.0;
    vec2 g = vec2(
        sdRoundBox(p + vec2(h, 0.0), halfSize, r) - sdRoundBox(p - vec2(h, 0.0), halfSize, r),
        sdRoundBox(p + vec2(0.0, h), halfSize, r) - sdRoundBox(p - vec2(0.0, h), halfSize, r));
    vec2 n = length(g) > 0.0 ? normalize(g) : vec2(0.0);

    float fold = min(FOLD, min(halfSize.x, halfSize.y) * 0.25);
    float x = fold - s;
    float k = x > 0.0 ? 1.0 / MAG : 1.0;
    float mirrored = fold + k * x * x / sqrt(x * x + CREASE * CREASE);
    float bend = s - mirrored;

    float fade = min(FADE, min(halfSize.x, halfSize.y));
    float f = 1.0 - clamp(s / fade, 0.0, 1.0);
    float w = 1.0 - f * f;
    vec3 col = vec3(
        graded(n * bend * (1.0 + SPLIT), w).r,
        graded(n * bend, w).g,
        graded(n * bend * (1.0 - SPLIT), w).b);

    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = max(mix(vec3(lum), col, VIBRANCY), 0.0);
    col = mix(col * LIFT, vec3(1.0), MILK);
    col = col * (1.0 - u_tint.a) + u_tint.rgb;

    float up = max(-n.y, 0.0);
    col *= 1.0 - OUTLINE * exp(-pow(s / 0.7, 2.0));
    col += exp(-pow((s - 1.6) / 0.6, 2.0)) * (LINE + LINE_TOP * up);

    float alpha = coverage * u_opacity;
    gl_FragColor = vec4(col * alpha, alpha);
}
)";

  void setSampling(TextureId texture) {
    glBindTexture(GL_TEXTURE_2D, texture.value());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

} // namespace

void LiquidGlassProgram::ensureInitialized() {
  if (m_program.isValid()) {
    return;
  }

  m_program.create(kVertexShaderSource, kFragmentShaderSource);
  const GLuint id = m_program.id();
  m_positionLocation = glGetAttribLocation(id, "a_position");
  m_surfaceSizeLocation = glGetUniformLocation(id, "u_surface_size");
  m_sizeLocation = glGetUniformLocation(id, "u_size");
  m_transformLocation = glGetUniformLocation(id, "u_transform");
  m_rawLocation = glGetUniformLocation(id, "u_raw");
  m_frostLocation = glGetUniformLocation(id, "u_frost");
  m_regionLocation = glGetUniformLocation(id, "u_region");
  m_scaleLocation = glGetUniformLocation(id, "u_scale");
  m_radiusLocation = glGetUniformLocation(id, "u_radius");
  m_tintLocation = glGetUniformLocation(id, "u_tint");
  m_opacityLocation = glGetUniformLocation(id, "u_opacity");

  if (m_positionLocation < 0
      || m_surfaceSizeLocation < 0
      || m_sizeLocation < 0
      || m_transformLocation < 0
      || m_rawLocation < 0
      || m_frostLocation < 0
      || m_regionLocation < 0
      || m_scaleLocation < 0
      || m_radiusLocation < 0
      || m_tintLocation < 0
      || m_opacityLocation < 0) {
    throw std::runtime_error("failed to query liquid-glass shader locations");
  }
}

void LiquidGlassProgram::destroy() {
  m_program.destroy();
  m_buffers = {};
}

void LiquidGlassProgram::abandon() noexcept {
  m_program.abandon();
  for (auto& set : m_buffers) {
    for (auto* fb : {&set.raw, &set.scratch, &set.frost}) {
      if (*fb) {
        (*fb)->abandon();
        fb->reset();
      }
    }
    set = {};
  }
}

LiquidGlassProgram::Buffers*
LiquidGlassProgram::ensureBuffers(RenderBackend& backend, std::uint32_t width, std::uint32_t height) {
  ++m_useCounter;
  Buffers* oldest = &m_buffers[0];
  for (auto& set : m_buffers) {
    if (set.raw && set.width == width && set.height == height) {
      set.lastUse = m_useCounter;
      return &set;
    }
    if (set.lastUse < oldest->lastUse) {
      oldest = &set;
    }
  }
  Buffers& set = *oldest;
  set = {};
  set.raw = backend.createFramebuffer(width, height);
  set.scratch = backend.createFramebuffer(width, height);
  set.frost = backend.createFramebuffer(width, height);
  if (!set.raw || !set.scratch || !set.frost || !set.raw->valid() || !set.scratch->valid() || !set.frost->valid()) {
    set = {};
    return nullptr;
  }
  for (const auto* fb : {set.raw.get(), set.scratch.get(), set.frost.get()}) {
    setSampling(fb->colorTexture());
  }
  set.width = width;
  set.height = height;
  set.lastUse = m_useCounter;
  return &set;
}

void LiquidGlassProgram::draw(RenderBackend& backend, const Params& params) {
  if (!m_program.isValid() || params.width <= 0.0F || params.height <= 0.0F || params.opacity <= 0.0F) {
    return;
  }

  // Everything below must leave GL as the scene renderer had it.
  GLint prevFramebuffer = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFramebuffer);
  std::array<GLint, 4> viewport{};
  glGetIntegerv(GL_VIEWPORT, viewport.data());
  const bool scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
  std::array<GLint, 4> scissorBox{};
  glGetIntegerv(GL_SCISSOR_BOX, scissorBox.data());
  const auto viewportW = static_cast<float>(viewport[2]);
  const auto viewportH = static_cast<float>(viewport[3]);

  // The rectangle in framebuffer px (y-up), padded so the frost and the fold read real pixels.
  const Mat3& m = params.transform;
  const float left = m.m[6];
  const float top = m.m[7];
  const float sx = params.pixelScaleX;
  const float sy = params.pixelScaleY;
  constexpr float kPad = 24.0F;
  const float x0 = std::clamp(std::floor((left - kPad) * sx), 0.0F, viewportW);
  const float x1 = std::clamp(std::ceil((left + params.width + kPad) * sx), 0.0F, viewportW);
  const float yTop = std::clamp(std::floor((top - kPad) * sy), 0.0F, viewportH);
  const float yBottom = std::clamp(std::ceil((top + params.height + kPad) * sy), 0.0F, viewportH);
  const auto regionW = static_cast<std::uint32_t>(x1 - x0);
  const auto regionH = static_cast<std::uint32_t>(yBottom - yTop);
  if (regionW == 0 || regionH == 0) {
    return;
  }
  const float regionX = x0;
  const float regionY = viewportH - yBottom;

  Buffers* buffers = ensureBuffers(backend, regionW, regionH);
  if (buffers == nullptr) {
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFramebuffer));
    return;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFramebuffer));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, buffers->raw->colorTexture().value());
  glCopyTexSubImage2D(
      GL_TEXTURE_2D, 0, 0, 0, static_cast<GLint>(regionX), static_cast<GLint>(regionY), static_cast<GLsizei>(regionW),
      static_cast<GLsizei>(regionH)
  );

  backend.disableScissor();
  backend.setBlendMode(RenderBlendMode::Disabled);
  backend.setViewport(regionW, regionH);
  const float frost = std::clamp(params.frostRadius * std::max(sx, sy), 0.0F, 20.0F);
  TextureId source = buffers->raw->colorTexture();
  for (int round = 0; round < 2; ++round) {
    backend.bindFramebuffer(*buffers->scratch);
    backend.drawFramebufferBlur(source, regionW, regionH, 1.0F, 0.0F, frost);
    backend.bindFramebuffer(*buffers->frost);
    backend.drawFramebufferBlur(buffers->scratch->colorTexture(), regionW, regionH, 0.0F, 1.0F, frost);
    source = buffers->frost->colorTexture();
  }

  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFramebuffer));
  backend.setViewport(static_cast<std::uint32_t>(viewport[2]), static_cast<std::uint32_t>(viewport[3]));
  backend.setBlendMode(RenderBlendMode::PremultipliedAlpha);
  if (scissorWasEnabled) {
    backend.setScissor(RenderScissor{scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]});
  }

  const std::array<GLfloat, 12> vertices = {
      0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F, 1.0F,
  };

  glUseProgram(m_program.id());
  glUniform2f(m_surfaceSizeLocation, params.surfaceWidth, params.surfaceHeight);
  glUniform2f(m_sizeLocation, params.width, params.height);
  glUniformMatrix3fv(m_transformLocation, 1, GL_FALSE, params.transform.m.data());
  glUniform4f(m_regionLocation, regionX, regionY, static_cast<float>(regionW), static_cast<float>(regionH));
  glUniform2f(m_scaleLocation, sx, sy);
  glUniform1f(m_radiusLocation, params.radius);
  glUniform4f(m_tintLocation, params.tint.r, params.tint.g, params.tint.b, params.tint.a);
  glUniform1f(m_opacityLocation, params.opacity);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, buffers->frost->colorTexture().value());
  glUniform1i(m_frostLocation, 1);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, buffers->raw->colorTexture().value());
  glUniform1i(m_rawLocation, 0);

  const auto posAttr = static_cast<GLuint>(m_positionLocation);
  glVertexAttribPointer(posAttr, 2, GL_FLOAT, GL_FALSE, 0, vertices.data());
  glEnableVertexAttribArray(posAttr);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(posAttr);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
}
