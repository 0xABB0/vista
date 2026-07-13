#include "vista.h"

GameState g_game;

#define GAME_PI 3.14159265358979f
#define GAME_ACCEL 40.0f
#define GAME_FRICTION 10.0f
#define GAME_GRAVITY 20.0f
#define GAME_JUMP 7.0f
#define GAME_WALK_SPEED 6.0f
#define GAME_SPRINT_MULT 1.8f
#define GAME_STEP 0.5f
#define GAME_LOOK_SENS 0.0025f
#define GAME_SNAP_RAD (GAME_PI / 6.0f)
#define GAME_PITCH_LIMIT 1.55f
#define GAME_FOVY 1.22f
#define GAME_ZNEAR 0.1f
#define GAME_ZFAR 8000.0f
#define GAME_DAY_SECONDS 600.0

static bool game_find_water_ahead(v3 pos, float yaw, float maxdist, float *out_dist)
{
    float sy = sinf(yaw), cy = cosf(yaw);
    v3 fwd = (v3){-sy, 0.0f, -cy};
    for (float d = 5.0f; d <= maxdist; d += 5.0f) {
        float x = pos.x + fwd.x * d;
        float z = pos.z + fwd.z * d;
        if (terrain_height_at(x, z) <= WATER_LEVEL) {
            *out_dist = d;
            return true;
        }
    }
    return false;
}

static float game_peak_ahead(v3 pos, float yaw)
{
    float sy = sinf(yaw), cy = cosf(yaw);
    v3 fwd = (v3){-sy, 0.0f, -cy};
    float best = 0.0f;
    for (float d = 200.0f; d <= 1600.0f; d += 100.0f) {
        float x = pos.x + fwd.x * d;
        float z = pos.z + fwd.z * d;
        float h = terrain_height_at(x, z);
        if (h > best) best = h;
    }
    return best;
}

void game_init(GameState *g)
{
    float half = TERRAIN_SCALE * 0.5f - 4.0f;
    float step = 8.0f;
    int maxring = (int)(half / step);
    bool found = false;
    float bx = 0.0f, bz = 0.0f, bh = 0.0f, byaw = 0.0f, bdist = 60.0f;
    float bscore = -1e9f;
    int candidates = 0;

    for (int ring = 0; ring <= maxring && candidates < 220; ring++) {
        int steps = ring == 0 ? 1 : ring * 8;
        for (int s = 0; s < steps; s++) {
            int ix, iz;
            if (ring == 0) {
                ix = 0;
                iz = 0;
            } else {
                int side = s / (ring * 2);
                int off = s % (ring * 2);
                if (side == 0) { ix = -ring + off; iz = -ring; }
                else if (side == 1) { ix = ring; iz = -ring + off; }
                else if (side == 2) { ix = ring - off; iz = ring; }
                else { ix = -ring; iz = ring - off; }
            }
            float x = (float)ix * step;
            float z = (float)iz * step;
            if (x < -half || x > half || z < -half || z > half)
                continue;
            float h = terrain_height_at(x, z);
            if (h < WATER_LEVEL + 1.0f || h > WATER_LEVEL + 4.0f)
                continue;
            v3 fwd = v3norm((v3){-x, 0.0f, -z});
            float yaw = atan2f(-fwd.x, -fwd.z);
            v3 pos = (v3){x, h + EYE_HEIGHT, z};
            float wd;
            if (!game_find_water_ahead(pos, yaw, 120.0f, &wd))
                continue;
            if (veg_min_tree_dist(pos) < 45.0f)
                continue;
            candidates++;
            float peak = game_peak_ahead(pos, yaw);
            float score = peak - fabsf(wd - 30.0f) * 2.0f;
            if (score > bscore) {
                bscore = score;
                bx = x;
                bz = z;
                bh = h;
                byaw = yaw;
                bdist = wd;
                found = true;
            }
        }
    }

    if (!found) {
        float bhh = -1e9f;
        for (int i = 0; i < 25; i++) {
            for (int j = 0; j < 25; j++) {
                float x = -half + (2.0f * half) * (float)i / 24.0f;
                float z = -half + (2.0f * half) * (float)j / 24.0f;
                float h = terrain_height_at(x, z);
                if (h > bhh) {
                    bhh = h;
                    bx = x;
                    bz = z;
                }
            }
        }
        bh = bhh;
        v3 fwd = v3norm((v3){-bx, 0.0f, -bz});
        byaw = atan2f(-fwd.x, -fwd.z);
        bdist = 60.0f;
    }

    g->pos = (v3){bx, bh + EYE_HEIGHT, bz};
    g->vel = (v3){0, 0, 0};
    g->yaw = byaw;
    float dy = WATER_LEVEL - g->pos.y;
    float pitch = atan2f(dy, bdist) * 0.45f;
    if (pitch > 0.05f) pitch = 0.05f;
    if (pitch < -0.35f) pitch = -0.35f;
    g->pitch = pitch;
    g->grounded = true;
    g->time = 25.0;
}

