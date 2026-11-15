// The software renderer: everything is drawn by the CPU into the small framebuffer.
//
// The floor is Mode 7: each scanline below the horizon is one straight line across the
// map at the depth that row sees, stepped one texel-span per pixel. Above it, two sky
// layers scroll with the camera's turn. Sprites are projected by the same camera, sorted
// back to front and scaled with nearest sampling, as the SNES's scaled sprite sets were.
#include "common.h"

namespace sr {

static inline void put(Frame& f, int x, int y, u32 c) {
    if ((unsigned)x < (unsigned)f.w && (unsigned)y < (unsigned)f.h) f.px[y * f.w + x] = c;
}

void fill_rect(Frame& f, int x, int y, int w, int h, u32 c) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) put(f, xx, yy, c);
}

void dim_rect(Frame& f, int x, int y, int w, int h, f32 k) {
    for (int yy = y; yy < y + h; ++yy) {
        if ((unsigned)yy >= (unsigned)f.h) continue;
        for (int xx = x; xx < x + w; ++xx)
            if ((unsigned)xx < (unsigned)f.w) f.px[yy * f.w + xx] = shade(f.px[yy * f.w + xx], k);
    }
}

// ---- text -----------------------------------------------------------------------------------
static void glyph(Frame& f, int x, int y, char ch, u32 color, int scale) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch < 32 || ch > 95) ch = '?';
    const u8* g = k_font[ch - 32];
    for (int row = 0; row < 7; ++row)
        for (int col = 0; col < 5; ++col)
            if (g[row] & (16 >> col)) fill_rect(f, x + col * scale, y + row * scale, scale, scale, color);
}

int text_width(const char* s, int scale) {
    int n = 0;
    for (; *s; ++s) ++n;
    return n ? n * 6 * scale - scale : 0;
}

void draw_text(Frame& f, int x, int y, const char* s, u32 color, int scale, bool shadow) {
    if (shadow) {
        u32 sh = rgb(16, 16, 24);
        int cx = x;
        for (const char* p = s; *p; ++p, cx += 6 * scale) {
            glyph(f, cx + scale, y + scale, *p, sh, scale);
            if (scale > 1) { glyph(f, cx - 1, y, *p, sh, scale); glyph(f, cx, y - 1, *p, sh, scale); glyph(f, cx + 1, y, *p, sh, scale); }
        }
    }
    for (; *s; ++s, x += 6 * scale) glyph(f, x, y, *s, color, scale);
}

void draw_text_c(Frame& f, int cx, int y, const char* s, u32 color, int scale) {
    draw_text(f, cx - text_width(s, scale) / 2, y, s, color, scale, true);
}

// ---- scaled indexed blits -------------------------------------------------------------------
// where the floor drew road this frame (space only): what hangs in the void is drawn behind it
static u8 s_road_mask[400 * 224];
static void blit(Frame& f, const u8* px, int w, int h, const u32* pal, f32 x0, f32 y0, f32 scale, f32 dim = 1.0f, bool mesh = false, bool behind = false) {
    if (scale <= 0) return;
    int dx0 = (int)kv::floor(x0), dy0 = (int)kv::floor(y0);
    int dx1 = (int)kv::floor(x0 + (f32)w * scale), dy1 = (int)kv::floor(y0 + (f32)h * scale);
    if (dx1 < 0 || dy1 < 0 || dx0 >= f.w || dy0 >= f.h) return;
    f32 inv = 1.0f / scale;
    for (int y = dy0 < 0 ? 0 : dy0; y < dy1 && y < f.h; ++y) {
        int sy = (int)(((f32)y + 0.5f - y0) * inv);
        if (sy < 0 || sy >= h) continue;
        const u8* row = px + sy * w;
        u32* out = f.px + y * f.w;
        for (int x = dx0 < 0 ? 0 : dx0; x < dx1 && x < f.w; ++x) {
            int sx = (int)(((f32)x + 0.5f - x0) * inv);
            if (sx < 0 || sx >= w) continue;
            u8 v = row[sx];
            if (mesh && ((x + y) & 1)) continue; // every other pixel: see-through, the SNES way
            if (behind && s_road_mask[y * f.w + x]) continue;
            if (v) out[x] = dim == 1.0f ? pal[v] : shade(pal[v], dim);
        }
    }
}

void draw_prop_img(Frame& f, const PropImg& img, int x, int y, int scale) {
    blit(f, img.px, img.w, img.h, img.pal, (f32)x, (f32)y, (f32)scale);
}

void draw_kart_icon(Frame& f, int ch, int angle, int x, int y, int scale) {
    angle = ((angle % KART_ANGLES) + KART_ANGLES) % KART_ANGLES;
    blit(f, k_kart_px[ch][angle], KART_SIZE, KART_SIZE, k_kart_pal[ch], (f32)x, (f32)y, (f32)scale);
}

static const PropImg& prop_img(u8 kind) {
    switch (kind) {
    case P_CACTUS: return k_cactus;
    case P_PINE: return k_pine;
    case P_TOWER: return k_castle_tower;
    case P_BANNER: return k_banner;
    case P_PALM: return k_palm;
    case P_BARREL: return k_barrel;
    case P_FERN: return k_fern;
    case P_JTREE: return k_jungle_tree;
    case P_LAMP: return k_lamp;
    case P_NEON: return k_neon_sign;
    case P_LOG: return k_log;
    case P_BONES: return k_bones;
    case P_CRATE: return k_crate;
    case P_BUOY: return k_buoy;
    case P_SNOWMAN: return k_snowman;
    case P_CONE: return k_cone;
    case P_PILLAR: return k_pillar;
    case P_TORCH: return k_torch0;
    case P_BARRIER: return k_cyber_barrier;
    case P_ROCK: return k_rock;
    case P_SHRUB: return k_shrub;
    case P_SUGAR: return k_sugar_cube;
    case P_SALT: return k_salt;
    case P_PEPPER: return k_pepper;
    case P_EGGCUP: return k_egg_cup;
    case P_TOXIC: return k_toxic_barrel;
    case P_PIPE: return k_pipe0;
    case P_RAT: return k_rat;
    case P_DUCK: return k_rubber_duck;
    case P_SLAMP: return k_sewer_lamp;
    case P_LADDER: return k_ladder;
    case P_LED_R: return k_led_red0;
    case P_LED_G: return k_led_green0;
    case P_JUMPER: return k_jumper;
    case P_TRIMPOT: return k_trimpot;
    case P_ASTRONAUT: return k_astronaut;
    case P_BEACON: return k_beacon0;
    case P_TOMB0: return k_tomb0;
    case P_TOMB1: return k_tomb1;
    case P_DEADTREE: return k_dead_tree;
    case P_FENCE: return k_fence;
    case P_CANDLE: return k_candle0;
    case P_PUMPKIN: return k_pumpkin;
    case P_ARMOR: return k_armor;
    case P_COFFIN: return k_coffin;
    case P_BAT: return k_bat0;
    case P_CLOCK: return k_grandfather;
    default: return k_tree;
    }
}

