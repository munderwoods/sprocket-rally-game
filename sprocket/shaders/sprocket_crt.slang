// Sprocket Rally's presentation shader: the game's 256x224 (or 400x224) frame, scaled up
// to the window, through one of several CRT simulations ported from libretro's
// slang-shaders (github.com/libretro/slang-shaders). The engine allows only a few custom
// shaders, so every port lives in this one, selected by a material parameter.
//
// Material params: params0.zw = output size in pixels; params1.x = mode; params1.y = 1
// when the swapchain encodes sRGB (the result is decoded so the hardware re-encodes it).
import glsl;
#include "sprite_fragment.slangh"
#include "crt_common.slangh"

static uint g_tex;
// libretro's default wrap mode is clamp-to-border (black); the engine's samplers repeat, so
// anything outside the frame is black here, for every port.
bool crt_outside(vec2 uv) { return uv.x < 0.0 || uv.y < 0.0 || uv.x >= 1.0 || uv.y >= 1.0; }
vec4 crt_tex(vec2 uv)         { return crt_outside(uv) ? vec4(0.0) : gTextures[NonUniformResourceIndex(g_tex)].Sample(gSamplers[1], uv); }
vec4 crt_tex_nearest(vec2 uv) { return crt_outside(uv) ? vec4(0.0) : gTextures[NonUniformResourceIndex(g_tex)].Sample(gSamplers[0], uv); }

// CRT_PORTS_BEGIN
#include "crt_lottes.slangh"
#include "crt_lottes_fast.slangh"
#include "crt_aperture.slangh"
#include "crt_easymode.slangh"
#include "crt_geom.slangh"
#include "crt_gdv_mini.slangh"
#include "crt_consumer.slangh"
#include "crt_hyllian.slangh"
#include "crt_zfast.slangh"
#include "crt_pi.slangh"
#include "crt_caligari.slangh"
#include "crt_mattias.slangh"
// CRT_PORTS_END

vec3 crt_none(CrtIn c) { return crt_tex_nearest(c.uv).rgb; }

float3 srgb_decode(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow((c + 0.055) / 1.055, float3(2.4));
    return lerp(lo, hi, step(float3(0.04045), c));
}

[shader("fragment")]
float4 fs_main(SpriteVaryings i) : SV_Target {
    g_tex = i.tex;
    GpuMaterial m = sprite_material(i);
    uint w, h;
    gTextures[NonUniformResourceIndex(i.tex)].GetDimensions(w, h);
    CrtIn c;
    c.uv = i.uv;
    c.SourceSize   = vec4(float(w), float(h), 1.0 / float(w), 1.0 / float(h));
    c.OriginalSize = c.SourceSize;
    float2 out_px  = max(m.params0.zw, float2(1.0, 1.0));
    c.OutputSize   = vec4(out_px, 1.0 / out_px);
    c.FrameCount   = gPush.tick;
    c.fragCoord    = i.pos.xy;
    int mode = int(m.params1.x + 0.5);
    vec3 col;
    switch (mode) {
    // CRT_SWITCH_BEGIN
    case 1: col = crt_lottes_aperture(c); break;
    case 2: col = crt_lottes_slot(c); break;
    case 3: col = crt_lottes_fast(c); break;
    case 4: col = crt_aperture(c); break;
    case 5: col = crt_easymode(c); break;
    case 6: col = crt_geom(c); break;
    case 7: col = crt_gdv_mini(c); break;
    case 8: col = crt_consumer(c); break;
    case 9: col = crt_hyllian(c); break;
    case 10: col = crt_zfast(c); break;
    case 11: col = crt_pi(c); break;
    case 12: col = crt_caligari(c); break;
    case 13: col = crt_mattias(c); break;
    // CRT_SWITCH_END
    default: col = crt_none(c); break;
    }
    col = clamp(col, 0.0, 1.0);
    if (m.params1.y > 0.5) col = srgb_decode(col);
    return float4(col, 1.0);
}
