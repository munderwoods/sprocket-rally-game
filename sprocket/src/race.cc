// The race: kart physics (grip, hop-drift with charged mini-turbos, boost pads, off-road,
// mud and ice, falling off open edges and into water), nine items from item crystals, bolts
// that raise top speed, the track's set piece, CPU drivers with personalities, laps and
// placings.
#include "common.h"

namespace sr {

const Character k_chars[RACERS] = {
    {"BRUNO WRENCH", "TOW TRUCK", {"BULLDOG MECHANIC. FIXES", "ANYTHING BY HITTING IT."},
     {rgb(232, 120, 40), rgb(56, 88, 184)}, 1.05f, 0.90f, 0.94f, 1.5f, 0.25f, 1.00f, 0.95f, 0.85f, 0.35f, IT_BOMB},
    {"PIP-8", "HOVER POD", {"TINY ROBOT. RUNS ON OPTIMISM", "AND TWO AA BATTERIES."},
     {rgb(160, 232, 248), rgb(184, 192, 208)}, 0.95f, 1.12f, 1.10f, 0.6f, 0.55f, 1.00f, 1.10f, 0.20f, 0.90f, IT_NITRO3},
    {"MADAME HEX", "CAULDRON CART", {"WITCH. THE CAULDRON WAS", "FOR SOUP. ONCE."},
     {rgb(152, 72, 200), rgb(96, 216, 96)}, 1.00f, 1.00f, 1.02f, 1.0f, 0.10f, 1.00f, 1.00f, 0.70f, 0.50f, IT_FAKE},
    {"CAPN GULLBEARD", "ROWBOAT RACER", {"SEAGULL PIRATE. STEALS CHIPS,", "ITEMS AND CHECKERED FLAGS."},
     {rgb(232, 232, 232), rgb(232, 184, 40)}, 1.00f, 0.98f, 1.00f, 1.1f, 0.15f, 1.00f, 1.00f, 0.65f, 0.50f, IT_MAGNET},
    {"TINY REX", "BONE BUGGY", {"BABY T-REX. CANT REACH THE", "PEDALS. DRIVES ANYWAY."},
     {rgb(88, 184, 64), rgb(232, 224, 200)}, 0.98f, 1.05f, 0.98f, 1.2f, 0.65f, 1.00f, 1.00f, 0.50f, 0.40f, IT_ROCKET},
    {"VOLT VEGA", "LIGHT CYCLE", {"HACKER. OVERCLOCKED HER BIKE.", "AND HER COFFEE."},
     {rgb(248, 48, 200), rgb(64, 232, 248)}, 1.06f, 0.96f, 1.00f, 0.8f, 0.00f, 1.30f, 1.05f, 0.50f, 0.80f, IT_EMP},
    {"SIR CLANKS", "WAR CHARIOT", {"KNIGHT. HAS NOT TAKEN THE", "ARMOR OFF SINCE 1192."},
     {rgb(184, 192, 208), rgb(200, 40, 48)}, 1.02f, 0.92f, 0.95f, 1.6f, 0.20f, 0.95f, 0.95f, 0.40f, 0.30f, IT_SHIELD},
    {"GRANNY GOGGLES", "ROCKET SCOOTER", {"92 YEARS YOUNG. HAS NEVER", "ONCE TOUCHED THE BRAKE."},
     {rgb(216, 48, 48), rgb(240, 224, 184)}, 1.01f, 1.04f, 1.03f, 0.8f, 0.10f, 1.10f, 1.25f, 0.30f, 1.00f, IT_NITRO},
};

const char* item_name(int item) {
    static const char* k[IT_COUNT] = {"", "NITRO", "TRIPLE NITRO", "BOMB", "ROCKET", "OIL", "FAKE CRYSTAL", "SHIELD", "EMP", "MAGNET"};
    return (item > 0 && item < IT_COUNT) ? k[item] : "";
}

// tuning, texels and seconds
constexpr f32 TOP = 168.0f, ACCEL = 118.0f, BRAKE = 260.0f, REVERSE_MAX = 45.0f;
constexpr f32 TURN = 2.35f, GRIP = 11.0f;
constexpr f32 KART_R = 4.5f;
constexpr f32 HOP_V = 58.0f, GRAVITY = 420.0f;
// Drifting follows SRB2Kart's model, scaled to these tracks: while drifting the kart's body
// turns at a rate set by the drift (steering only bends it: outward 25%, neutral ~62%,
// inward 100% of normal full lock) and its thrust points DRIFT_ANGLE outward of where the
// body faces, so the body hangs into the corner while the line you travel bends gently.
// Holding it charges three tiers of mini-turbo; releasing swings the body back in line.
constexpr f32 DRIFT_ANGLE = 0.52f;      // 30 degrees: two sprite steps
constexpr f32 DRIFT_RAMP = 0.15f;       // seconds for the body to swing in or out
constexpr f32 CHARGE_1 = 1.0f, CHARGE_2 = 2.0f, CHARGE_3 = 3.4f;
constexpr f32 TURBO_1 = 0.45f, TURBO_2 = 0.9f, TURBO_3 = 1.6f;
constexpr f32 DRIFT_MIN_SPEED = 60.0f;

static vec2 dir(f32 a) { return vec2{kv::cos(a), kv::sin(a)}; }

// How far (squared) a place is from the nearest player: what any of them could hear or feel.
static f32 human_d2(const Race& r, vec2 p) {
    f32 best = 1e18f;
    for (int hu = 0; hu < r.humans; ++hu) {
        int i = hu == 0 ? r.player : r.human_idx[hu];
        if (i < 0) continue;
        f32 d = length_sq(r.racers[i].pos - p);
        if (d < best) best = d;
    }
    return best;
}

// Sound for something that happened to racer k (or at a place, if k is null): only in a
// real race, and only when it is a player's or close enough to one of them to hear.
static void sfx(const Race& r, int id, const Racer* k, vec2 at = vec2{}, f32 range = 110.0f) {
    if (r.player < 0) return;
    if (k && k->human) { snd_sfx(id); return; }
    if (human_d2(r, k ? k->pos : at) < range * range) snd_sfx(id);
}

static void spawn_part(Race& r, vec2 p, f32 z, vec2 v, f32 vz, f32 life, u32 color, f32 size, bool ground) {
    if (r.parts.len >= 900) return;
    r.parts.push(Particle{p, v, z, vz, life, life, color, size, ground});
}

static bool solid_at(const Race& r, vec2 p) {
    u8 s = track_surface(r.track, p.x, p.y);
    if (s == S_WALL || s == S_OUT) return true; // pits and water are open: you fall in
    for (const Prop& pr : r.track.props)
        if (pr.solid && length_sq(pr.pos - p) < 25.0f) return true;
    return false;
}

// The way out of whatever is solid around p: the average direction of the free samples on
// a small ring. Zero if p is boxed in.
static vec2 wall_normal(const Race& r, vec2 p) {
    vec2 n{};
    for (int i = 0; i < 12; ++i) {
        vec2 d = dir(6.2831853f * (f32)i / 12.0f);
        if (!solid_at(r, p + d * 5.0f)) n += d;
    }
    f32 l = length(n);
    return l > 0.01f ? n / l : vec2{};
}

static void hit(Race& r, Racer& k) {
    if (k.spin > 0 || k.invuln > 0) return;
    if (k.shield > 0) { k.shield = 0; sfx(r, SFX_WALL, &k); return; }
    sfx(r, SFX_HIT, &k);
    k.stat_hits++;
    if (k.human) r.shake = mxf(r.shake, 0.3f);
    k.spin = 1.1f;
    if (!k.human && r.cpu_level > 1.0f) k.spin -= 0.35f * (r.cpu_level - 1.0f); // hard rivals shake a hit off sooner
    k.invuln = 1.1f + 1.0f; // spin, then a second to drive clear
    k.vel *= 0.35f;
    k.drifting = false; k.drift_charge = 0; k.boost = 0; k.magnet = 0;
    int lose = k.bolts < 3 ? k.bolts : 3;
    k.bolts -= lose;
    // what it lost spills onto the road, there for anyone to pick up for a few seconds
    for (int i = 0; i < lose; ++i) {
        Bolt b{k.pos, 0};
        b.life = 6.0f;
        b.vel = dir(r.rng.range(0.0f, 6.283f)) * r.rng.range(45.0f, 85.0f);
        b.z = 6.0f; b.vz = r.rng.range(70.0f, 120.0f);
        r.track.bolts.push(b);
    }
    for (int i = 0; i < 14; ++i) {
        f32 a = r.rng.range(0.0f, 6.283f);
        spawn_part(r, k.pos, 4, dir(a) * r.rng.range(20.0f, 70.0f), r.rng.range(40.0f, 120.0f), 0.7f, i & 1 ? rgb(255, 200, 64) : rgb(255, 255, 255), 2, false);
    }
}

static int roll_item(Race& r, const Racer& k) {
    // weights by place: leaders get defensive items, the back of the pack gets speed and
    // the big ones; every character rolls its favorite more often
    f32 t = (f32)k.place / (f32)(RACERS - 1);
    f32 w[IT_COUNT] = {0,
        12 + 20 * t,              // nitro
        2 + 18 * t,               // triple nitro
        22 - 6 * t,               // bomb
        4 + 14 * t,               // rocket
        26 - 22 * t,              // oil
        18 - 14 * t,              // fake crystal
        12 + 4 * t,               // shield
        mxf(0.0f, 12 * t - 6),    // emp: only the back half
        2 + 10 * t};              // magnet
    w[k_chars[k.ch].favorite] *= 1.8f;
    f32 sum = 0;
    for (int i = 1; i < IT_COUNT; ++i) sum += w[i];
    f32 x = r.rng.range(0.0f, sum);
    for (int i = 1; i < IT_COUNT; ++i) {
        if (x < w[i]) return i;
        x -= w[i];
    }
    return IT_NITRO;
}

static int racer_at_place(const Race& r, int place) {
    for (int i = 0; i < RACERS; ++i) if (r.racers[i].place == place) return i;
    return -1;
}

static void use_item(Race& r, Racer& k, int idx) {
    vec2 f = dir(k.heading);
    int item = k.item;
    switch (item) {
    case IT_NITRO: k.boost = mxf(k.boost, 1.3f); sfx(r, SFX_BOOST, &k); break;
    case IT_NITRO3:
        k.boost = mxf(k.boost, 1.1f); sfx(r, SFX_BOOST, &k);
        if (--k.item_uses > 0) return; // keep the rest
        break;
    case IT_SHIELD: k.shield = 7.0f; sfx(r, SFX_SHIELD, &k); break;
    case IT_OIL: r.oils.push(Hazard{k.pos - f * 12.0f, 40.0f, idx, HZ_OIL, 0}); sfx(r, SFX_THROW, &k); break;
    case IT_FAKE: r.track.boxes.push(ItemBox{k.pos - f * 14.0f, 0, 0, 0, 30.0f, true}); sfx(r, SFX_THROW, &k); break;
    case IT_BOMB:
        r.bombs.push(Projectile{k.pos + f * 8.0f, f * 250.0f + k.vel * 0.3f, 4.0f, 6.0f, 0, idx, PK_BOMB, -1, vec2{}});
        sfx(r, SFX_THROW, &k);
        break;
    case IT_ROCKET: {
        int target = k.place > 0 ? racer_at_place(r, k.place - 1) : -1;
        r.bombs.push(Projectile{k.pos + f * 8.0f, f * 230.0f, 6.0f, 8.0f, 0, idx, PK_ROCKET, target, vec2{}});
        sfx(r, SFX_ROCKET, &k);
    } break;
    case IT_EMP: {
        // every racer ahead is shocked: slowed, drift lost
        r.emp_flash = 0.45f;
        for (int j = 0; j < RACERS; ++j) {
            Racer& o = r.racers[j];
            if (j == idx || o.place >= k.place) continue;
            if (o.shield > 0) { o.shield = 0; continue; }
            o.stun = 1.8f; o.drifting = false; o.drift_charge = 0; o.boost = 0;
        }
        snd_sfx(SFX_ZAP);
    } break;
    case IT_MAGNET: {
        int target = k.place > 0 ? racer_at_place(r, k.place - 1) : -1;
        if (target >= 0) { k.magnet = 2.5f; k.magnet_target = target; }
        sfx(r, SFX_MAGNET, &k);
    } break;
    default: break;
    }
    k.item = IT_NONE;
    k.item_uses = 0;
}

// ---- AI -------------------------------------------------------------------------------------
// Whether a CPU driver would want to put a wheel here.
static bool lane_clear(const Race& r, f32 s, f32 lat) {
    vec2 tan;
    vec2 c = track_point(r.track, s, &tan);
    vec2 p = c + vec2{-tan.y, tan.x} * (lat * track_half_width(r.track, s));
    u8 su = track_surface(r.track, p.x, p.y);
    if (!(su == S_ROAD || su == S_BOOST || su == S_RAMP || su == S_ICE || su == S_SHALLOW)) return false;
    for (const Prop& pr : r.track.props)
        if (pr.solid && length_sq(pr.pos - p) < 100.0f) return false;
    return true;
}

static void ai_controls(Race& r, Racer& k, int idx, f32 dt) {
    Controls c;
    k.ai_t += dt;
    const Character& ch = k_chars[k.ch];
    // difficulty: -1 easy, 0 normal, +1 hard. Everything it scales is exactly as it was at 0.
    const f32 dif = k.human ? 0.0f : r.cpu_level - 1.0f, easy = dif < 0 ? -dif : 0.0f, hard = dif > 0 ? dif : 0.0f;
    // (where there is nothing to catch a kart that runs wide, hard rivals do not push their luck in corners)
    const f32 bold = r.track.theme->road == RS_SPACE ? 0.0f : hard;
    f32 speed = length(k.vel);
    f32 look = 46.0f + speed * 0.28f;
    f32 lane = k.lane * 0.6f + 0.25f * kv::sin(k.ai_t * 0.37f + (f32)idx * 1.7f);
    if (r.track.theme->road == RS_SPACE) lane *= 0.55f; // nothing to catch you out here: keep to the middle
    // dodge what the set piece has put in the road ahead (the snowball, a leg, a bus)
    {
        vec2 f = dir(k.move_angle), rt{-f.y, f.x};
        for (const SetBody& b : r.track.sp.bodies) {
            if (!b.solid) continue;
            vec2 d = b.pos - k.pos;
            f32 fw = dot(d, f), lat = dot(d, rt);
            if (fw > 0 && fw < 230.0f && kv::fabs(lat) < b.radius + 26.0f) lane = lat > 0 ? -0.7f : 0.7f;
            if (fw > 6.0f && fw < 55.0f && kv::fabs(lat) < b.radius + 7.0f && speed > 70.0f) c.brake = true;
        }
    }
    // pick the nearest lane that is clean at the aim point and half way to it
    {
        static const f32 k_try[] = {0.0f, 0.3f, -0.3f, 0.55f, -0.55f, 0.8f, -0.8f};
        f32 best = lane;
        bool found = false;
        for (f32 off : k_try) {
            f32 cand = clampf(lane + off, -0.82f, 0.82f);
            if (lane_clear(r, k.s + look, cand) && lane_clear(r, k.s + look * 0.5f, cand)) { best = cand; found = true; break; }
        }
        if (found) lane = best;
    }
    vec2 tan;
    vec2 target = track_point(r.track, k.s + look, &tan);
    vec2 nrm{-tan.y, tan.x};
    target += nrm * (lane * track_half_width(r.track, k.s + look));
    vec2 to = target - k.pos;
    f32 want = kv::atan2(to.y, to.x);
    f32 err = wrap_pi(want - k.move_angle);
    c.steer = clampf(err * 2.6f, -1.0f, 1.0f);
    // the aggressive ones lean on whoever is alongside
    // (on hard nearly everyone does; on easy nobody)
    if (ch.aggression > 0.6f - 0.35f * bold + 2.0f * easy && !k.drifting) {
        vec2 f = dir(k.heading), rt{-f.y, f.x};
        for (int j = 0; j < RACERS; ++j) {
            if (j == idx) continue;
            vec2 d = r.racers[j].pos - k.pos;
            f32 fw = dot(d, f), lat = dot(d, rt);
            if (kv::fabs(fw) < 10.0f && kv::fabs(lat) < 16.0f && kv::fabs(lat) > 4.0f) c.steer = clampf(c.steer + (lat > 0 ? 0.35f : -0.35f), -1.0f, 1.0f);
        }
    }
    // read the corner ahead and arrive at a speed that makes it
    vec2 t1, t2;
    track_point(r.track, k.s + 18.0f, &t1);
    track_point(r.track, k.s + 18.0f + 0.42f * speed + 30.0f, &t2);
    f32 bend = kv::fabs(wrap_pi(kv::atan2(t2.y, t2.x) - kv::atan2(t1.y, t1.x))) / (0.42f * speed + 30.0f); // radians per texel
    // easy rivals lift early for corners; hard ones carry a little more speed through
    f32 safe = bend > 0.0005f ? (TURN * ch.turn * (1.22f - 0.16f * easy + 0.03f * bold)) / bend : 1e9f;
    bool ramp_ahead = false;
    for (f32 q = 0; q < 90.0f; q += 15.0f) {
        vec2 p = track_point(r.track, k.s + q, nullptr);
        if (track_surface(r.track, p.x, p.y) == S_RAMP) ramp_ahead = true;
    }
    c.accel = speed < safe || ramp_ahead;
    c.brake = c.brake || (speed > safe * 1.18f && !ramp_ahead) || (kv::fabs(err) > 1.3f && speed > 60.0f);
    // drift through long bends for mini-turbos
    // (easy rivals hardly ever drift; hard ones drift sooner and hold on for the orange sparks)
    if (!k.ai_drift && kv::fabs(err) > 0.38f - 0.18f * ch.drift_bias + 0.3f * easy - 0.07f * bold && speed > 110.0f && !k.drifting && !ramp_ahead && !c.brake) { k.ai_drift = true; c.hop_pressed = true; }
    if (k.ai_drift) {
        c.hop = true;
        f32 into = err * (f32)k.drift_dir;
        if (k.drifting && (into < -0.04f || (into < 0.03f && k.drift_charge >= (bold > 0 && into > 0.0f ? CHARGE_2 : CHARGE_1)) || k.drift_charge > CHARGE_3 + 0.1f)) { k.ai_drift = false; c.hop = false; }
        if (!k.drifting && !k.airborne && !c.hop_pressed) k.ai_drift = false;
    }
    // items, by personality
    if (k.item != IT_NONE && k.roulette <= 0) {
        k.item_wait -= dt * (1.0f - 0.55f * easy + 0.4f * hard); // easy rivals sit on an item; hard ones are quick with it
        bool go = false;
        vec2 f = dir(k.heading), rt{-f.y, f.x};
        bool ahead_in_cone = false, someone_behind = false;
        for (int j = 0; j < RACERS; ++j) {
            if (j == idx) continue;
            vec2 d = r.racers[j].pos - k.pos;
            f32 fw = dot(d, f), lat = dot(d, rt);
            if (fw > 15 && fw < 160 && kv::fabs(lat) < 18) ahead_in_cone = true;
            if (fw < -8 && fw > -70 && kv::fabs(lat) < 20) someone_behind = true;
        }
        switch (k.item) {
        case IT_SHIELD: go = k.item_wait < 1.0f - 2.5f * easy; break;
        case IT_NITRO: case IT_NITRO3: go = k.item_wait < 0 && kv::fabs(err) < 0.2f; break;
        case IT_OIL: case IT_FAKE: go = (someone_behind && k.item_wait < 0) || k.item_wait < -5.0f; break;
        case IT_BOMB: go = (ahead_in_cone && (easy <= 0 || k.item_wait < 0)) || k.item_wait < -6.0f; break;
        case IT_ROCKET: go = k.item_wait < 0 && k.place > 0; break;
        case IT_EMP: go = k.item_wait < 0; break;
        case IT_MAGNET: {
            int t = k.place > 0 ? racer_at_place(r, k.place - 1) : -1;
            go = t >= 0 && k.item_wait < 0 && length_sq(r.racers[t].pos - k.pos) < 200.0f * 200.0f;
        } break;
        default: break;
        }
        if (go) c.item_pressed = true;
    }
    k.in = c;
}

// ---- physics --------------------------------------------------------------------------------
static void kart_tick(Race& r, Racer& k, int idx, f32 dt) {
    const Character& ch = k_chars[k.ch];
    Controls c = k.in;
    bool can_drive = r.phase != RacePhase::Countdown && !k.finished;
    if (k.finished && !k.human) can_drive = true; // keep driving after the flag
    if (k.finished && k.human) { // autopilot after the flag
        Controls keep = k.in;
        ai_controls(r, k, idx, dt);
        c = k.in; c.item_pressed = false; c.hop = false; c.hop_pressed = false;
        k.in = keep;
        can_drive = true;
    }
    if (!can_drive) c = Controls{};
    if (k.spin > 0) { c = Controls{}; k.spin -= dt; }
    if (k.invuln > 0) k.invuln -= dt;

    // over the edge or into the water: drop, then the rescue claw sets the kart back down
    if (k.fall > 0) {
        k.fall -= dt;
        k.vel *= kv::exp(-2.5f * dt);
        k.pos += k.vel * dt;
        k.z -= (k.fall_kind == S_PIT ? 140.0f : 28.0f) * dt;
        if (k.fall_kind == S_WATER && r.rng.chance(0.4f))
            spawn_part(r, k.pos, 0, vec2{r.rng.range(-30.0f, 30.0f), r.rng.range(-30.0f, 30.0f)}, r.rng.range(30.0f, 70.0f), 0.4f, rgb(200, 232, 255), 2, false);
        if (k.fall <= 0) {
            vec2 tan;
            // set down a little before where it left the road, on the first solid stretch
            // going back (never in a gap, never on the lip of one)
            f32 back = k.last_safe_s - 25.0f;
            for (int tries = 0; tries < 60; ++tries, back -= 8.0f) {
                bool ok = true;
                for (f32 q = -10.0f; q <= 10.0f; q += 10.0f) {
                    vec2 p = track_point(r.track, back + q, nullptr);
                    u8 su = track_surface(r.track, p.x, p.y);
                    if (!(su == S_ROAD || su == S_BOOST || su == S_ICE || su == S_MUD || su == S_SHALLOW)) ok = false;
                }
                if (ok) break;
            }
            k.last_safe_s = back;
            k.pos = track_point(r.track, back, &tan);
            k.heading = k.move_angle = kv::atan2(tan.y, tan.x);
            k.vel = vec2{};
            k.z = 46.0f; k.vz = 0;
            k.rescue = 1.0f;
            sfx(r, SFX_RESCUE, &k);
            k.s = track_project(r.track, k.pos, &k.seg, (int)r.track.line.len / 2);
        }
        return;
    }
    if (k.rescue > 0) {
        k.rescue -= dt;
        k.z = mxf(0.0f, 46.0f * (k.rescue - 0.2f) / 0.8f);
        if (k.rescue > 0.15f) c = Controls{};
    }

    // the frame the engine pushes along: the body's heading, less the drift's outward angle
    vec2 f = dir(k.heading - (f32)k.drift_dir * DRIFT_ANGLE * k.drift_ramp), rt{-f.y, f.x};
    f32 vf = dot(k.vel, f), vl = dot(k.vel, rt);
    u8 surf = track_surface(r.track, k.pos.x, k.pos.y);
    if (k.airborne) surf = S_ROAD; // nothing underfoot matters in the air
    bool off = surf == S_OFF && k.boost <= 0;
    bool mud = surf == S_MUD && k.boost <= 0;
    bool ice = surf == S_ICE;
    bool ford = surf == S_SHALLOW;
    bool launch = surf == S_RAMP && vf > 35.0f;
    if (surf == S_BOOST) { if (k.boost < 0.5f) sfx(r, SFX_BOOST, &k); k.boost = mxf(k.boost, 0.9f); }

    // difficulty for the rivals' pace: e is how easy (0..1), h how hard (0..1); both 0 on normal.
    // Out in space, where a fast kart simply flies off the road, hard adds no pace.
    const f32 e = r.cpu_level < 1.0f ? 1.0f - r.cpu_level : 0.0f;
    const f32 h = r.cpu_level > 1.0f && r.track.theme->road != RS_SPACE ? r.cpu_level - 1.0f : 0.0f;
    // (on hard the slower rivals are brought most of the way up to the best of them)
    f32 top = TOP * ch.top * (1.0f + 0.012f * (f32)k.bolts) * (k.human ? 1.0f : (h > 0 ? k.skill + (1.0f - k.skill) * 0.7f * h : k.skill));
    if (!k.human && r.player >= 0) {
        // rubber band against the player
        f32 ref = r.racers[r.player].total; // with two players, the one in front
        for (int hu = 1; hu < r.humans; ++hu) if (r.racers[r.human_idx[hu]].total > ref) ref = r.racers[r.human_idx[hu]].total;
        f32 gap = (k.total - ref) / r.track.length;
        // (how far a leader eases off for you: more on easy, hardly at all on hard)
        // normal: a leader eases off by up to 10% and a straggler gains up to 7%. Easy leaders
        // wait more and stragglers get no help; hard leaders barely wait and stragglers fly.
        top *= clampf(1.0f - gap * 0.35f, 0.9f - 0.05f * e + 0.08f * h, (1.03f + 0.04f * 1.0f) - 0.07f * e + 0.05f * h);
    }
    if (off) top *= 0.5f + 0.3f * ch.offroad;
    if (mud) top *= 0.6f + 0.25f * ch.offroad;
    if (ford) top *= 0.74f + 0.16f * ch.offroad;
    if (k.draft > 1.0f) top *= 1.07f; // a tow from the kart ahead
    if (!k.human) top *= (0.955f + 0.045f * 1.0f) - 0.085f * e + 0.09f * h; // normal's own figure, less easy's cut or plus hard's gain
    if (k.stun > 0) { top *= 0.55f; k.stun -= dt; }
    if (k.magnet > 0) {
        k.magnet -= dt;
        top *= 1.25f;
        if (k.magnet_target >= 0) {
            Racer& t = r.racers[k.magnet_target];
            vec2 d = t.pos - k.pos;
            f32 dist = length(d);
            if (dist > 1.0f) k.vel += d * (160.0f * dt / dist);
            if (dist < 11.0f) { // latched: take their item if our hands are free
                if (k.item == IT_NONE && t.item != IT_NONE && t.roulette <= 0) { k.item = t.item; k.item_uses = t.item_uses; t.item = IT_NONE; t.item_uses = 0; sfx(r, SFX_ITEM_GET, &k); }
                k.magnet = 0;
            }
        }
    }
    f32 accel = ACCEL * ch.accel * (k.human ? 1.0f : 1.0f - 0.06f * e + 0.10f * h); // hard rivals pick up speed sooner out of corners and after a hit
    if (k.boost > 0) { top *= 1.38f; accel *= 3.0f; k.boost -= dt; }

    // engine
    if (c.accel && !c.brake) {
        if (vf < top) vf += accel * (1.0f - mxf(vf, 0.0f) / top * 0.75f) * dt;
        else vf -= (vf - top) * 2.0f * dt;
    } else if (c.brake) {
        if (vf > 0) vf -= BRAKE * dt;
        else if (vf > -REVERSE_MAX) vf -= ACCEL * 0.6f * dt;
    } else {
        vf -= vf * 0.9f * dt;
    }
    if (off) vf -= vf * 1.4f * (1.0f - 0.5f * ch.offroad) * dt;
    if (mud) vf -= vf * 1.0f * (1.0f - 0.5f * ch.offroad) * dt;
    if (ford) vf -= vf * 0.5f * dt;
    if (k.surf > 0) { // on the wave: it carries the kart at its own pace
        k.surf -= dt;
        vf = mxf(vf, 238.0f);
        if (r.rng.chance(0.8f)) spawn_part(r, k.pos - f * 5.0f + rt * r.rng.range(-6.0f, 6.0f), 1, -f * 30.0f + rt * r.rng.range(-30.0f, 30.0f), r.rng.range(30.0f, 80.0f), 0.3f, (r.rng.next() & 1) ? rgb(232, 248, 240) : rgb(112, 184, 120), 2, false);
        if (k.surf <= 0) k.boost = mxf(k.boost, 0.8f); // and throws it clear at the end
    }
    if (launch) {
        // a ramp throws the kart up and makes sure it carries the gap, whatever it arrived at
        vf = mxf(vf, 152.0f);
        // (where gravity is weak the throw is gentler, so the flight is only a little longer)
        k.airborne = true; k.ramp_air = true; k.vz = 132.0f * (r.track.theme->gravity < 1.0f ? r.track.theme->gravity * 1.3f : 1.0f); k.air_time = 0;
        if (k.drifting) { k.drifting = false; k.drift_charge = 0; }
        sfx(r, SFX_RAMP, &k);
    }

    // hop; and, as in SRB2Kart, holding the hop button is asking for a drift: it starts the
    // moment there is a direction to drift in, on landing or any time after, not only if the
    // stick happened to be over as the wheels touched down
    {
        f32 yr = wrap_pi(k.heading - k.yaw_prev) / dt; // which way the kart has been turning lately
        k.yaw_prev = k.heading;
        if (!k.drifting && k.drift_ramp <= 0.0f) k.yaw_avg += (yr - k.yaw_avg) * mnf(1.0f, 6.0f * dt);
    }
    bool landed = false;
    if (c.hop_pressed && !k.airborne && !k.drifting && vf > 30.0f && k.spin <= 0) { k.airborne = true; k.vz = HOP_V * (r.track.theme->gravity < 1.0f ? r.track.theme->gravity * 1.3f : 1.0f); if (k.human) sfx(r, SFX_HOP, &k); }
    if (k.airborne) {
        k.z += k.vz * dt; k.vz -= GRAVITY * r.track.theme->gravity * dt;
        k.air_time += dt;
        if (k.z <= 0) {
            k.z = 0; k.vz = 0; k.airborne = false;
            if (k.ramp_air) { sfx(r, SFX_LAND, &k); k.ramp_air = false; for (int q = 0; q < 6; ++q) spawn_part(r, k.pos, 0, dir(r.rng.range(0.0f, 6.283f)) * 30.0f, 30.0f, 0.3f, rgb(232, 232, 232), 2, false); }
            landed = true;
        }
    }
    if (c.hop && !k.airborne && !k.drifting && k.spin <= 0 && vf > DRIFT_MIN_SPEED) {
        int want = 0;
        if (kv::fabs(c.steer) > 0.05f) want = c.steer > 0 ? 1 : -1;
        else if (landed && kv::fabs(k.yaw_avg) > 0.12f) want = k.yaw_avg > 0 ? 1 : -1; // no stick, but already curving: that way
        if (want) { k.drifting = true; k.drift_dir = want; k.drift_charge = 0; }
    }
    if (k.drifting) {
        if (!c.hop || k.spin > 0) {
            // release: the mini-turbo for the tier reached
            f32 turbo = ch.turbo * (k.drift_charge >= CHARGE_3 ? TURBO_3 : (k.drift_charge >= CHARGE_2 ? TURBO_2 : (k.drift_charge >= CHARGE_1 ? TURBO_1 : 0.0f)));
            if (turbo > 0 && k.spin <= 0) { k.boost = mxf(k.boost, turbo); sfx(r, SFX_MINITURBO, &k); }
            k.drifting = false; k.drift_charge = 0;
        } else if (vf < DRIFT_MIN_SPEED * 0.6f) {
            k.drifting = false; k.drift_charge = 0; // too slow: the drift falls apart, no turbo
        } else {
            f32 cs = c.steer * (f32)k.drift_dir; // countersteer: +1 into the drift, -1 out of it
            f32 before = k.drift_charge;
            f32 rate = 1.0f + 0.35f * mxf(cs, 0.0f) - 0.45f * mxf(-cs, 0.0f);
            if (!off && !mud) k.drift_charge += rate * ch.charge * dt;
            if (k.human) {
                if ((before < CHARGE_1 && k.drift_charge >= CHARGE_1) || (before < CHARGE_2 && k.drift_charge >= CHARGE_2) ||
                    (before < CHARGE_3 && k.drift_charge >= CHARGE_3)) sfx(r, SFX_CHARGE, &k);
                if ((int)(before * 6.0f) != (int)(k.drift_charge * 6.0f)) sfx(r, SFX_SKID, &k);
            }
        }
    }
    // the body swings into the drift and back out of it without changing the line travelled
    {
        f32 old = k.drift_ramp;
        f32 target = k.drifting ? 1.0f : 0.0f;
        k.drift_ramp += clampf(target - old, -dt / DRIFT_RAMP, dt / DRIFT_RAMP);
        k.heading += (f32)k.drift_dir * DRIFT_ANGLE * (k.drift_ramp - old);
        if (k.drift_ramp <= 0.0f && !k.drifting) k.drift_dir = 0;
    }

    // steering
    f32 speed_k = clampf(kv::fabs(vf) / 55.0f, 0.0f, 1.0f);
    f32 steer = c.steer;
    if (k.drifting) {
        f32 cs = c.steer * (f32)k.drift_dir;
        steer = (f32)k.drift_dir * (0.625f + 0.375f * cs);
    }
    f32 turn = TURN * ch.turn * (k.airborne ? 0.6f : 1.0f);
    k.heading += steer * turn * speed_k * (vf < 0 ? -1.0f : 1.0f) * dt;
    // a spin-out is drawn (render.cc) but never changes where the kart is heading

    // grip, in the frame of the new push direction: sideways motion bleeds away, so the
    // kart's path bends toward where the engine pushes it
    k.vel = f * vf + rt * vl;
    f = dir(k.heading - (f32)k.drift_dir * DRIFT_ANGLE * k.drift_ramp);
    rt = vec2{-f.y, f.x};
    vf = dot(k.vel, f); vl = dot(k.vel, rt);
    f32 grip = GRIP * (k.drifting ? 0.75f : 1.0f) * r.track.theme->grip * (ice ? 0.35f : 1.0f);
    if (k.airborne) grip *= 0.25f;
    vl *= kv::exp(-grip * dt);
    k.vel = f * vf + rt * vl;
    k.move_angle = kv::atan2(f.y, f.x);

    // move; against a wall, keep the speed along it and lose what went into it
    vec2 np = k.pos + k.vel * dt;
    if (!solid_at(r, np)) k.pos = np;
    else {
        vec2 n = wall_normal(r, np);
        f32 into = dot(k.vel, n);
        if (length_sq(n) < 0.5f) { k.vel *= -0.3f; } // boxed in: back out
        else {
            if (into < 0) k.vel -= n * (into * 1.25f); // a little bounce off
            k.vel *= 0.985f;                            // scrubbing along it
            vec2 slide = k.pos + k.vel * dt;
            if (!solid_at(r, slide)) k.pos = slide;
            else if (!solid_at(r, k.pos + n * 1.5f)) k.pos += n * 1.5f;
        }
        if (into < -55.0f) { // a real hit
            if (k.human) sfx(r, SFX_WALL, &k);
            if (k.drifting) { k.drifting = false; k.drift_charge = 0; }
            for (int q = 0; q < 4; ++q) spawn_part(r, k.pos, 3, n * 40.0f + vec2{r.rng.range(-20.0f, 20.0f), r.rng.range(-20.0f, 20.0f)}, 40.0f, 0.25f, rgb(255, 232, 160), 1, false);
        }
    }

    // falling in
    {
        u8 now = track_surface(r.track, k.pos.x, k.pos.y);
        if ((now == S_PIT || now == S_WATER) && !k.airborne && k.rescue <= 0) {
            k.fall = 1.1f; k.fall_kind = now;
            k.stat_falls++;
            k.drifting = false; k.drift_charge = 0; k.boost = 0; k.drift_ramp = 0; k.drift_dir = 0;
            sfx(r, now == S_WATER ? SFX_SPLASH : SFX_FALL, &k);
            return;
        }
        if ((now == S_ROAD || now == S_BOOST || now == S_ICE || now == S_MUD) && !k.airborne) k.last_safe_s = k.s;
    }

    // effects
    if (ford && kv::fabs(vf) > 30.0f && r.rng.chance(0.7f))
        spawn_part(r, k.pos - f * 3.0f, 1, -f * 20.0f + rt * r.rng.range(-30.0f, 30.0f), r.rng.range(30.0f, 60.0f), 0.35f, rgb(200, 232, 255), 2.0f, false);
    if (mud && kv::fabs(vf) > 30.0f && r.rng.chance(0.6f))
        spawn_part(r, k.pos - f * 4.0f, 1, -f * 25.0f + rt * r.rng.range(-20.0f, 20.0f), 35.0f, 0.4f, r.track.theme->pal[C_MUD1], 2.0f, false);
    if (k.stun > 0 && r.rng.chance(0.5f))
        spawn_part(r, k.pos, 6, vec2{r.rng.range(-20.0f, 20.0f), r.rng.range(-20.0f, 20.0f)}, 40.0f, 0.2f, (r.rng.next() & 1) ? rgb(120, 232, 255) : rgb(255, 255, 255), 1.0f, false);
    if (k.drifting && !k.airborne) {
        u32 col = k.drift_charge >= CHARGE_3 ? ((r.rng.next() & 1) ? rgb(208, 96, 255) : rgb(255, 120, 232))
                : k.drift_charge >= CHARGE_2 ? rgb(255, 144, 32) : (k.drift_charge >= CHARGE_1 ? rgb(80, 168, 255) : rgb(232, 232, 232));
        vec2 bf = dir(k.heading), brt{-bf.y, bf.x};
        for (int side = -1; side <= 1; side += 2) {
            vec2 wheel = k.pos - bf * 4.0f + brt * (3.0f * (f32)side);
            if (r.rng.chance(0.7f))
                spawn_part(r, wheel, 1, rt * r.rng.range(-25.0f, 25.0f) - f * 20.0f, r.rng.range(20.0f, 60.0f), 0.25f, col, k.drift_charge >= CHARGE_1 ? 2.0f : 1.0f, false);
        }
    }
    if (off && kv::fabs(vf) > 40.0f && r.rng.chance(0.5f))
        spawn_part(r, k.pos - f * 4.0f, 1, -f * 20.0f + rt * r.rng.range(-15.0f, 15.0f), 25.0f, 0.4f, r.track.theme->pal[C_OFF2], 2.0f, false);
    if (k.boost > 0 && r.rng.chance(0.8f))
        spawn_part(r, k.pos - f * 6.0f, 3, -f * 40.0f, 10.0f, 0.18f, r.rng.chance(0.5f) ? rgb(255, 232, 96) : rgb(255, 120, 32), 2.0f, false);

    // items
    if (k.roulette > 0) {
        f32 before = k.roulette;
        k.roulette -= dt;
        if (k.human) {
            if (k.roulette <= 0) sfx(r, SFX_ITEM_GET, &k);
            else if ((int)(before * 15.0f) != (int)(k.roulette * 15.0f)) sfx(r, SFX_ROULETTE, &k);
        }
    }
    if (c.item_pressed && k.item != IT_NONE && k.roulette <= 0 && k.spin <= 0) use_item(r, k, idx);
    if (k.shield > 0) k.shield -= dt;

    // progress and laps
    f32 prev_s = k.s;
    k.s = track_project(r.track, k.pos, &k.seg, 12);
    f32 L = r.track.length;
    {
        vec2 tan;
        vec2 on = track_point(r.track, k.s, &tan);
        // a long way from where we think we are (a jump, a cut): look along the whole line
        if (length_sq(on - k.pos) > 110.0f * 110.0f) k.s = track_project(r.track, k.pos, &k.seg, (int)r.track.line.len / 2);
        // wrong way: moving against the track for a while
        if (dot(k.vel, tan) < -25.0f && k.spin <= 0) k.wrong_way += dt; else k.wrong_way = 0;
    }
    if (prev_s > 0.75f * L && k.s < 0.25f * L) {
        if (k.passed_half) {
            k.lap++; k.passed_half = false;
            if (k.lap > 1) { // a lap done: time it
                k.last_lap = r.race_time - k.lap_start;
                if (k.lap >= 2 && k.lap - 2 < LAPS) k.lap_times[k.lap - 2] = k.last_lap;
                bool best = k.best_lap <= 0 || k.last_lap < k.best_lap;
                if (best) k.best_lap = k.last_lap;
                if (best && k.human && k.lap > 2 && k.lap <= LAPS) sfx(r, SFX_BEST_LAP, &k);
            }
            k.lap_start = r.race_time;
            if (k.lap > LAPS && !k.finished) {
                k.finished = true; k.finish_time = r.race_time; r.finish_count++;
                if (k.human) snd_final_lap(false);
            } else if (k.lap == LAPS && k.human) { r.final_lap_banner = 2.0f; sfx(r, SFX_FINAL_LAP, &k); snd_final_lap(true); }
            else if (k.lap > 1 && k.human) sfx(r, SFX_LAP, &k);
        }
    } else if (prev_s < 0.25f * L && k.s > 0.75f * L) {
        k.lap--; k.passed_half = true;
    }
    if (k.s > 0.4f * L && k.s < 0.6f * L) k.passed_half = true;
    k.total = (f32)k.lap * L + k.s;
}

// ---- set pieces ---------------------------------------------------------------------------
// Each theme's big moving thing. Paths come from the track (track_build): a line through the
// middle of the map that crosses the road, or a circle around the middle.

static vec2 sp_line_point(vec2 a, vec2 b, f32 t, vec2* u) {
    vec2 d = b - a;
    f32 l = length(d);
    *u = d / l;
    return a + *u * t;
}

static SetBody body(u8 img, f32 length, f32 radius, bool solid) {
    SetBody b{};
    b.img = img; b.length = length; b.radius = radius; b.solid = solid;
    return b;
}

void setpiece_init(Race& r) {
    SetPiece& sp = r.track.sp;
    sp.bodies.clear();
    r.nhands = 0; r.moon = 0; // only the graveyard has them
    sp.t = 0; sp.timer = 3.0f; sp.aux = 0; sp.dir = 1; sp.ncross = 0;
    switch (sp.kind) {
    case SPK_TRAIN: {
        sp.bodies.push(body(SPI_TRAIN_LOCO, 40, 7, true));
        for (int i = 0; i < 5; ++i) sp.bodies.push(body(SPI_TRAIN_CAR, 36, 7, true));
        sp.t = 400; // the head starts part way along
        // where the line meets the road: warning lights beside each crossing
        vec2 u;
        f32 L = length(sp.b - sp.a);
        bool was_road = false;
        for (f32 t = 0; t < L && sp.ncross < 8; t += 2.0f) {
            vec2 p = sp_line_point(sp.a, sp.b, t, &u);
            u8 su = track_surface(r.track, p.x, p.y);
            bool road = su == S_ROAD || su == S_BOOST;
            if (road && !was_road) sp.cross[sp.ncross++] = p - u * 6.0f + vec2{-u.y, u.x} * 10.0f;
            was_road = road;
        }
    } break;
    case SPK_SNOWBALL: sp.bodies.push(body(SPI_SNOWBALL, 0, 16, true)); sp.timer = 4.0f; break;
    case SPK_DINO: sp.bodies.push(body(SPI_DINO, 0, 4.5f, true)); sp.t = (r.track.def->sp_flags & SPF_SEGMENT) ? 20.0f : 300.0f; break;
    case SPK_BLIMP: {
        SetBody b = body(SPI_BLIMP, 0, 0, false);
        vec2 tan;
        b.pos = track_point(r.track, 200.0f, &tan);
        b.heading = kv::atan2(tan.y, tan.x);
        b.z = 70;
        sp.bodies.push(b);
        sp.dir = 3; sp.timer = 2.0f; // first drop soon after the start
    } break;
    case SPK_DRAGON: { SetBody b = body(SPI_DRAGON, 0, 0, false); b.z = 62; sp.bodies.push(b); sp.timer = 5.0f; } break;
    case SPK_SHIP: {
        // on each bearing from the center, sail well beyond the last land: open water
        for (int i = 0; i < 64; ++i) {
            f32 a = 6.2831853f * (f32)i / 64.0f;
            vec2 d{kv::cos(a), kv::sin(a)};
            f32 last_land = 0;
            for (f32 rr = 0; rr < 760.0f; rr += 4.0f) {
                vec2 p = sp.center + d * rr;
                u8 su = track_surface(r.track, p.x, p.y);
                bool in_map = p.x >= 0 && p.y >= 0 && p.x < MAP && p.y < MAP;
                if (in_map && su != S_WATER && su != S_OUT) last_land = rr;
            }
            sp.ring[i] = last_land + 95.0f;
        }
        for (int pass = 0; pass < 3; ++pass) { // smooth it, keeping the larger so it never cuts in
            f32 tmp[64];
            for (int i = 0; i < 64; ++i) tmp[i] = mxf(sp.ring[i], (sp.ring[(i + 63) % 64] + sp.ring[i] + sp.ring[(i + 1) % 64]) / 3.0f);
            for (int i = 0; i < 64; ++i) sp.ring[i] = tmp[i];
        }
        sp.bodies.push(body(SPI_SHIP, 0, 0, false));
        sp.timer = 3.0f;
    } break;
    case SPK_TRAFFIC: sp.timer = 0; break;
    case SPK_WAVE: sp.bodies.push(body(SPI_WAVE, 0, 0, false)); sp.dir = 0; sp.timer = 7.0f; break;
    case SPK_BUG: sp.bodies.push(body(SPI_MOTH, 0, 13, true)); sp.t = 20.0f; break;
    case SPK_COMET: sp.bodies.push(body(SPI_COMET, 0, 12, true)); sp.timer = 5.0f; sp.aux = 1.0f; break;
    case SPK_PENDULUM: {
        sp.timer = 2.0f;
        SetBody c = body(SPI_CARRIAGE, 40, 8, true); // the ghost carriage, half a lap ahead
        c.t = r.track.length * 0.5f;
        sp.bodies.push(c);
        // the plots where hands come up: half the road's width each, on alternate sides
        r.nhands = 0; r.moon = 0;
        for (int p = 0; p < 6; ++p) {
            f32 s0 = r.track.length * (0.09f + 0.155f * (f32)p);
            for (int row = 0; row < 2; ++row)
                for (int q = 0; q < 3; ++q) {
                    f32 s = s0 + 16.0f * (f32)row;
                    vec2 tan;
                    vec2 c0 = track_point(r.track, s, &tan);
                    f32 lat = (0.1f + 0.33f * (f32)q + 0.12f * (f32)row) * ((p & 1) ? -1.0f : 1.0f);
                    vec2 at = c0 + vec2{-tan.y, tan.x} * (track_half_width(r.track, s) * lat);
                    if (track_surface(r.track, at.x, at.y) != S_ROAD) continue;
                    if (r.nhands < 48) r.hands[r.nhands++] = at;
                }
        }
    } break;
    case SPK_TOASTER: {
        // an orange that rolls the table, and a toaster that pops waffles at the road
        sp.bodies.push(body(SPI_ORANGE, 0, 17, true));
        sp.t = 40; sp.timer = 5.0f; sp.aux = 0;
        sp.center = vec2{512, 300};
        for (int i = 0; i < r.track.def->nobjs; ++i)
            if (r.track.def->objs[i].img == SPI_TOASTER) sp.center = vec2{r.track.def->objs[i].x, r.track.def->objs[i].y};
    } break;
    default: break;
    }
}

// A body's collision circles along its heading.
static void sp_collide(Race& r, const SetBody& b) {
    if (!b.solid) return;
    vec2 f = dir(b.heading), rt{-f.y, f.x};
    vec2 pts[8];
    int n = 0;
    if (b.img == SPI_DINO) {
        // four legs; the body is overhead
        for (int fb = -1; fb <= 1; fb += 2)
            for (int lr = -1; lr <= 1; lr += 2) pts[n++] = b.pos + f * (13.0f * (f32)fb) + rt * (7.0f * (f32)lr);
    } else {
        int count = b.length > 0 ? (int)(b.length / (b.radius * 2.0f)) : 1;
        if (count < 1) count = 1;
        if (count > 8) count = 8;
        for (int i = 0; i < count; ++i) {
            f32 off = count == 1 ? 0 : -b.length * 0.5f + b.radius + (b.length - 2 * b.radius) * (f32)i / (f32)(count - 1);
            pts[n++] = b.pos + f * off;
        }
    }
    for (int i = 0; i < RACERS; ++i) {
        Racer& k = r.racers[i];
        if (k.fall > 0 || k.rescue > 0 || k.z > 8.0f) continue;
        for (int c = 0; c < n; ++c) {
            vec2 d = k.pos - pts[c];
            f32 want = b.radius + KART_R;
            f32 dist2 = length_sq(d);
            if (dist2 >= want * want) continue;
            f32 dist = kv::sqrt(dist2);
            vec2 nrm = dist > 0.01f ? d / dist : rt;
            vec2 np = pts[c] + nrm * want;
            if (!solid_at(r, np)) k.pos = np;
            k.vel = nrm * (r.track.theme->road == RS_SPACE ? 45.0f : 95.0f); // with no walls, a gentler shove
            hit(r, k);
            break;
        }
    }
}

static void sp_shake_near(Race& r, vec2 at, f32 radius, f32 amount) {
    if (r.player < 0) return;
    if (human_d2(r, at) < radius * radius) r.shake = mxf(r.shake, amount);
}

void setpiece_tick(Race& r, f32 dt) {
    SetPiece& sp = r.track.sp;
    vec2 u;
    switch (sp.kind) {
    case SPK_TRAIN: {
        // a loco and five cars run the line edge to edge, then come round again
        f32 L = length(sp.b - sp.a);
        sp.t += 150.0f * dt;
        if (sp.t - 5 * 40.0f > L + 120.0f) sp.t = -260.0f;
        for (u32 i = 0; i < sp.bodies.len; ++i) {
            SetBody& b = sp.bodies[i];
            b.t = sp.t - (f32)i * 40.0f;
            b.pos = sp_line_point(sp.a, sp.b, b.t, &u);
            b.heading = kv::atan2(u.y, u.x);
            b.solid = b.t > 0 && b.t < L;
            sp_collide(r, b);
        }
        sp_shake_near(r, sp.bodies[0].pos, 70.0f, 0.15f);
        // the whistle, once each time the loco comes within earshot
        if (r.player >= 0) {
            bool near = human_d2(r, sp.bodies[0].pos) < 330.0f * 330.0f;
            if (near && sp.aux == 0) snd_sfx(SFX_TRAIN_HORN);
            sp.aux = near ? 1.0f : 0.0f;
        }
    } break;
    case SPK_SNOWBALL: {
        // a giant snowball rolls its line, rests, and comes again: down the road at you on
        // Frost Ring (one way, weaving), back and forth across the track elsewhere
        f32 L = length(sp.b - sp.a);
        SetBody& b = sp.bodies[0];
        u8 flags = r.track.def->sp_flags;
        if (sp.timer > 0) { sp.timer -= dt; b.solid = false; b.pos = vec2{-9999, -9999}; break; }
        sp.t += 120.0f * dt;
        f32 t = sp.dir > 0 ? sp.t : L - sp.t;
        b.pos = sp_line_point(sp.a, sp.b, t, &u);
        if (flags & SPF_WEAVE) b.pos += vec2{-u.y, u.x} * (17.0f * kv::sin(sp.t * 0.021f + sp.aux));
        b.heading = kv::atan2(u.y, u.x) + (sp.dir > 0 ? 0.0f : 3.14159f);
        b.t += dt;
        b.solid = true;
        sp_collide(r, b);
        sp_shake_near(r, b.pos, 90.0f, 0.1f);
        if (r.player >= 0 && ((int)(sp.t / 60.0f) != (int)((sp.t - 120.0f * dt) / 60.0f)) && human_d2(r, b.pos) < 300.0f * 300.0f) snd_sfx(SFX_RUMBLE);
        if (r.rng.chance(0.5f)) spawn_part(r, b.pos, 2, vec2{r.rng.range(-30.0f, 30.0f), r.rng.range(-30.0f, 30.0f)}, r.rng.range(30.0f, 70.0f), 0.5f, rgb(248, 248, 255), 2, false);
        if (sp.t > L) {
            sp.t = 0; sp.timer = 3.0f; sp.aux = r.rng.range(0.0f, 6.28f);
            if (!(flags & SPF_ONE_WAY)) sp.dir = -sp.dir;
            for (int q = 0; q < 24; ++q) spawn_part(r, b.pos, 6, dir(r.rng.range(0.0f, 6.283f)) * r.rng.range(20.0f, 80.0f), r.rng.range(40.0f, 120.0f), 0.7f, rgb(248, 248, 255), 3, false);
        }
    } break;
    case SPK_DINO: {
        // the long-neck walks the line and back, slowly; its legs are what you hit
        f32 L = length(sp.b - sp.a);
        SetBody& b = sp.bodies[0];
        f32 margin = (r.track.def->sp_flags & SPF_SEGMENT) ? 10.0f : 300.0f;
        sp.t += 28.0f * dt * (f32)sp.dir;
        if (sp.t > L - margin) sp.dir = -1;
        if (sp.t < margin) sp.dir = 1;
        b.pos = sp_line_point(sp.a, sp.b, sp.t, &u);
        b.heading = kv::atan2(u.y, u.x) + (sp.dir > 0 ? 0.0f : 3.14159f);
        b.t += dt;
        sp_collide(r, b);
        sp.aux += dt;
        if (sp.aux > 0.8f) { // a footstep
            sp.aux = 0;
            sp_shake_near(r, b.pos, 140.0f, 0.12f);
            if (r.player >= 0 && human_d2(r, b.pos) < 200.0f * 200.0f) snd_sfx(SFX_STOMP);
        }
    } break;
    case SPK_TRAFFIC: {
        // hover cars and buses stream across two crossing roads, both directions
        sp.timer -= dt;
        if (sp.timer <= 0 && sp.bodies.len < 24) {
            sp.timer = r.rng.range(0.45f, 0.9f);
            bool bus = r.rng.chance(0.25f);
            SetBody b = body(bus ? SPI_HOVERBUS : SPI_HOVERCAR, bus ? 36.0f : 18.0f, bus ? 8.0f : 6.0f, true);
            b.lane = (i8)r.rng.below(4); // 0,1: first road both ways; 2,3: second road
            b.t = 0;
            b.z = 4;
            sp.bodies.push(b);
        }
        for (u32 i = 0; i < sp.bodies.len;) {
            SetBody& b = sp.bodies[i];
            bool second = b.lane >= 2, back = (b.lane & 1) != 0;
            vec2 a = second ? sp.a2 : sp.a, e = second ? sp.b2 : sp.b;
            f32 L = length(e - a);
            b.t += (b.img == SPI_HOVERBUS ? 125.0f : 170.0f) * dt;
            if (b.t > L) { sp.bodies[i] = sp.bodies[sp.bodies.len - 1]; --sp.bodies.len; continue; }
            vec2 p = sp_line_point(a, e, back ? L - b.t : b.t, &u);
            vec2 side{-u.y, u.x};
            b.pos = p + side * (back ? -7.0f : 7.0f);
            b.heading = kv::atan2(u.y, u.x) + (back ? 3.14159f : 0.0f);
            sp_collide(r, b);
            if (r.player >= 0) { // the whoosh as one goes by close
                f32 d2 = human_d2(r, b.pos);
                if (d2 < 45.0f * 45.0f && b.z < 5.0f) { snd_sfx(SFX_WHOOSH); b.z = 5.0f; }
            }
            ++i;
        }
    } break;
    case SPK_BLIMP: {
        // the blimp cruises in low over the road ahead of a racer (often you), hovers, and
        // drops a shower of item crystals, then heads for the next spot
        SetBody& b = sp.bodies[0];
        if (sp.dir == 1 && r.phase != RacePhase::Countdown) { // pick a spot
            int who = (r.player >= 0 && r.rng.chance(0.55f)) ? r.player : (int)r.rng.below(RACERS);
            vec2 tan;
            sp.a2 = track_point(r.track, r.racers[who].s + 300.0f, &tan);
            sp.aux = r.racers[who].s + 300.0f;
            sp.dir = 0;
        }
        vec2 to = sp.a2 - b.pos;
        f32 dist = length(to);
        b.z += (48.0f - b.z) * mnf(1.0f, dt);
        if (dist > 4.0f && sp.dir == 0) {
            b.pos += to * (mnf(dist, 130.0f * dt) / dist);
            b.heading += wrap_pi(kv::atan2(to.y, to.x) - b.heading) * mnf(1.0f, 2.0f * dt);
        }
        if (sp.dir == 0 && dist < 6.0f) { sp.dir = 2; sp.timer = 0.6f; } // arrived: hover a moment
        if (sp.dir == 2) {
            sp.timer -= dt;
            if (sp.timer <= 0) {
                vec2 tan;
                f32 hw = r.track.def->half_width;
                for (int k = 0; k < 5; ++k) {
                    vec2 c = track_point(r.track, sp.aux + r.rng.range(-30.0f, 30.0f), &tan);
                    vec2 nrm{-tan.y, tan.x};
                    r.track.boxes.push(ItemBox{c + nrm * (hw * r.rng.range(-0.7f, 0.7f)), 0, b.z - 6.0f, 20.0f, 16.0f, false});
                }
                for (int q = 0; q < 30; ++q) // confetti
                    spawn_part(r, b.pos, b.z - 8.0f, vec2{r.rng.range(-40.0f, 40.0f), r.rng.range(-40.0f, 40.0f)}, r.rng.range(-10.0f, 40.0f), 1.2f,
                               (q % 3 == 0) ? rgb(255, 232, 96) : ((q % 3 == 1) ? rgb(96, 200, 255) : rgb(255, 96, 160)), 2, false);
                sfx(r, SFX_DROP, nullptr, b.pos, 260.0f);
                sp.dir = 3; sp.timer = 5.0f; // wait, then the next spot
            }
        }
        if (sp.dir == 3) {
            b.heading += 0.25f * dt; // a lazy turn while it waits
            b.pos += dir(b.heading) * (40.0f * dt);
            sp.timer -= dt;
            if (sp.timer <= 0) sp.dir = 1;
        }
    } break;
    case SPK_DRAGON: {
        // the dragon circles, then swoops and lays a line of fire across the road ahead
        SetBody& b = sp.bodies[0];
        sp.t += 0.32f * dt;
        b.pos = sp.center + vec2{kv::cos(sp.t), kv::sin(sp.t)} * sp.radius;
        b.heading = sp.t + 1.5708f;
        b.t += dt;
        sp.timer -= dt;
        b.z = 62.0f - 26.0f * mxf(0.0f, 1.0f - kv::fabs(sp.timer)); // dips as it breathes
        if (sp.timer <= 0 && r.phase != RacePhase::Countdown) {
            sp.timer = r.rng.range(10.0f, 13.0f);
            int who = racer_at_place(r, (int)r.rng.below(3));
            if (who >= 0) {
                vec2 tan;
                vec2 c = track_point(r.track, r.racers[who].s + 120.0f, &tan);
                vec2 nrm{-tan.y, tan.x};
                f32 hw = r.track.def->half_width;
                // one half of the road, so there is always a way past; a second of
                // scorch marks before it catches, two seconds of flame
                f32 side = r.rng.chance(0.5f) ? 1.0f : -1.0f;
                for (int k = 0; k < 3; ++k) {
                    vec2 p = c + nrm * (hw * side * (0.18f + 0.28f * (f32)k));
                    r.oils.push(Hazard{p, FIRE_BURN, -1, HZ_FIRE, FIRE_WARN});
                }
                sfx(r, SFX_ROAR, nullptr, c, 320.0f);
                sp_shake_near(r, c, 200.0f, 0.2f);
            }
        }
    } break;
    case SPK_SHIP: {
        // the galleon sails the bay and lobs cannonballs at where karts are about to be
        SetBody& b = sp.bodies[0];
        sp.t += 0.1f * dt;
        f32 fi = kv::fmod(sp.t, 6.2831853f) / 6.2831853f * 64.0f;
        if (fi < 0) fi += 64.0f;
        int i0 = (int)fi % 64, i1 = (i0 + 1) % 64;
        f32 rad = sp.ring[i0] + (sp.ring[i1] - sp.ring[i0]) * (fi - kv::floor(fi));
        vec2 prev = b.pos;
        b.pos = sp.center + vec2{kv::cos(sp.t), kv::sin(sp.t)} * rad;
        vec2 mv = b.pos - prev;
        if (length_sq(mv) > 0.0001f) b.heading = kv::atan2(mv.y, mv.x);
        sp.timer -= dt;
        if (sp.timer <= 0 && r.phase != RacePhase::Countdown) {
            sp.timer = r.rng.range(1.8f, 3.2f);
            int who = (int)r.rng.below(RACERS);
            const Racer& k = r.racers[who];
            vec2 aim = k.pos + k.vel * 1.4f;
            f32 flight = 1.4f;
            vec2 from = b.pos;
            r.bombs.push(Projectile{from, (aim - from) / flight, 3.0f, 20.0f, (0.5f * 260.0f * flight * flight - 20.0f) / flight, -1, PK_CANNON, who, aim});
            for (int q = 0; q < 8; ++q) spawn_part(r, from, 16, vec2{r.rng.range(-20.0f, 20.0f), r.rng.range(-20.0f, 20.0f)}, 30.0f, 0.8f, rgb(216, 216, 224), 3, false);
            if (r.player >= 0 && (who == r.player || human_d2(r, from) < 420.0f * 420.0f)) snd_sfx(SFX_CANNON);
        }
    } break;
    case SPK_WAVE: {
        // the flush: every so often a wall of water runs the main channel. Whoever it
        // catches, it carries.
        SetBody& b = sp.bodies[0];
        const f32 s0 = track_node_s(r.track, r.track.def->sp[0]), s1 = track_node_s(r.track, r.track.def->sp[1]);
        if (sp.dir == 0) {
            b.pos = vec2{-9999, -9999};
            sp.timer -= dt;
            if (sp.timer <= 0 && r.phase != RacePhase::Countdown) {
                sp.dir = 1; sp.t = s0 - 60.0f;
                if (r.player >= 0) snd_sfx(SFX_RUMBLE);
            }
            break;
        }
        sp.t += 240.0f * dt;
        vec2 tan;
        b.pos = track_point(r.track, sp.t, &tan);
        b.heading = kv::atan2(tan.y, tan.x);
        b.t += dt;
        for (int i = 0; i < RACERS; ++i) {
            Racer& k = r.racers[i];
            if (k.airborne || k.fall > 0 || k.rescue > 0 || k.spin > 0) continue;
            f32 ds = k.s - sp.t;
            if (ds > -24.0f && ds < 10.0f && length_sq(k.pos - b.pos) < 60.0f * 60.0f) {
                if (k.surf <= 0 && k.human) snd_sfx(SFX_SPLASH);
                k.surf = 0.2f;
            }
        }
        sp_shake_near(r, b.pos, 80.0f, 0.12f);
        for (int q = 0; q < 3; ++q) {
            vec2 side{-tan.y, tan.x};
            spawn_part(r, b.pos + side * r.rng.range(-34.0f, 34.0f), r.rng.range(4.0f, 18.0f), tan * 120.0f + side * r.rng.range(-20.0f, 20.0f), r.rng.range(10.0f, 60.0f), 0.35f, (q & 1) ? rgb(232, 248, 240) : rgb(112, 184, 120), 2, false);
        }
        if (sp.t >= s1) { sp.dir = 0; sp.timer = r.rng.range(9.0f, 13.0f); }
    } break;
    case SPK_BUG: {
        // the bug in the system: a moth that wanders its line across the board and back
        f32 L = length(sp.b - sp.a);
        SetBody& b = sp.bodies[0];
        sp.t += 44.0f * dt * (f32)sp.dir;
        if (sp.t > L - 10.0f) sp.dir = -1;
        if (sp.t < 10.0f) sp.dir = 1;
        b.pos = sp_line_point(sp.a, sp.b, sp.t, &u);
        b.pos += vec2{-u.y, u.x} * (9.0f * kv::sin(sp.t * 0.05f)); // it does not walk straight
        b.heading = kv::atan2(u.y, u.x) + (sp.dir > 0 ? 0.0f : 3.14159f);
        b.t += dt;
        sp_collide(r, b);
    } break;
    case SPK_COMET: {
        // a comet down its line, again and again; and the asteroid belt drifting across the
        // road both ways
        SetBody& c = sp.bodies[0];
        f32 L = length(sp.b - sp.a);
        if (sp.timer > 0) { sp.timer -= dt; c.solid = false; c.pos = vec2{-9999, -9999}; }
        else {
            sp.t += 290.0f * dt;
            c.pos = sp_line_point(sp.a, sp.b, sp.t, &u);
            c.heading = kv::atan2(u.y, u.x);
            c.z = 6.0f;
            c.solid = true;
            sp_collide(r, c);
            for (int q = 0; q < 2; ++q) spawn_part(r, c.pos - u * r.rng.range(6.0f, 40.0f), 6, vec2{r.rng.range(-10.0f, 10.0f), r.rng.range(-10.0f, 10.0f)}, 0, 0.5f, (q & 1) ? rgb(200, 232, 255) : rgb(255, 255, 255), 2, false);
            if (r.player >= 0 && sp.t < 290.0f * dt * 1.5f && human_d2(r, c.pos) < 700.0f * 700.0f) snd_sfx(SFX_ROCKET);
            if (sp.t > L) { sp.t = 0; sp.timer = r.rng.range(8.0f, 12.0f); }
        }
        // the sun spits: small flames arc out of it and come down here and there on the road
        // near it, each with its shadow and ring a second and a half ahead
        sp.aux2 -= dt;
        if (sp.aux2 <= 0 && r.phase != RacePhase::Countdown) {
            sp.aux2 = r.rng.range(0.5f, 1.0f);
            vec2 sun{400, 440};
            for (int i = 0; i < r.track.def->nobjs; ++i)
                if (r.track.def->objs[i].img == SPI_SUN) sun = vec2{r.track.def->objs[i].x, r.track.def->objs[i].y};
            f32 s = r.rng.range(track_node_s(r.track, 14.6f), track_node_s(r.track, 19.2f));
            vec2 tan;
            vec2 center = track_point(r.track, s, &tan);
            vec2 aim = center + vec2{-tan.y, tan.x} * (track_half_width(r.track, s) * r.rng.range(-0.8f, 0.8f));
            const f32 flight = 1.6f, z0 = 60.0f;
            r.bombs.push(Projectile{sun, (aim - sun) / flight, 4.0f, z0, (0.5f * 260.0f * flight * flight - z0) / flight, -1, PK_FLARE, -1, aim});
        }
        vec2 a2 = sp.a2, b2 = sp.b2;
        f32 L2 = length(b2 - a2);
        sp.aux -= dt;
        if (sp.aux <= 0 && sp.bodies.len < 14 && L2 > 1.0f) {
            sp.aux = r.rng.range(2.2f, 3.6f);
            SetBody rock = body(SPI_ASTEROID, 0, 11, true);
            rock.lane = (i8)r.rng.below(4);
            rock.radius = 11.0f;
            rock.scale = r.rng.range(0.7f, 1.2f);
            rock.t = 0;
            sp.bodies.push(rock);
        }
        for (u32 i = 1; i < sp.bodies.len;) {
            SetBody& b = sp.bodies[i];
            bool back = (b.lane & 1) != 0;
            b.t += (60.0f + 12.0f * (f32)b.lane) * dt;
            if (b.t > L2) { sp.bodies[i] = sp.bodies[sp.bodies.len - 1]; --sp.bodies.len; continue; }
            vec2 p = sp_line_point(a2, b2, back ? L2 - b.t : b.t, &u);
            b.pos = p + vec2{-u.y, u.x} * ((b.lane >= 2 ? 22.0f : 8.0f) * (back ? -1.0f : 1.0f));
            b.heading = kv::atan2(u.y, u.x) + (back ? 3.14159f : 0.0f);
            b.z = 5.0f;
            sp_collide(r, b);
            ++i;
        }
    } break;
    case SPK_PENDULUM: {
        // the pendulum swings across the hall; in the graveyard, ghosts drift over the road
        // and take whatever you are carrying
        { // the carriage keeps its own slow lap, drifting from one side of the road to the other
            SetBody& c = sp.bodies[0];
            c.t = kv::fmod(c.t + 88.0f * dt, r.track.length);
            vec2 tan;
            vec2 p = track_point(r.track, c.t, &tan);
            c.pos = p + vec2{-tan.y, tan.x} * (track_half_width(r.track, c.t) * 0.5f * kv::sin(c.t * 0.011f));
            c.heading = kv::atan2(tan.y, tan.x);
            sp_collide(r, c);
        }
        { // the moon: hidden, then out for a while. The light coming up is the warning.
            f32 ph = kv::fmod(r.clock, 22.0f), was = r.moon;
            r.moon = ph < 11.0f ? 0.0f : (ph < 13.0f ? (ph - 11.0f) * 0.5f : (ph < 19.0f ? 1.0f : (ph < 21.0f ? 1.0f - (ph - 19.0f) * 0.5f : 0.0f)));
            if (was <= 0.0f && r.moon > 0.0f && r.player >= 0) snd_sfx(SFX_BELL); // the chapel bell tolls as the moon comes out
            if (r.moon >= 1.0f)
                for (int j = 0; j < RACERS; ++j) {
                    Racer& k = r.racers[j];
                    if (k.fall > 0 || k.airborne) continue;
                    for (int h = 0; h < r.nhands; ++h) {
                        if (length_sq(k.pos - r.hands[h]) > 8.0f * 8.0f) continue;
                        if (length_sq(k.vel) > 120.0f * 120.0f) sfx(r, SFX_BUMP, &k); // caught
                        k.vel = k.vel * clampf(1.0f - 7.0f * dt, 0.0f, 1.0f); // held, not thrown: it lets go as you crawl clear
                        if (r.rng.chance(0.3f)) spawn_part(r, r.hands[h], 2, vec2{r.rng.range(-12.0f, 12.0f), r.rng.range(-12.0f, 12.0f)}, 40.0f, 0.4f, rgb(72, 56, 40), 2, false);
                        break;
                    }
                }
        }
        vec2 a2 = sp.a2, b2 = sp.b2;
        f32 L2 = length(b2 - a2);
        sp.timer -= dt;
        if (sp.timer <= 0 && sp.bodies.len < 7 && L2 > 1.0f && r.moon <= 0.0f) { // ghosts walk while the moon is hidden
            sp.timer = r.rng.range(2.5f, 4.0f);
            SetBody g = body(SPI_GHOST, 0, 0, false);
            g.lane = (i8)r.rng.below(2);
            g.radius = r.rng.range(-30.0f, 30.0f); // its own lateral drift
            sp.bodies.push(g);
        }
        for (u32 i = 1; i < sp.bodies.len;) {
            SetBody& g = sp.bodies[i];
            g.t += 42.0f * dt;
            if (g.t > L2) { sp.bodies[i] = sp.bodies[sp.bodies.len - 1]; --sp.bodies.len; continue; }
            vec2 p = sp_line_point(a2, b2, g.lane ? L2 - g.t : g.t, &u);
            g.pos = p + vec2{-u.y, u.x} * (g.radius + 12.0f * kv::sin(g.t * 0.04f));
            g.z = 5.0f + 3.0f * kv::sin(g.t * 0.1f);
            if (g.length > 0) g.length -= dt; // a moment before it can take again
            for (int j = 0; j < RACERS && g.length <= 0; ++j) {
                Racer& k = r.racers[j];
                if (k.fall > 0 || length_sq(k.pos - g.pos) > 11.0f * 11.0f) continue;
                if (k.item != IT_NONE && k.roulette <= 0 && k.shield <= 0) {
                    k.item = IT_NONE; k.item_uses = 0;
                    g.length = 1.5f;
                    sfx(r, SFX_MAGNET, &k);
                    for (int q = 0; q < 10; ++q) spawn_part(r, k.pos, 6, dir(r.rng.range(0.0f, 6.283f)) * 30.0f, 40.0f, 0.5f, rgb(200, 224, 255), 2, false);
                }
            }
            ++i;
        }
    } break;
    case SPK_TOASTER: {
        f32 L = length(sp.b - sp.a);
        SetBody& b = sp.bodies[0];
        sp.t += 92.0f * dt * (f32)sp.dir;
        if (sp.t > L - 20.0f) sp.dir = -1;
        if (sp.t < 20.0f) sp.dir = 1;
        b.pos = sp_line_point(sp.a, sp.b, sp.t, &u);
        b.heading = kv::atan2(u.y, u.x) + (sp.dir > 0 ? 0.0f : 3.14159f);
        b.t += dt * (f32)sp.dir;
        sp_collide(r, b);
        sp_shake_near(r, b.pos, 80.0f, 0.08f);
        if (r.player >= 0 && ((int)(sp.t / 70.0f) != (int)((sp.t - 92.0f * dt * (f32)sp.dir) / 70.0f)) && human_d2(r, b.pos) < 260.0f * 260.0f) snd_sfx(SFX_RUMBLE);
        // the toaster: up come the waffles, out to where somebody is about to be
        if (sp.aux > 0) sp.aux -= dt;
        sp.timer -= dt;
        if (sp.timer <= 0 && r.phase != RacePhase::Countdown) {
            sp.timer = r.rng.range(2.4f, 3.8f);
            int who = (int)r.rng.below(RACERS);
            const Racer& k = r.racers[who];
            const f32 flight = 1.6f;
            vec2 aim = k.pos + k.vel * flight;
            vec2 from = sp.center;
            r.bombs.push(Projectile{from, (aim - from) / flight, 4.0f, 40.0f, (0.5f * 260.0f * flight * flight - 40.0f) / flight, -1, PK_WAFFLE, who, aim});
            sp.aux = 0.5f;
            if (r.player >= 0 && (who == r.player || human_d2(r, from) < 380.0f * 380.0f)) snd_sfx(SFX_CANNON);
        }
    } break;
    default: break;
    }
}

// ---- race ---------------------------------------------------------------------------------
void race_init(Race& r, const TrackDef& def, int player_char, bool attract, Allocator* alloc, int players, const int* chars) {
    // the humans' characters, no two the same
    int hc[4] = {player_char, -1, -1, -1};
    int nh = attract ? 0 : 1;
    if (!attract && chars && players > 1) {
        nh = players > 4 ? 4 : players;
        for (int i = 1; i < nh; ++i) {
            int c = ((chars[i] % RACERS) + RACERS) % RACERS;
            for (int tries = 0; tries < RACERS; ++tries) {
                bool taken = false;
                for (int j = 0; j < i; ++j) if (hc[j] == c) taken = true;
                if (!taken) break;
                c = (c + 1) % RACERS;
            }
            hc[i] = c;
        }
    }
    auto is_human_char = [&](int c) { for (int i = 0; i < nh; ++i) if (hc[i] == c) return true; return false; };
    race_free(r);
    r.bombs.alloc = r.oils.alloc = nullptr;
    r.bombs.alloc = alloc; r.oils.alloc = alloc; r.parts.alloc = alloc;
    track_build(r.track, def, alloc);
    r.rng = Rng::seeded(0x5150 + (u64)def.nnodes * 31 + (u64)player_char + (u64)def.seed * 13 + (u64)def.theme * 7);
    r.phase = RacePhase::Countdown;
    r.time_trial = false; r.rec = nullptr; r.rec_n = r.rec_cap = 0; r.ghost = nullptr; r.ghost_n = 0; r.ghost_time = 0;
    r.clock = 0; r.race_time = 0; r.done_timer = 0; r.final_lap_banner = 0; r.finish_count = 0; r.emp_flash = 0; r.shake = 0;
    r.player = attract ? -1 : 0;
    // grid: the player starts at the back, CPUs in front in a staggered two-column grid
    int order[RACERS];
    int n = 0;
    for (int c = 0; c < RACERS; ++c) if (attract || !is_human_char(c)) order[n++] = c;
    for (int i = nh - 1; i >= 0; --i) order[n++] = hc[i]; // the humans at the back, player one last
    for (int i = 0; i < RACERS; ++i) {
        Racer& k = r.racers[i];
        k = Racer{};
        k.ch = order[i];
        k.human = !attract && i >= RACERS - nh;
        f32 s = r.track.length - 22.0f - (f32)i * 15.0f;
        vec2 tan;
        vec2 p = track_point(r.track, s, &tan);
        vec2 nrm{-tan.y, tan.x};
        k.pos = p + nrm * ((i & 1) ? 11.0f : -11.0f);
        k.heading = kv::atan2(tan.y, tan.x);
        k.move_angle = k.heading;
        k.seg = 0;
        k.s = track_project(r.track, k.pos, &k.seg, (int)r.track.line.len / 2);
        k.lap = 0; k.passed_half = true;
        k.total = k.s - r.track.length;
        k.skill = 0.93f + 0.012f * (f32)i;
        k.lane = r.rng.range(-0.7f, 0.7f);
        k.last_safe_s = k.s;
    }
    setpiece_init(r);
    for (int g = 0; g < 8; ++g) r.gate_t[g] = 1.3f * (f32)g;
    snd_final_lap(false);
    if (!attract) {
        // the player is the last entry; keep the convention that r.player indexes it
        r.player = RACERS - 1;
    }
    r.humans = nh > 0 ? nh : 1;
    for (int i = 0; i < 4; ++i) r.human_idx[i] = i < nh ? RACERS - 1 - i : -1;
    r.player2 = nh > 1 ? RACERS - 2 : -1;
    race_camera(r, 0, true);
}

void race_time_trial(Race& r, GhostSample* rec, int rec_cap, const GhostSample* ghost, int ghost_n, int ghost_ch) {
    r.time_trial = true;
    r.rec = rec; r.rec_cap = rec ? rec_cap : 0; r.rec_n = 0;
    r.ghost = ghost_n > 0 ? ghost : nullptr; r.ghost_n = ghost ? ghost_n : 0; r.ghost_ch = ghost_ch;
    for (int i = 0; i < RACERS; ++i) {
        Racer& k = r.racers[i];
        if (k.human) { k.item = IT_NITRO3; k.item_uses = 3; continue; }
        k.absent = true;
        k.pos = vec2{-9999.0f, -9999.0f - 40.0f * (f32)i}; // nowhere, and not on top of each other
        k.vel = vec2{};
    }
    for (u32 i = 0; i < r.track.boxes.len; ++i) r.track.boxes[i].respawn = 1e9f; // no items but the three nitros
    // the player takes the front of the grid
    if (r.player >= 0) {
        Racer& k = r.racers[r.player];
        vec2 tan;
        vec2 p = track_point(r.track, r.track.length - 22.0f, &tan);
        k.pos = p;
        k.heading = k.move_angle = kv::atan2(tan.y, tan.x);
        k.s = track_project(r.track, k.pos, &k.seg, (int)r.track.line.len / 2);
        k.total = k.s - r.track.length;
        k.last_safe_s = k.s;
    }
    race_camera(r, 0, true);
}

void race_free(Race& r) {
    track_free(r.track);
    r.bombs.free(); r.oils.free(); r.parts.free();
}

static void camera_follow(const Racer& k, vec2& cam, f32& ang, f32& pull, f32 dt, bool snap) {
    f32 target = k.move_angle;
    if (k.spin > 0) target = ang; // hold the view while spinning
    if (snap) ang = target;
    else ang += wrap_pi(target - ang) * mnf(1.0f, (k.drifting ? 4.5f : 7.0f) * dt);
    ang = wrap_pi(ang);
    // under boost the camera falls back a little and the kart pulls away from it
    f32 want_pull = k.boost > 0 ? 9.0f : 0.0f;
    pull = snap ? 0.0f : pull + (want_pull - pull) * mnf(1.0f, 5.0f * dt);
    cam = k.pos - dir(ang) * (CAM_DIST + pull);
}

void race_camera(Race& r, f32 dt, bool snap) {
    int follow = r.player >= 0 ? r.player : 0;
    if (r.player < 0) {
        // attract: follow the leader
        for (int i = 0; i < RACERS; ++i) if (r.racers[i].place == 0) follow = i;
    }
    camera_follow(r.racers[follow], r.cam, r.cam_angle, r.cam_pull, dt, snap);
    r.cams[0] = r.cam; r.cam_angles[0] = r.cam_angle; r.cam_pulls[0] = r.cam_pull;
    for (int hu = 1; hu < r.humans; ++hu) camera_follow(r.racers[r.human_idx[hu]], r.cams[hu], r.cam_angles[hu], r.cam_pulls[hu], dt, snap);
}

void race_tick(Race& r, const Controls& human, f32 dt, const Controls* more) {
    f32 before = r.clock;
    r.clock += dt;
    if (r.phase == RacePhase::Countdown) {
        if ((int)before != (int)r.clock || before == 0) sfx(r, r.clock >= 3.0f ? SFX_GO : SFX_COUNT_BEEP, nullptr, r.racers[r.player >= 0 ? r.player : 0].pos);
        if (r.clock >= 3.0f) r.phase = RacePhase::Racing;
    }
    if (r.phase != RacePhase::Countdown) r.race_time += dt;
    if (r.final_lap_banner > 0) r.final_lap_banner -= dt;

    for (int i = 0; i < RACERS; ++i) {
        Racer& k = r.racers[i];
        if (k.human && !r.autopilot) {
            Controls c = human;
            for (int hu = 1; hu < r.humans; ++hu) if (i == r.human_idx[hu] && more) c = more[hu - 1];
            c.hop_pressed = c.hop_pressed || (c.hop && !k.prev_hop);
            k.prev_hop = c.hop;
            k.in = c;
        } else if (!k.absent) ai_controls(r, k, i, dt);
    }
    // the grid: hold the throttle from just after the second beep for a flying start; sit on
    // it the whole countdown and the engine floods
    bool go = before < 3.0f && r.clock >= 3.0f;
    for (int i = 0; i < RACERS; ++i) {
        Racer& k = r.racers[i];
        if (r.phase == RacePhase::Countdown) {
            // (easy rivals are late on the throttle and miss the flying start)
            bool on = k.human && !r.autopilot ? k.in.accel : (r.clock > 3.0f - (r.cpu_level < 0.5f ? 0.12f * r.rng.unit() : 0.35f + 0.5f * k.skill * r.rng.unit()));
            k.rev = on ? k.rev + dt : 0;
        }
        if (go) {
            if (k.rev >= 0.2f && k.rev <= 1.05f) { k.boost = 0.9f; if (k.human) snd_sfx(SFX_START_BOOST); }
            else if (k.rev > 2.2f) { k.stun = 1.0f; if (k.human) snd_sfx(SFX_STALL); }
        }
    }
    for (int i = 0; i < RACERS; ++i) if (!r.racers[i].absent) kart_tick(r, r.racers[i], i, dt);
    // a time trial's run, a sample a tick from the green light to the flag
    if (r.rec && r.player >= 0 && r.phase == RacePhase::Racing && !r.racers[r.player].finished && r.rec_n < r.rec_cap) {
        const Racer& k = r.racers[r.player];
        r.rec[r.rec_n++] = GhostSample{k.pos.x, k.pos.y, k.heading, k.total, k.drifting ? 1u : 0u};
    }

    // drafting: tucked in close behind another kart, at speed
    for (int i = 0; i < RACERS; ++i) {
        Racer& k = r.racers[i];
        bool tucked = false;
        if (length_sq(k.vel) > 110.0f * 110.0f && !k.airborne && k.fall <= 0) {
            vec2 f = dir(k.move_angle), rt{-f.y, f.x};
            for (int j = 0; j < RACERS && !tucked; ++j) {
                if (j == i) continue;
                vec2 d = r.racers[j].pos - k.pos;
                f32 fw = dot(d, f), lat = dot(d, rt);
                if (fw > 10.0f && fw < 62.0f && kv::fabs(lat) < 7.0f && dot(r.racers[j].vel, f) > 80.0f) tucked = true;
            }
        }
        f32 was = k.draft;
        k.draft = tucked ? k.draft + dt : mxf(0.0f, k.draft - dt * 2.0f);
        if (k.human && was <= 1.0f && k.draft > 1.0f) snd_sfx(SFX_DRAFT);
        if (k.draft > 1.0f && r.rng.chance(0.5f)) {
            vec2 f = dir(k.move_angle), rt{-f.y, f.x};
            spawn_part(r, k.pos + f * 10.0f + rt * r.rng.range(-6.0f, 6.0f), r.rng.range(2.0f, 8.0f), -f * 60.0f, 0, 0.18f, rgb(232, 240, 255), 1, false);
        }
    }

    // kart vs kart
    for (int i = 0; i < RACERS; ++i)
        for (int j = i + 1; j < RACERS; ++j) {
            Racer& a = r.racers[i];
            Racer& b = r.racers[j];
            if (a.fall > 0 || b.fall > 0 || a.z > 6.0f || b.z > 6.0f) continue;
            vec2 d = b.pos - a.pos;
            f32 dist = length(d);
            if (dist >= KART_R * 2 || dist < 0.001f) continue;
            vec2 n = d / dist;
            f32 push = (KART_R * 2 - dist) * 0.5f;
            if (!solid_at(r, a.pos - n * push)) a.pos -= n * push;
            if (!solid_at(r, b.pos + n * push)) b.pos += n * push;
            f32 rel = dot(b.vel - a.vel, n);
            f32 wa = k_chars[a.ch].weight, wb = k_chars[b.ch].weight;
            if (rel < 0) { a.vel += n * (rel * 1.6f * wb / (wa + wb)); b.vel -= n * (rel * 1.6f * wa / (wa + wb)); }
            if (rel < -25.0f && (a.human || b.human)) snd_sfx(SFX_BUMP);
        }

    // item crystals (some dropped from above, some fake) and bolts
    for (u32 bi = 0; bi < r.track.boxes.len;) {
        ItemBox& box = r.track.boxes[bi];
        if (box.life > 0) {
            box.life -= dt;
            if (box.life <= 0) { r.track.boxes[bi] = r.track.boxes[r.track.boxes.len - 1]; --r.track.boxes.len; continue; }
        }
        if (box.z > 0) {
            box.vz -= 120.0f * dt; box.z += box.vz * dt;
            if (r.rng.chance(0.5f)) spawn_part(r, box.pos, box.z + 4.0f, vec2{}, 10.0f, 0.4f, (r.rng.next() & 1) ? rgb(255, 255, 255) : rgb(120, 220, 255), 1, false);
            if (box.z < 0) box.z = 0;
            ++bi; continue;
        }
        if (box.respawn > 0) { box.respawn -= dt; ++bi; continue; }
        bool taken = false;
        for (int i = 0; i < RACERS && !taken; ++i) {
            Racer& k = r.racers[i];
            if (k.fall > 0 || length_sq(k.pos - box.pos) > 64.0f) continue;
            taken = true;
            if (box.fake) { hit(r, k); sfx(r, SFX_EXPLODE, &k); break; }
            box.respawn = 3.0f;
            if (k.human) sfx(r, SFX_ITEM_BOX, &k);
            for (int p = 0; p < 10; ++p)
                spawn_part(r, box.pos, 6, dir(r.rng.range(0.0f, 6.283f)) * r.rng.range(10.0f, 50.0f), r.rng.range(30.0f, 90.0f), 0.5f, p & 1 ? rgb(120, 220, 255) : rgb(255, 255, 255), 2, false);
            if (k.item == IT_NONE && !r.time_trial) { // (a time trial has its three nitros and nothing else, whatever a set piece drops)
                k.item = roll_item(r, k);
                k.item_uses = k.item == IT_NITRO3 ? 3 : 1;
                k.roulette = k.human ? 1.1f : 0.5f;
                k.item_wait = r.rng.range(0.5f, 3.5f) * (1.25f - k_chars[k.ch].aggression);
            }
        }
        if (taken && (box.fake || box.life > 0)) { r.track.boxes[bi] = r.track.boxes[r.track.boxes.len - 1]; --r.track.boxes.len; continue; }
        ++bi;
    }
    for (u32 bi2 = 0; bi2 < r.track.bolts.len;) {
        Bolt& b = r.track.bolts[bi2];
        bool gone = false;
        if (b.life > 0) { // a spilled one: it bounces, rolls to a stop, and does not last
            b.pos += b.vel * dt;
            b.vel = b.vel * clampf(1.0f - 2.5f * dt, 0.0f, 1.0f);
            b.z += b.vz * dt; b.vz -= 260.0f * dt;
            if (b.z < 0) { b.z = 0; b.vz = b.vz < -40.0f ? -b.vz * 0.45f : 0.0f; }
            b.life -= dt;
            if (b.life <= 0) gone = true;
        }
        if (b.respawn > 0) b.respawn -= dt;
        else if (!gone && !(b.life > 5.4f)) // (a spilled one cannot be taken straight back)
            for (int i = 0; i < RACERS; ++i) {
                Racer& k = r.racers[i];
                if (k.spin > 0 || length_sq(k.pos - b.pos) > 36.0f) continue;
                if (b.life > 0) gone = true; else b.respawn = 9.0f;
                if (k.human) sfx(r, SFX_BOLT, &k);
                if (k.bolts < 10) k.bolts++;
                spawn_part(r, b.pos, 4, vec2{}, 50.0f, 0.3f, rgb(255, 224, 96), 2, false);
                break;
            }
        if (gone) { r.track.bolts[bi2] = r.track.bolts[r.track.bolts.len - 1]; --r.track.bolts.len; continue; }
        ++bi2;
    }

    // projectiles: bombs bounce off walls, rockets home in, cannonballs arc in from the ship
    for (u32 i = 0; i < r.bombs.len;) {
        Projectile& b = r.bombs[i];
        b.life -= dt;
        bool boom = b.life <= 0;
        f32 blast = 196.0f;
        if (b.kind == PK_BOMB) {
            vec2 np = b.pos + b.vel * dt;
            if (solid_at(r, np)) {
                if (!solid_at(r, vec2{np.x, b.pos.y})) b.vel.y = -b.vel.y;
                else if (!solid_at(r, vec2{b.pos.x, np.y})) b.vel.x = -b.vel.x;
                else b.vel = -b.vel;
            } else b.pos = np;
            b.z = 6.0f + 4.0f * kv::fabs(kv::sin(b.life * 9.0f));
        } else if (b.kind == PK_ROCKET) {
            if (b.target >= 0) {
                vec2 to = r.racers[b.target].pos - b.pos;
                f32 want = kv::atan2(to.y, to.x), cur = kv::atan2(b.vel.y, b.vel.x);
                cur += clampf(wrap_pi(want - cur), -3.5f * dt, 3.5f * dt);
                b.vel = dir(cur) * 235.0f;
            }
            b.pos += b.vel * dt;
            if (r.rng.chance(0.8f)) spawn_part(r, b.pos, b.z, -b.vel * 0.1f, 0, 0.25f, r.rng.chance(0.5f) ? rgb(255, 240, 160) : rgb(160, 160, 176), 2, false);
        } else { // lobbed: a cannonball, or a waffle from the toaster
            b.pos += b.vel * dt;
            b.z += b.vz * dt; b.vz -= 260.0f * dt;
            blast = 210.0f;
            if (b.z <= 0) {
                boom = true;
                u8 su = track_surface(r.track, b.pos.x, b.pos.y);
                if (su == S_WATER || su == S_OUT || su == S_PIT) { // a splash, nothing more
                    for (int p = 0; p < 16; ++p) spawn_part(r, b.pos, 0, dir(r.rng.range(0.0f, 6.283f)) * r.rng.range(10.0f, 40.0f), r.rng.range(60.0f, 140.0f), 0.6f, rgb(200, 232, 255), 2, false);
                    r.bombs[i] = r.bombs[r.bombs.len - 1]; --r.bombs.len;
                    continue;
                }
            }
        }
        for (int j = 0; j < RACERS && !boom && b.kind != PK_CANNON && b.kind != PK_WAFFLE && b.kind != PK_FLARE; ++j) {
            if (j == b.owner && b.life > (b.kind == PK_ROCKET ? 5.6f : 3.6f)) continue;
            if (r.racers[j].fall > 0) continue;
            if (length_sq(r.racers[j].pos - b.pos) < 49.0f) boom = true;
        }
        if (boom && b.kind == PK_FLARE) { // a spit of solar flame: small, and gone as it lands
            sfx(r, SFX_FIRE, nullptr, b.pos, 140.0f);
            for (int j = 0; j < RACERS; ++j)
                if (r.racers[j].fall <= 0 && !r.racers[j].airborne && length_sq(r.racers[j].pos - b.pos) < 90.0f) hit(r, r.racers[j]);
            for (int p = 0; p < 10; ++p)
                spawn_part(r, b.pos, 2, dir(r.rng.range(0.0f, 6.283f)) * r.rng.range(10.0f, 40.0f), r.rng.range(20.0f, 80.0f), 0.4f, (p & 1) ? rgb(255, 232, 120) : rgb(255, 136, 40), 2, false);
            r.bombs[i] = r.bombs[r.bombs.len - 1]; --r.bombs.len;
            continue;
        }
        if (boom && b.kind == PK_WAFFLE) { // a waffle lands flat: a thump and a shower of crumbs
            sfx(r, SFX_LAND, nullptr, b.pos, 160.0f);
            for (int j = 0; j < RACERS; ++j)
                if (r.racers[j].fall <= 0 && length_sq(r.racers[j].pos - b.pos) < 170.0f) hit(r, r.racers[j]);
            for (int p = 0; p < 22; ++p)
                spawn_part(r, b.pos, 2, dir(r.rng.range(0.0f, 6.283f)) * r.rng.range(15.0f, 70.0f), r.rng.range(30.0f, 110.0f), 0.6f, (p & 1) ? rgb(232, 176, 80) : rgb(184, 120, 40), 2, false);
            r.bombs[i] = r.bombs[r.bombs.len - 1]; --r.bombs.len;
            continue;
        }
        if (boom) {
            sfx(r, SFX_EXPLODE, nullptr, b.pos);
            for (int j = 0; j < RACERS; ++j)
                if (r.racers[j].fall <= 0 && length_sq(r.racers[j].pos - b.pos) < blast) hit(r, r.racers[j]);
            for (int p = 0; p < 30; ++p)
                spawn_part(r, b.pos, 4, dir(r.rng.range(0.0f, 6.283f)) * r.rng.range(20.0f, 90.0f), r.rng.range(20.0f, 140.0f), 0.6f,
                           p % 3 == 0 ? rgb(255, 255, 160) : (p % 3 == 1 ? rgb(255, 152, 32) : rgb(208, 48, 32)), 3, false);
            r.bombs[i] = r.bombs[r.bombs.len - 1]; --r.bombs.len;
            continue;
        }
        ++i;
    }
    // oil slicks
    for (u32 i = 0; i < r.oils.len;) {
        Hazard& h = r.oils[i];
        if (h.warn > 0) { // not alight yet
            h.warn -= dt;
            if (h.warn <= 0) sfx(r, r.track.theme->fire_look == 1 ? SFX_ZAP : SFX_FIRE, nullptr, h.pos, 200.0f);
            ++i;
            continue;
        }
        h.life -= dt;
        bool gone = h.life <= 0;
        for (int j = 0; j < RACERS && !gone; ++j) {
            Racer& k = r.racers[j];
            if (k.airborne) continue;
            if (k.fall > 0) continue;
            if (length_sq(k.pos - h.pos) < (h.kind == HZ_FIRE ? 50.0f : 40.0f)) { sfx(r, h.kind == HZ_FIRE ? SFX_HIT : SFX_OIL, &k); hit(r, k); gone = h.kind == HZ_OIL; }
        }
        if (h.kind == HZ_FIRE && r.rng.chance(0.3f))
            spawn_part(r, h.pos + vec2{r.rng.range(-4.0f, 4.0f), r.rng.range(-4.0f, 4.0f)}, 2, vec2{}, r.rng.range(20.0f, 50.0f), 0.35f, r.rng.chance(0.5f) ? rgb(255, 200, 48) : rgb(255, 96, 32), 2, false);
        if (gone) { r.oils[i] = r.oils[r.oils.len - 1]; --r.oils.len; continue; }
        ++i;
    }
    setpiece_tick(r, dt);
    if (r.phase != RacePhase::Countdown)
        for (int g = 0; g < r.track.ngates; ++g) {
            r.gate_t[g] += dt;
            if (r.gate_t[g] < r.track.gate_period[g]) continue;
            r.gate_t[g] = 0;
            vec2 tan;
            vec2 c = track_point(r.track, r.track.gate_s[g], &tan);
            vec2 nrm{-tan.y, tan.x};
            f32 hw = track_half_width(r.track, r.track.gate_s[g]);
            // one half of the road at a time, so there is always a way through
            int q0 = r.rng.chance(0.5f) ? 0 : 3;
            for (int q = q0; q < q0 + 3; ++q) r.oils.push(Hazard{c + nrm * (hw * (-0.85f + 0.34f * (f32)q)), 0.9f, -1, HZ_FIRE, 1.0f});
        }
    if (r.emp_flash > 0) r.emp_flash -= dt;
    if (r.shake > 0) r.shake -= dt;

    // particles
    for (u32 i = 0; i < r.parts.len;) {
        Particle& p = r.parts[i];
        p.life -= dt;
        if (p.life <= 0) { r.parts[i] = r.parts[r.parts.len - 1]; --r.parts.len; continue; }
        p.pos += p.vel * dt;
        p.z += p.vz * dt;
        p.vz -= 260.0f * dt;
        if (p.z < 0) { p.z = 0; p.vz = 0; p.vel *= 0.8f; }
        ++i;
    }

    // places: finished racers by finish order, then by distance covered
    int idx[RACERS];
    for (int i = 0; i < RACERS; ++i) idx[i] = i;
    for (int a = 1; a < RACERS; ++a)
        for (int b = a; b > 0; --b) {
            const Racer& x = r.racers[idx[b]];
            const Racer& y = r.racers[idx[b - 1]];
            if (x.absent != y.absent) { if (x.absent) break; int t2 = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = t2; continue; }
            bool sorts_before = (x.finished && !y.finished) || (x.finished && y.finished && x.finish_time < y.finish_time) ||
                                (!x.finished && !y.finished && x.total > y.total);
            if (!sorts_before) break;
            int t = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = t;
        }
    for (int p = 0; p < RACERS; ++p) r.racers[idx[p]].place = p;

    if (r.phase == RacePhase::Racing) {
        bool player_done = r.player >= 0;
        for (int hu = 0; hu < r.humans && player_done; ++hu) if (!r.racers[r.human_idx[hu] >= 0 ? r.human_idx[hu] : r.player].finished) player_done = false;
        if (player_done || r.finish_count == RACERS) r.phase = RacePhase::Done;
        if (r.player < 0 && r.racers[0].lap > LAPS + 1) r.phase = RacePhase::Done;
    }
    if (r.phase == RacePhase::Done) r.done_timer += dt;
    race_camera(r, dt, false);
}

} // namespace sr