// Props with a second frame: flames, lights, wings, running water.
static const PropImg& prop_frame(u8 kind, u32 tick, u32 index) {
    bool alt = ((tick / 8 + index) & 1) != 0;
    switch (kind) {
    case P_TORCH: return alt ? k_torch1 : k_torch0;
    case P_PIPE: return alt ? k_pipe1 : k_pipe0;
    case P_LED_R: return ((tick / 20 + index) & 1) ? k_led_red1 : k_led_red0;
    case P_LED_G: return ((tick / 26 + index) & 1) ? k_led_green1 : k_led_green0;
    case P_BEACON: return ((tick / 16 + index) & 1) ? k_beacon1 : k_beacon0;
    case P_CANDLE: return alt ? k_candle1 : k_candle0;
    case P_BAT: return ((tick / 5) & 1) ? k_bat1 : k_bat0;
    default: return prop_img(kind);
    }
}

static const SetPieceImg& sp_img(u8 img, f32 t) {
    bool alt = ((int)(t * 4.0f)) & 1;
    switch (img) {
    case SPI_TRAIN_LOCO: return k_sp_train_loco;
    case SPI_TRAIN_CAR: return k_sp_train_car;
    case SPI_SNOWBALL: return k_sp_snowball;
    case SPI_DRAGON: return alt ? k_sp_dragon_b : k_sp_dragon_a;
    case SPI_SHIP: return k_sp_ship;
    case SPI_DINO: return ((int)(t * 1.25f) & 1) ? k_sp_dino_b : k_sp_dino_a;
    case SPI_BLIMP: return k_sp_blimp;
    case SPI_HOVERBUS: return k_sp_hoverbus;
    case SPI_CHEVRON_L: return k_sp_chevron_l;
    case SPI_CHEVRON_R: return k_sp_chevron_r;
    case SPI_CEREAL: return k_sp_cereal;
    case SPI_MILK: return k_sp_milk;
    case SPI_JUICE: return k_sp_juice;
    case SPI_SYRUP: return k_sp_syrup;
    case SPI_TOASTER: return k_sp_toaster;
    case SPI_MUG: return k_sp_mug;
    case SPI_ORANGE: return k_sp_orange;
    case SPI_BUTTER: return k_sp_butter;
    case SPI_WAVE: return alt ? k_sp_wave_b : k_sp_wave_a;
    case SPI_CAPACITOR: return k_sp_capacitor;
    case SPI_CHIP: return k_sp_chip;
    case SPI_RESISTOR: return k_sp_resistor;
    case SPI_HEATSINK: return k_sp_heatsink;
    case SPI_CRYSTAL: return k_sp_crystal;
    case SPI_CERAMIC: return k_sp_ceramic;
    case SPI_MOTH: return alt ? k_sp_moth_b : k_sp_moth_a;
    case SPI_SUN: return k_sp_sun;
    case SPI_JUPITER: return k_sp_jupiter;
    case SPI_SATURN: return k_sp_saturn;
    case SPI_EARTH: return k_sp_earth;
    case SPI_MOON: return k_sp_moon;
    case SPI_MARS: return k_sp_mars;
    case SPI_NEPTUNE: return k_sp_neptune;
    case SPI_VENUS: return k_sp_venus;
    case SPI_ASTEROID: return k_sp_asteroid;
    case SPI_COMET: return k_sp_comet;
    case SPI_SATELLITE: return k_sp_satellite;
    case SPI_GHOST: return alt ? k_sp_ghost_b : k_sp_ghost_a;
    case SPI_CARRIAGE: return k_sp_carriage;
    default: return k_sp_hovercar;
    }
}

const PropImg* item_icon(int item) {
    switch (item) {
    case IT_NITRO: return &k_icon_nitro;
    case IT_NITRO3: return &k_icon_nitro3;
    case IT_BOMB: return &k_bomb;
    case IT_ROCKET: return &k_icon_rocket;
    case IT_OIL: return &k_icon_oil;
    case IT_FAKE: return &k_icon_fake;
    case IT_SHIELD: return &k_icon_shield;
    case IT_EMP: return &k_icon_emp;
    case IT_MAGNET: return &k_icon_magnet;
    default: return nullptr;
    }
}

// ---- sky and floor --------------------------------------------------------------------------
// the view being drawn: whose camera, where the skyline sits in the frame, how high the camera is
static vec2 s_view_cam{};
static f32  s_view_angle = 0, s_view_pull = 0;
static int  s_horizon = 70;
static f32  s_cam_h = 40.0f;
// the graveyard's moon this frame: below zero, there is none
static f32 s_moon = -1.0f;
static const u32 k_moonlight = rgb(150, 190, 255);