void game_update(GameState *g, const Input *in, float dt)
{
    if (dt <= 0.0f)
        return;
    if (dt > 0.1f)
        dt = 0.1f;

    g->yaw -= in->look_dx * GAME_LOOK_SENS;
    g->pitch -= in->look_dy * GAME_LOOK_SENS;
    g->yaw -= in->snap_turn * GAME_SNAP_RAD;
    if (g->pitch > GAME_PITCH_LIMIT)
        g->pitch = GAME_PITCH_LIMIT;
    if (g->pitch < -GAME_PITCH_LIMIT)
        g->pitch = -GAME_PITCH_LIMIT;
    if (g->yaw > GAME_PI)
        g->yaw -= 2.0f * GAME_PI;
    if (g->yaw < -GAME_PI)
        g->yaw += 2.0f * GAME_PI;

    float sy = sinf(g->yaw), cy = cosf(g->yaw);
    v3 fwd = (v3){-sy, 0.0f, -cy};
    v3 right = (v3){cy, 0.0f, -sy};
    v3 wish = v3add(v3scale(right, in->move_x), v3scale(fwd, in->move_y));
    float wl = v3len(wish);
    if (wl > 1.0f)
        wish = v3scale(wish, 1.0f / wl);

    v3 hvel = (v3){g->vel.x, 0.0f, g->vel.z};
    if (g->grounded) {
        float sp = v3len(hvel);
        if (sp > 0.0f) {
            float drop = sp * GAME_FRICTION * dt;
            float ns = sp - drop;
            if (ns < 0.0f)
                ns = 0.0f;
            hvel = v3scale(hvel, ns / sp);
        }
    }
    hvel = v3add(hvel, v3scale(wish, GAME_ACCEL * dt));
    float maxsp = GAME_WALK_SPEED * (in->sprint ? GAME_SPRINT_MULT : 1.0f);
    float hsp = v3len(hvel);
    if (hsp > maxsp)
        hvel = v3scale(hvel, maxsp / hsp);
    g->vel.x = hvel.x;
    g->vel.z = hvel.z;

    if (in->jump && g->grounded) {
        g->vel.y = GAME_JUMP;
        g->grounded = false;
    }
    g->vel.y -= GAME_GRAVITY * dt;

    float ox = g->pos.x, oz = g->pos.z;
    g->pos = v3add(g->pos, v3scale(g->vel, dt));
    float lim = TERRAIN_SCALE * 0.5f - 1.0f;
    if (g->pos.x > lim) g->pos.x = lim;
    if (g->pos.x < -lim) g->pos.x = -lim;
    if (g->pos.z > lim) g->pos.z = lim;
    if (g->pos.z < -lim) g->pos.z = -lim;

    float ground = terrain_height_at(g->pos.x, g->pos.z);
    float feet = g->pos.y - EYE_HEIGHT;
    if (g->grounded) {
        if (feet < ground - GAME_STEP) {
            g->pos.x = ox;
            g->pos.z = oz;
            ground = terrain_height_at(ox, oz);
            g->pos.y = ground + EYE_HEIGHT;
            g->vel.x = 0.0f;
            g->vel.z = 0.0f;
            g->vel.y = 0.0f;
        } else if (feet <= ground + GAME_STEP) {
            g->pos.y = ground + EYE_HEIGHT;
            g->vel.y = 0.0f;
        } else {
            g->grounded = false;
        }
    } else if (feet <= ground && g->vel.y <= 0.0f) {
        g->pos.y = ground + EYE_HEIGHT;
        g->vel.y = 0.0f;
        g->grounded = true;
    }

    g->time += dt;
}

void game_flat_ubo(const GameState *g, float aspect, FrameUBO *out)
{
    float cp = cosf(g->pitch), sp = sinf(g->pitch);
    float sy = sinf(g->yaw), cy = cosf(g->yaw);
    v3 fwd = (v3){-sy * cp, sp, -cy * cp};
    m4 view = m4look(g->pos, v3add(g->pos, fwd), (v3){0, 1, 0});
    m4 proj = m4perspective(GAME_FOVY, aspect, GAME_ZNEAR, GAME_ZFAR);
    m4 vp = m4mul(proj, view);
    out->viewproj[0] = vp;
    out->viewproj[1] = vp;
    out->campos[0] = g->pos.x;
    out->campos[1] = g->pos.y;
    out->campos[2] = g->pos.z;
    out->campos[3] = 0.0f;
    v3 sun = game_sun_dir(g);
    out->sundir[0] = sun.x;
    out->sundir[1] = sun.y;
    out->sundir[2] = sun.z;
    out->sundir[3] = 0.0f;
    game_fill_light(g, out);
}

void game_fill_light(const GameState *g, FrameUBO *out)
{
    atmos_update(game_sun_dir(g), g->pos.y, out);
    float frac = (float)fmod(g->time / GAME_DAY_SECONDS, 1.0);
#ifdef __ANDROID__
    float quality = 0.6f;
#else
    float quality = 1.0f;
#endif
    out->timeparams[0] = (float)g->time;
    out->timeparams[1] = frac;
    out->timeparams[2] = quality;
    out->timeparams[3] = quality;
    out->post[0] = 0.20f;
    out->post[1] = 0.012f;
    out->post[2] = 0.0f;
    out->post[3] = 0.0f;
}

v3 game_sun_dir(const GameState *g)
{
    float frac = (float)fmod(g->time / GAME_DAY_SECONDS, 1.0);
    float ang = frac * 2.0f * GAME_PI;
    const float tilt = 0.45f;
    v3 s = { cosf(ang), sinf(ang) * cosf(tilt), sinf(ang) * sinf(tilt) };
    return v3norm(s);
}