static void draw_sky(Frame& f, const Track& t, f32 cam_angle) {
    const Theme& th = *t.theme;
    // banded gradient, like an HDMA color ramp
    for (int y = 0; y <= s_horizon; ++y) {
        f32 k = (f32)(y / 6 * 6) / (f32)s_horizon;
        u32 c = blend(th.sky_top, th.sky_bottom, k);
        u32* row = f.px + y * f.w;
        for (int x = 0; x < f.w; ++x) row[x] = c;
    }
    if (th.stars) {
        int off = (int)kv::floor(cam_angle * FOCAL * 0.25f);
        for (int i = 0; i < 70; ++i) {
            int sx = (int)(hash2(i, 5) % 1024), sy = (int)(hash2(i, 9) % (u32)(s_horizon - 20));
            int x = ((sx - off) % 1024 + 1024) % 1024;
            if (x < f.w) f.px[sy * f.w + x] = (i % 7 == 0) ? rgb(248, 200, 255) : rgb(200, 200, 232);
        }
    }
    if (s_moon >= 0.0f) { // the graveyard's moon, behind the hills; a cloud bank hides it and slides off
        int mx = (((300 - ((int)kv::floor(cam_angle * FOCAL * 0.5f) - f.w / 2)) % 512) + 512) % 512;
        if (mx > 256) mx -= 512;
        const PropImg& m = s_moon > 0.5f ? k_moon : k_moon_dim;
        f32 my = (f32)s_horizon - 44.0f - 14.0f * s_moon;
        blit(f, m.px, m.w, m.h, m.pal, (f32)mx - 14.0f, my - 14.0f, 1.0f);
        blit(f, k_moon_cloud.px, k_moon_cloud.w, k_moon_cloud.h, k_moon_cloud.pal, (f32)mx - 28.0f + 52.0f * s_moon, (f32)s_horizon - 52.0f, 1.0f);
    }
    int near_off = (int)kv::floor(cam_angle * FOCAL) - f.w / 2;
    int far_off = (int)kv::floor(cam_angle * FOCAL * 0.5f) - f.w / 2;
    for (int ly = 0; ly < 64; ++ly) {
        int y = s_horizon + 1 - 64 + ly;
        if (y < 0) continue;
        u32* row = f.px + y * f.w;
        const u32* far_row = t.far_layer.data + ly * 512;
        const u32* near_row = t.near_layer.data + ly * 1024;
        for (int x = 0; x < f.w; ++x) {
            u32 c = near_row[(x + near_off) & 1023];
            if (!c) c = far_row[(x + far_off) & 511];
            if (c) row[x] = c;
        }
    }
    if (s_moon > 0.0f) // and the moonlight on the hills
        for (int y = s_horizon - 63; y <= s_horizon; ++y) {
            if (y < 0) continue;
            u32* row = f.px + y * f.w;
            for (int x = 0; x < f.w; ++x) if (row[x] != k_moon.pal[1] && row[x] != k_moon.pal[2]) row[x] = blend(row[x], k_moonlight, 0.22f * s_moon);
        }
}

static void draw_floor(Frame& f, const Track& t, vec2 cam, f32 ang, u32 tick) {
    const f32 fx = kv::cos(ang), fy = kv::sin(ang);
    const f32 rx = -fy, ry = fx;
    const u8* color = t.color.data;
    // palette cycling, the SNES way to animate a map: water ripples, boost pads chase
    u32 pal[C_COUNT];
    for (int i = 0; i < C_COUNT; ++i) pal[i] = t.theme->pal[i];
    int w = (int)(tick / 10) % 3;
    for (int i = 0; i < 3; ++i) pal[C_WATER0 + i] = t.theme->pal[C_WATER0 + (i + w) % 3];
    if ((tick / 6) & 1) { pal[C_BOOST0] = t.theme->pal[C_BOOST1]; pal[C_BOOST1] = t.theme->pal[C_BOOST0]; }
    if ((tick / 12) & 1) { pal[C_SHAL0] = t.theme->pal[C_SHAL1]; pal[C_SHAL1] = t.theme->pal[C_SHAL0]; }
    if ((tick / 8) & 1) { pal[C_RAMP0] = t.theme->pal[C_RAMP1]; pal[C_RAMP1] = t.theme->pal[C_RAMP0]; }
    if (t.theme->road == RS_SPACE) {
        static const u32 hues[6] = {0xffc840f8u, 0xfff8e840u, 0xff40f8a0u, 0xff40e8f8u, 0xff4080f8u, 0xfff840c0u};
        pal[C_CURB_A] = hues[(tick / 7) % 6]; pal[C_CURB_B] = hues[(tick / 7 + 3) % 6];
    }
    if (t.theme->road == RS_NEON && ((tick / 20) & 1)) { pal[C_CURB_B] = t.theme->pal[C_CURB_A]; pal[C_CURB_A] = t.theme->pal[C_LINE]; }
    if (s_moon > 0.0f) for (int i = 0; i < C_COUNT; ++i) pal[i] = blend(pal[i], k_moonlight, 0.3f * s_moon); // moonlight on the ground
    u32 out0 = pal[C_OUT0], out1 = pal[C_OUT1];
    const bool space = t.theme->road == RS_SPACE && f.w * f.h <= (int)sizeof s_road_mask;
    const u8* surf = t.surf.data;
    if (space) for (int i = 0; i < f.w * f.h; ++i) s_road_mask[i] = 0;
    for (int y = s_horizon + 1; y < f.h; ++y) {
        f32 depth = s_cam_h * FOCAL / (f32)(y - s_horizon);
        f32 step = depth / FOCAL;
        f32 cx = cam.x + fx * depth, cy = cam.y + fy * depth;
        f32 wx = cx + rx * step * ((f32)(-f.w / 2) + 0.5f);
        f32 wy = cy + ry * step * ((f32)(-f.w / 2) + 0.5f);
        f32 sx = rx * step, sy = ry * step;
        u32* row = f.px + y * f.w;
        for (int x = 0; x < f.w; ++x, wx += sx, wy += sy) {
            int ix = (int)kv::floor(wx), iy = (int)kv::floor(wy);
            if ((unsigned)ix < (unsigned)MAP && (unsigned)iy < (unsigned)MAP) {
                row[x] = pal[color[iy * MAP + ix]];
                if (space && surf[iy * MAP + ix] != S_PIT) s_road_mask[y * f.w + x] = 1;
            } else row[x] = (((ix >> 3) ^ (iy >> 3)) & 1) ? out0 : out1;
        }
    }
}

// ---- world sprites --------------------------------------------------------------------------
enum DrawKind : u8 { D_KART, D_PROP, D_BOX, D_BOLT, D_BOMB, D_OIL, D_PART, D_SET, D_SHADOW, D_LIGHT, D_STATIC, D_HAND, D_GHOSTKART, D_GATE_POST, D_GATE_SPAN };

struct Draw {
    f32 depth, sx, sy, scale;  // sy: the ground point's row
    u8  kind;
    u16 index;
};

struct Proj { bool ok; f32 depth, sx, sy, scale; };

static Proj project(const Frame& f, vec2 cam, f32 ang, vec2 p) {
    Proj r{};
    vec2 d = p - cam;
    f32 fx = kv::cos(ang), fy = kv::sin(ang);
    f32 depth = d.x * fx + d.y * fy;
    f32 lat = -d.x * fy + d.y * fx;
    if (depth < 6.0f || depth > 1500.0f) return r;
    f32 k = FOCAL / depth;
    r.sx = (f32)f.w * 0.5f + lat * k;
    if (r.sx < -260.0f || r.sx > (f32)f.w + 260.0f) return r; // wide: some things are very large
    r.sy = (f32)s_horizon + s_cam_h * k;
    r.depth = depth;
    r.scale = k * SPRITE_WORLD;
    r.ok = true;
    return r;
}

static void draw_shadow(Frame& f, f32 cx, f32 cy, f32 rw, f32 rh) {
    int x0 = (int)(cx - rw), x1 = (int)(cx + rw), y0 = (int)(cy - rh), y1 = (int)(cy + rh);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            f32 ex = ((f32)x + 0.5f - cx) / rw, ey = ((f32)y + 0.5f - cy) / rh;
            if (ex * ex + ey * ey <= 1.0f && (unsigned)x < (unsigned)f.w && (unsigned)y < (unsigned)f.h) f.px[y * f.w + x] = shade(f.px[y * f.w + x], 0.55f);
        }
}

static void draw_ring(Frame& f, f32 cx, f32 cy, f32 r, u32 c) {
    int n = (int)(r * 6.0f) + 8;
    for (int i = 0; i < n; ++i) {
        f32 a = 6.2831853f * (f32)i / (f32)n;
        put(f, (int)(cx + kv::cos(a) * r), (int)(cy + kv::sin(a) * r * 0.85f), c);
    }
}

static void draw_world_impl(Frame& f, const Race& r);
// ---- the start gate -------------------------------------------------------------------------
// Drawn from its measurements rather than from a baked picture, so it is right from any
// angle and distance: the near post larger than the far one, the banner foreshortened, and
// the whole thing passing overhead as a kart drives through. Units are map texels; the
// beam sits above the camera's own height, so it leaves by the top of the screen.
constexpr f32 GATE_POST = 38.0f, GATE_HALF = 41.0f;         // a post's distance from the middle; half the span
constexpr f32 GATE_POST_TOP = 50.0f, GATE_BEAM0 = 44.0f, GATE_BEAM1 = 52.0f, GATE_CREST = 57.5f, GATE_TOP = 64.0f;

// A post's color at a height: foot, hazard bands, steel, cap. `tone` 0 lit, 1 mid, 2 shaded.
static u32 gate_post_color(f32 z, int tone) {
    static const u32 steel[3] = {rgb(176, 190, 214), rgb(118, 132, 164), rgb(74, 84, 116)};
    static const u32 red[3] = {rgb(255, 96, 80), rgb(216, 44, 48), rgb(140, 24, 40)};
    static const u32 white[3] = {rgb(255, 255, 255), rgb(214, 220, 236), rgb(150, 158, 186)};
    static const u32 black[3] = {rgb(64, 64, 80), rgb(36, 36, 50), rgb(22, 22, 32)};
    if (z < 2.0f) return black[tone];
    if (z < 26.0f) return (((int)((z - 2.0f) / 4.0f)) & 1) ? white[tone] : red[tone];
    if (z >= GATE_POST_TOP - 1.6f) return steel[tone > 0 ? tone - 1 : 0];
    return steel[tone];
}
// How wide (half) a post is at a height.
static f32 gate_post_half(f32 z) { return z < 2.0f ? 3.4f : (z < 26.0f ? 2.2f : (z >= GATE_POST_TOP - 1.6f ? 2.4f : 1.7f)); }

// The gate seen flat on: the color at `u` along the span (0 in the middle) and height `z`,
// or 0 where there is nothing.
static u32 gate_plane_color(f32 u, f32 z) {
    f32 au = kv::fabs(u);
    if (z >= GATE_BEAM0 && z < GATE_BEAM1 && au <= 40.5f) { // the beam: rails, and two rows of checkers
        if (z >= GATE_BEAM1 - 0.9f) return rgb(176, 190, 214);
        if (z < GATE_BEAM0 + 0.9f) return rgb(118, 132, 164);
        if (au > 36.0f) return z > GATE_BEAM0 + 4.0f ? rgb(255, 96, 80) : rgb(216, 44, 48);
        int row = z - (GATE_BEAM0 + 0.9f) >= 3.1f ? 1 : 0, col = (int)kv::floor((u + 36.0f) / 3.0f);
        return ((row + col) & 1) ? rgb(36, 36, 50) : rgb(255, 255, 255);
    }
    if (z >= GATE_BEAM1 && au < 7.5f) { // the sprocket crest standing on the beam
        f32 dz = z - GATE_CREST, rr = kv::sqrt(u * u + dz * dz);
        if (rr < 2.0f) return rgb(36, 36, 50);
        f32 ang = kv::atan2(dz, u);
        bool tooth = kv::cos(ang * 8.0f) > 0.25f;
        if (rr < 4.6f || (tooth && rr < 6.2f)) return rr > 3.6f && dz < 0.5f ? rgb(226, 164, 48) : (dz > 1.5f ? rgb(255, 226, 110) : rgb(240, 196, 80));
        return 0;
    }
    if (z < GATE_POST_TOP) {
        f32 off = au - GATE_POST;
        f32 side = u < 0 ? -off : off; // left of the post's own middle is its lit side, as on the standing post
        if (kv::fabs(off) <= gate_post_half(z)) return gate_post_color(z, side < -0.5f ? 0 : (side > 0.8f ? 2 : 1));
        // the braces under the beam, in toward the middle
        f32 in = (GATE_POST - 1.7f) - au;
        if (in >= 0.0f && in < 9.0f && z < GATE_BEAM0 && z >= GATE_BEAM0 - (9.0f - in)) return rgb(74, 84, 116);
    }
    return 0;
}

// The span between the posts: each screen column finds where on the gate it looks, how far
// away that is, and so how tall the gate stands there.
static void draw_gate_span(Frame& f, vec2 cam, const SetBody& g) {
    vec2 fw{kv::cos(s_view_angle), kv::sin(s_view_angle)}, rt{-fw.y, fw.x};
    vec2 across{-kv::sin(g.heading), kv::cos(g.heading)};
    vec2 a = g.pos - across * GATE_HALF - cam, b = g.pos + across * GATE_HALF - cam;
    f32 da = dot(a, fw), la = dot(a, rt), dd = dot(b, fw) - da, dl = dot(b, rt) - la;
    const f32 near = 3.0f;
    for (int x = 0; x < f.w; ++x) {
        f32 m = ((f32)x + 0.5f - (f32)f.w * 0.5f) / FOCAL;
        f32 den = dl - m * dd;
        if (kv::fabs(den) < 1e-5f) continue;
        f32 tt = (m * da - la) / den;
        if (tt < 0.0f || tt > 1.0f) continue;
        f32 depth = da + tt * dd;
        if (depth < near) continue;
        f32 k = FOCAL / depth, u = (tt - 0.5f) * 2.0f * GATE_HALF;
        f32 ground = (f32)s_horizon + s_cam_h * k;
        int y0 = (int)kv::floor(ground - GATE_TOP * k), y1 = (int)kv::floor(ground);
        if (y0 < 0) y0 = 0;
        if (y1 >= f.h) y1 = f.h - 1;
        for (int y = y0; y <= y1; ++y) {
            u32 c = gate_plane_color(u, (ground - ((f32)y + 0.5f)) / k);
            if (c) f.px[y * f.w + x] = c;
        }
    }
}

// A post, facing the camera whichever way the gate stands, so it keeps its thickness when
// the span is seen edge on.
static void draw_gate_post(Frame& f, f32 sx, f32 ground, f32 k) {
    draw_shadow(f, sx, ground, 5.0f * k, 1.4f * k);
    int y0 = (int)kv::floor(ground - (GATE_POST_TOP + 3.4f) * k), y1 = (int)kv::floor(ground);
    if (y0 < 0) y0 = 0;
    if (y1 >= f.h) y1 = f.h - 1;
    for (int y = y0; y <= y1; ++y) {
        f32 z = (ground - ((f32)y + 0.5f)) / k;
        bool lamp = z >= GATE_POST_TOP; // the beacon on top
        f32 hw = (lamp ? kv::sqrt(mxf(0.0f, 2.9f - (z - GATE_POST_TOP - 1.7f) * (z - GATE_POST_TOP - 1.7f))) : gate_post_half(z)) * k;
        if (hw < 0.5f) { if (lamp) continue; hw = 0.5f; }
        int x0 = (int)kv::floor(sx - hw), x1 = (int)kv::floor(sx + hw);
        for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < f.w; ++x) {
            f32 across = ((f32)x + 0.5f - sx) / hw; // -1 .. 1
            f.px[y * f.w + x] = lamp ? (across < 0.2f ? rgb(255, 250, 190) : rgb(255, 210, 90)) : gate_post_color(z, across < -0.3f ? 0 : (across > 0.45f ? 2 : 1));
        }
    }
}

void draw_world(Frame& f, const Race& r) { draw_world_view(f, r, 0, 70, 40.0f); }
void draw_world_view(Frame& f, const Race& r, int view, int horizon, f32 cam_h) {
    bool other = view > 0 && view < r.humans;
    s_view_cam = other ? r.cams[view] : r.cam;
    s_view_angle = other ? r.cam_angles[view] : r.cam_angle;
    s_view_pull = other ? r.cam_pulls[view] : r.cam_pull;
    s_horizon = horizon; s_cam_h = cam_h;
    draw_world_impl(f, r);
    s_horizon = 70; s_cam_h = 40.0f;
}
static void draw_world_impl(Frame& f, const Race& r) {
    const Track& t = r.track;
    s_moon = t.theme->road == RS_HAUNT ? r.moon : -1.0f;
    u32 tick = (u32)(r.clock * 60.0f);
    vec2 cam = s_view_cam;
    if (r.shake > 0) cam += vec2{(f32)((int)(hash2((i32)tick, 1) % 5) - 2), (f32)((int)(hash2((i32)tick, 2) % 5) - 2)} * (r.shake * 6.0f);
    draw_sky(f, t, s_view_angle);
    draw_floor(f, t, cam, s_view_angle, tick);

    static Draw list[1600];
    int n = 0;
    auto add = [&](vec2 p, u8 kind, u32 index) {
        if (n >= 1600) return;
        Proj pr = project(f, cam, s_view_angle, p);
        if (!pr.ok) return;
        list[n++] = Draw{pr.depth, pr.sx, pr.sy, pr.scale, kind, (u16)index};
    };
    for (u32 i = 0; i < t.props.len; ++i) add(t.props[i].pos, D_PROP, i);
    for (u32 i = 0; i < t.boxes.len; ++i) if (t.boxes[i].respawn <= 0) add(t.boxes[i].pos, D_BOX, i);
    for (u32 i = 0; i < t.sp.bodies.len; ++i) {
        add(t.sp.bodies[i].pos, D_SET, i);
        if (t.sp.bodies[i].z > 20.0f) add(t.sp.bodies[i].pos, D_SHADOW, i); // flyers cast a shadow on the ground
    }
    for (int i = 0; i < t.sp.ncross; ++i) add(t.sp.cross[i], D_LIGHT, (u32)i);
    for (u32 i = 0; i < t.statics.len; ++i) {
        // the start gate is real geometry, not a picture: a post at each side of the road and
        // the banner spanning between them, each sorted by its own depth
        if (t.statics[i].img == SPI_GANTRY) {
            const SetBody& gb = t.statics[i];
            vec2 across{-kv::sin(gb.heading), kv::cos(gb.heading)};
            for (int side = -1; side <= 1; side += 2) {
                Proj pr = project(f, cam, s_view_angle, gb.pos + across * (GATE_POST * (f32)side));
                if (pr.ok && n < 1600) list[n++] = Draw{pr.depth, pr.sx, pr.sy, pr.scale, D_GATE_POST, (u16)i};
            }
            vec2 d = gb.pos - cam;
            f32 depth = d.x * kv::cos(s_view_angle) + d.y * kv::sin(s_view_angle);
            // (the span is listed even with its middle behind the camera: its ends may still be in view)
            if (depth > -GATE_HALF && depth < 1500.0f && n < 1600) list[n++] = Draw{depth < 1.0f ? 1.0f : depth, 0, 0, 0, D_GATE_SPAN, (u16)i};
            continue;
        }
        if (t.statics[i].img == SPI_SUN) { // the sun burns in the void under the road: always behind it
            Proj pr = project(f, cam, s_view_angle, t.statics[i].pos);
            if (pr.ok) {
                const SetPieceImg& img = sp_img(SPI_SUN, 0);
                f32 kk = FOCAL / pr.depth, ss = kk * img.world_per_px * (t.statics[i].scale > 0 ? t.statics[i].scale : 1.0f);
                blit(f, img.px, img.w, img.h, img.pal, pr.sx - (f32)img.anchor_x * ss, pr.sy - t.statics[i].z * kk - (f32)img.anchor_y * ss, ss, 1.0f, false, true);
            }
            continue;
        }
        add(t.statics[i].pos, D_STATIC, i);
    }
    for (u32 i = 0; i < t.bolts.len; ++i) if (t.bolts[i].respawn <= 0) add(t.bolts[i].pos, D_BOLT, i);
    for (int i = 0; i < r.nhands; ++i) add(r.hands[i], D_HAND, (u32)i);
    for (u32 i = 0; i < r.oils.len; ++i) add(r.oils[i].pos, D_OIL, i);
    for (u32 i = 0; i < r.bombs.len; ++i) add(r.bombs[i].pos, D_BOMB, i);
    for (int i = 0; i < RACERS; ++i) add(r.racers[i].pos, D_KART, (u32)i);
    // a time trial's ghost: where the best run was at this moment of the race
    int ghost_i = -1;
    if (r.ghost && r.ghost_n > 0) {
        ghost_i = r.phase == RacePhase::Countdown ? 0 : (int)(r.race_time * 60.0f + 0.5f);
        if (ghost_i >= r.ghost_n) ghost_i = -1; // it has finished
        else add(vec2{r.ghost[ghost_i].x, r.ghost[ghost_i].y}, D_GHOSTKART, 0);
    }
    for (u32 i = 0; i < r.parts.len; ++i) add(r.parts[i].pos, D_PART, i);

    // back to front (insertion sort: the list is nearly sorted frame to frame anyway)
    for (int a = 1; a < n; ++a) {
        Draw d = list[a];
        int b = a;
        // shadows draw first among equals: they lie on the ground under their object
        while (b > 0 && (list[b - 1].depth < d.depth || (list[b - 1].depth == d.depth && d.kind == D_SHADOW))) { list[b] = list[b - 1]; --b; }
        list[b] = d;
    }

    bool train_near[8] = {};
    if (t.sp.kind == SPK_TRAIN && t.sp.bodies.len)
        for (int i = 0; i < t.sp.ncross; ++i)
            for (u32 b = 0; b < t.sp.bodies.len; ++b)
                if (length_sq(t.sp.bodies[b].pos - t.sp.cross[i]) < 260.0f * 260.0f) train_near[i] = true;
    for (int i = 0; i < n; ++i) {
        const Draw& d = list[i];
        f32 s = d.scale;
        f32 k = FOCAL / d.depth;
        switch (d.kind) {
        case D_PROP: {
            const Prop& p = t.props[d.index];
            const PropImg& img = prop_frame(p.kind, tick, d.index);
            f32 ps = s * 1.6f;
            blit(f, img.px, img.w, img.h, img.pal, d.sx - (f32)img.w * ps * 0.5f, d.sy - (f32)img.h * ps, ps);
        } break;
        case D_GATE_POST: draw_gate_post(f, d.sx, d.sy, k); break;
        case D_GATE_SPAN: draw_gate_span(f, cam, t.statics[d.index]); break;
        case D_GHOSTKART: {
            if (ghost_i < 0) break;
            const GhostSample& gs = r.ghost[ghost_i];
            f32 a = wrap_pi(gs.heading - kv::atan2(gs.y - cam.y, gs.x - cam.x));
            int idx = (int)kv::floor(a / (6.2831853f / (f32)KART_ANGLES) + 0.5f);
            idx = ((idx % KART_ANGLES) + KART_ANGLES) % KART_ANGLES;
            int gc = r.ghost_ch & 7;
            // every other pixel, so the road shows through it
            blit(f, k_kart_px[gc][idx], KART_SIZE, KART_SIZE, k_kart_pal[gc], d.sx - (f32)KART_ANCHOR_X * s, d.sy - (f32)KART_ANCHOR_Y * s, s, 1.0f, true);
        } break;
        case D_HAND: {
            // a mound while the moon is hidden, fingers as it rises, the whole arm when it is out
            const PropImg& img = r.moon <= 0.0f ? k_hand0 : (r.moon < 1.0f ? k_hand1 : (((tick / 14 + d.index) & 1) ? k_hand3 : k_hand2));
            f32 ps = s * 1.3f;
            blit(f, img.px, img.w, img.h, img.pal, d.sx - (f32)img.w * ps * 0.5f, d.sy - (f32)img.h * ps, ps);
        } break;
        case D_BOX: {
            const ItemBox& box = t.boxes[d.index];
            const PropImg* frames[4] = {&k_crystal0, &k_crystal1, &k_crystal2, &k_crystal3};
            const PropImg& img = box.fake ? k_fake_crystal : *frames[(tick / 8 + d.index) & 3];
            f32 bob = 6.0f + 1.5f * kv::sin((f32)tick * 0.1f + (f32)d.index) + box.z;
            draw_shadow(f, d.sx, d.sy, 5.0f * s, 1.5f * s);
            blit(f, img.px, img.w, img.h, img.pal, d.sx - 8.0f * s, d.sy - bob * k - 16.0f * s, s);
        } break;
        case D_BOLT: {
            const PropImg* frames[4] = {&k_bolt0, &k_bolt1, &k_bolt2, &k_bolt3};
            const Bolt& bo = t.bolts[d.index];
            if (bo.life > 0 && bo.life < 1.5f && (tick & 4)) break; // a spilled one blinks before it goes
            const PropImg& img = *frames[(tick / (bo.life > 0 ? 3 : 6) + d.index) & 3];
            blit(f, img.px, img.w, img.h, img.pal, d.sx - 5.5f * s, d.sy - (3.0f + bo.z) * k - 11.0f * s, s);
        } break;
        case D_OIL: {
            const Hazard& h = r.oils[d.index];
            if (h.kind == HZ_FIRE && h.warn > 0) {
                // the warning: a smouldering scorch mark, flickering
                if (tick & 4) draw_shadow(f, d.sx, d.sy, 7.0f * s, 2.0f * s);
                if ((tick & 8) && d.index % 2 == 0) fill_rect(f, (int)d.sx, (int)(d.sy - 3.0f * s), 1, 1, rgb(255, 160, 48));
            } else if (h.kind == HZ_FIRE) {
                const PropImg* fire[3] = {&k_fire0, &k_fire1, &k_fire2};
                const PropImg* arcs[3] = {&k_spark0, &k_spark1, &k_spark2};
                const PropImg* flares[3] = {&k_flare0, &k_flare1, &k_flare2};
                const PropImg** fr = t.theme->fire_look == 1 ? arcs : (t.theme->fire_look == 2 ? flares : fire);
                const PropImg& img = *fr[(tick / 5 + d.index) % 3];
                blit(f, img.px, img.w, img.h, img.pal, d.sx - (f32)img.w * s * 0.5f, d.sy - (f32)img.h * s, s);
            } else blit(f, k_oil.px, k_oil.w, k_oil.h, k_oil.pal, d.sx - 12.0f * s, d.sy - 6.0f * s, s);
        } break;
        case D_BOMB: {
            const Projectile& b = r.bombs[d.index];
            const PropImg& img = b.kind == PK_FLARE ? ((tick & 4) ? k_flare1 : k_flare0) : b.kind == PK_ROCKET ? k_rocket : (b.kind == PK_CANNON ? k_cannonball : (b.kind == PK_WAFFLE ? ((tick & 8) ? k_waffle1 : k_waffle0) : k_bomb));
            if (b.kind == PK_CANNON || b.kind == PK_WAFFLE || b.kind == PK_FLARE) {
                // where it will land: a shadow that grows as it comes down, and a target ring
                Proj at = project(f, cam, s_view_angle, b.aim);
                if (at.ok) {
                    f32 speed = length(b.vel);
                    f32 left = speed > 1.0f ? length(b.aim - b.pos) / speed : 0.0f; // seconds to impact
                    f32 grow = clampf(1.0f - left / 1.4f, 0.0f, 1.0f);
                    draw_shadow(f, at.sx, at.sy, (3.0f + 10.0f * grow) * at.scale, (1.0f + 3.0f * grow) * at.scale);
                    if (tick & 4) draw_ring(f, at.sx, at.sy, (b.kind == PK_FLARE ? 8.0f : 13.0f) * at.scale, rgb(255, 64, 48));
                }
            }
            draw_shadow(f, d.sx, d.sy, 4.0f * s, 1.2f * s);
            f32 is = b.kind == PK_FLARE ? s * 0.8f : b.kind == PK_CANNON ? s * 1.4f : (b.kind == PK_WAFFLE ? s * 2.4f : s);
            blit(f, img.px, img.w, img.h, img.pal, d.sx - (f32)img.w * is * 0.5f, d.sy - b.z * k - (f32)img.h * is, is);
        } break;
        case D_SET: {
            const SetBody& b = t.sp.bodies[d.index];
            // (a ghost's `t` is how far it has drifted, which would flap it; it wavers slowly instead)
            const SetPieceImg& img = sp_img(b.img, b.img == SPI_GHOST ? r.clock * 0.35f + (f32)d.index * 0.3f : b.t + r.clock);
            f32 ss = k * img.world_per_px * (b.scale > 0 ? b.scale : 1.0f);
            int idx = 0;
            if (b.img == SPI_SNOWBALL) idx = (int)(r.clock * 8.0f) % img.angles; // rolling frames
            else if (b.img == SPI_ASTEROID) idx = (int)(r.clock * 5.0f + (f32)d.index) % img.angles;
            else if (b.img == SPI_ORANGE) idx = (((int)(b.t * 6.0f) % img.angles) + img.angles) % img.angles;
            else {
                // the angle is taken from the view direction, not the bearing to the thing: a
                // row of wagons then shares one frame, and nothing turns as you drive past it
                f32 a = wrap_pi(b.heading - s_view_angle);
                idx = (int)kv::floor(a / (6.2831853f / (f32)img.angles) + 0.5f);
                idx = ((idx % img.angles) + img.angles) % img.angles;
            }
            if (b.z <= 20.0f) draw_shadow(f, d.sx, d.sy, (f32)img.w * ss * 0.35f, (f32)img.w * ss * 0.08f);
            blit(f, img.px + (usize)idx * (usize)(img.w * img.h), img.w, img.h, img.pal,
                 d.sx - (f32)img.anchor_x * ss, d.sy - b.z * k - (f32)img.anchor_y * ss, ss, 1.0f, b.img == SPI_GHOST || b.img == SPI_CARRIAGE);
        } break;
        case D_STATIC: {
            const SetBody& b = t.statics[d.index];
            const SetPieceImg& img = (b.img == SPI_TOASTER && t.sp.aux > 0) ? k_sp_toaster_pop : sp_img(b.img, 0);
            const f32 obj_scale = b.scale > 0 ? b.scale : 1.0f;
            // a giant standing between the camera and the kart is drawn as a mesh, so it
            // towers without hiding the road
            bool mesh = b.img >= SPI_CEREAL && d.depth < CAM_DIST + s_view_pull + 8.0f;
            f32 ss = k * img.world_per_px * obj_scale;
            f32 a = wrap_pi(b.heading - s_view_angle);
            int idx = (int)kv::floor(a / (6.2831853f / (f32)img.angles) + 0.5f);
            idx = ((idx % img.angles) + img.angles) % img.angles;
            blit(f, img.px + (usize)idx * (usize)(img.w * img.h), img.w, img.h, img.pal,
                 d.sx - (f32)img.anchor_x * ss, d.sy - b.z * k - (f32)img.anchor_y * ss, ss, 1.0f, mesh);
        } break;
        case D_SHADOW: {
            const SetBody& b = t.sp.bodies[d.index];
            // (a ghost's `t` is how far it has drifted, which would flap it; it wavers slowly instead)
            const SetPieceImg& img = sp_img(b.img, b.img == SPI_GHOST ? r.clock * 0.35f + (f32)d.index * 0.3f : b.t + r.clock);
            f32 ss = k * img.world_per_px;
            draw_shadow(f, d.sx, d.sy, (f32)img.w * ss * 0.3f, (f32)img.w * ss * 0.09f);
        } break;
        case D_LIGHT: {
            const PropImg& img = (train_near[d.index] && (tick & 16)) ? k_warning_light1 : k_warning_light0;
            f32 ls = s * 1.5f;
            blit(f, img.px, img.w, img.h, img.pal, d.sx - (f32)img.w * ls * 0.5f, d.sy - (f32)img.h * ls, ls);
        } break;
        case D_PART: {
            const Particle& p = r.parts[d.index];
            int sz = (int)(p.size * s + 0.5f);
            if (sz < 1) sz = 1;
            fill_rect(f, (int)(d.sx - (f32)sz * 0.5f), (int)(d.sy - p.z * k - (f32)sz * 0.5f), sz, sz, p.color);
        } break;
        case D_KART: {
            const Racer& kart = r.racers[d.index];
            vec2 rel = kart.pos - cam;
            f32 view = kv::atan2(rel.y, rel.x);
            f32 vis = kart.heading; // the body: it already hangs into a drift
            if (kart.spin > 0) vis += (1.0f - kart.spin / 1.1f) * 2.0f * 6.2831853f; // two whole turns, ending as it started
            if (kart.fall > 0 && kart.fall_kind == S_PIT && kart.fall < 0.05f) break;
            f32 a = wrap_pi(vis - view);
            int idx = (int)kv::floor(a / (6.2831853f / (f32)KART_ANGLES) + 0.5f);
            idx = ((idx % KART_ANGLES) + KART_ANGLES) % KART_ANGLES;
            const u32* pal = k_kart_pal[kart.ch];
            f32 ks = s;
            f32 lift = kart.z * k;
            if (kart.fall > 0 && kart.fall_kind == S_PIT) ks = s * mxf(0.0f, kart.fall / 1.1f); // dropping away
            if (kart.fall > 0 && kart.fall_kind == S_WATER && kart.z < -10.0f) break;      // gone under
            if (kart.fall <= 0) draw_shadow(f, d.sx, d.sy, 11.0f * s, 3.0f * s);
            if (kart.drifting && !kart.airborne) lift += (f32)((tick >> 1) & 1); // the drift wobble
            if (kart.rescue > 0) {
                // the rescue claw lowering it back onto the track
                f32 top = d.sy - lift - (f32)KART_ANCHOR_Y * s;
                for (int y = 0; y < (int)top; ++y) put(f, (int)d.sx, y, rgb(32, 32, 40));
                fill_rect(f, (int)(d.sx - 6.0f * s), (int)(top - 2.0f * s), (int)(12.0f * s) + 1, (int)(2.0f * s) + 1, rgb(232, 200, 64));
            }
            blit(f, k_kart_px[kart.ch][idx], KART_SIZE, KART_SIZE, pal, d.sx - (f32)KART_ANCHOR_X * ks, d.sy - lift - (f32)KART_ANCHOR_Y * ks, ks);
            if (kart.shield > 0 && (kart.shield > 1.5f || (tick & 2)))
                draw_ring(f, d.sx, d.sy - lift - 10.0f * s, 15.0f * s, (tick & 4) ? rgb(140, 255, 230) : rgb(255, 255, 255));
            if (kart.stun > 0 && (tick & 2)) draw_ring(f, d.sx, d.sy - lift - 10.0f * s, 12.0f * s, rgb(120, 232, 255));
        } break;
        }
    }
    if (r.emp_flash > 0) {
        f32 a = r.emp_flash / 0.45f;
        for (int i = 0; i < f.w * f.h; ++i) f.px[i] = blend(f.px[i], rgb(200, 240, 255), a * 0.7f);
    }
}

} // namespace sr
