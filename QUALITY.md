# Vista quality overhaul — art direction and task brief

Goal: transform the current "textures slapped on a surface" into a scene you would
screenshot: a golden-hour alpine lake. Warm low sun, long soft shadows, snow-capped
ridges, pine forests on the slopes, a reflective lake in the valley, hazy layered
distance. Every task below serves that one image.

## Fixed art constants (exact values, used by multiple modules)

- Sun direction (world, normalized in code): `normalize(v3(0.45, 0.30, 0.55))`.
  game_sun_dir returns exactly this; the terrain light bake uses exactly this.
  timeofday[0] keeps advancing (water ripples, cloud drift, grass sway use it).
- WATER_LEVEL 58.0 (already in vista.h). Terrain gen must produce connected lakes:
  target 15-25% of the map below WATER_LEVEL, in valley floors.
- Shared ACES tonemap — every fragment shader REPLACES its `col/(1+col)` with:
```glsl
vec3 aces(vec3 x){ x *= 0.8; return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }
```
- Shared aerial perspective — every opaque fragment shader applies before tonemap:
```glsl
vec3 aerial(vec3 col, vec3 wpos, vec3 cam, vec3 sd){
    float dist = distance(wpos, cam);
    float f = 1.0 - exp(-dist * 0.00030);
    float sunAmt = pow(max(dot(normalize(wpos - cam), normalize(sd)), 0.0), 6.0);
    vec3 haze = mix(vec3(0.50, 0.58, 0.70), vec3(1.0, 0.75, 0.52), sunAmt);
    return mix(col, haze, f);
}
```
- Descriptor set 0 binding map (scene.c owns the layout): b0 FrameUBO, b1 heightmap
  R16, b2..b7 material color/normal pairs, b8 NEW terrain lightmap (RGBA8, R = sun
  shadow term 0..1, G = ambient occlusion 0..1, BA unused). Lightmap UV = same
  mapping as heightmap: `world.xz / 4000.0 + 0.5`.

## File ownership (agents MUST NOT edit files owned by others)

### terrain agent — owns src/terrain.c, src/scene.c, shaders/terrain.*
1. Heightmap overhaul in terrain.c: domain warping (warp sample coords by two
   low-frequency fbm fields, amplitude ~150-200m) over ridged multifractal whose
   amplitude grows with base altitude (sharp peaks, smooth valleys). Flatten valley
   floors as they approach WATER_LEVEL so lakes get natural shores. 15-25% of map
   below WATER_LEVEL. Keep TERRAIN_N/SCALE/HEIGHT and seed-determinism.
2. CPU light bake at startup into 1024x1024 RGBA8, uploaded via vkc_texture_rgba
   (linear, no mips needed; expose via terrain_lightmap_tex()). R = soft sun shadow:
   raymarch the heightmap toward the sun constant (~48 steps, growing step), penumbra
   by distance-to-occluder (smoothstep). G = AO: horizon angle sampled in 8 azimuths,
   ~12 steps each. Must complete < 2s (it is 1M texels of cheap loops).
3. scene.c: add b8 to the set0 layout + descriptor writes (all VISTA_FRAMES sets).
4. terrain.frag: light = suncol * shadow(R) * NdotL + skyambient * AO(G). Add snow:
   height > ~0.70*THEIGHT and slope < 0.45 blends to snow (albedo ~0.9 white,
   blue-tinted ambient); macro tint variation from very-low-frequency fbm (dry-grass
   yellows); wetness band: world.y < WATER_LEVEL+2.5 darkens and saturates albedo.
   Apply shared aerial() then aces(). Keep the lowq branch: Quest also samples the
   lightmap (single tap, cheap) but keeps skipping detail normal maps.

### water agent — creates src/water.c, shaders/water.vert, shaders/water.frag ONLY
Grid plane 128x128 quads spanning the full terrain at y=WATER_LEVEL, two small sine
wave octaves in the vertex shader (amplitude <= 0.15m). Fragment: ripple normal from
three scrolling derivative-noise octaves; fresnel (Schlick, F0 0.02) blending deep
water color vec3(0.02,0.09,0.10) toward an analytic sky reflection (inline compact
copy of the sky gradient + sun: zenith/horizon mix on the reflected ray + sun
specular pow ~300 glint); shore handling by sampling heightmap b1: depth =
WATER_LEVEL - terrainH; shallow (<1.5m) fades alpha toward 0.15 and lightens toward
teal, plus a foam line (noise-broken white band where depth < 0.5m). Pipeline:
alpha blend, depth test on, depth write on, cull none. Registered via water_init /
water_record / water_destroy already declared in vista.h and called by scene.c.
Sample the terrain lightmap b8 for shadowed water (multiply reflection by
0.6+0.4*shadow). Apply aerial() and aces().

### veg agent — owns src/veg.c, shaders/grass.*, shaders/rock.*, creates shaders/tree.vert, shaders/tree.frag
1. Procedural pine mesh generated in C at init (like the existing rock icosphere
   path): 6-sided trunk + 3 stacked foliage cones, ~150-250 tris, vertex colors
   (trunk brown 0.25,0.16,0.10; foliage two greens alternating per cone). 
2. Scatter once at init across the whole map: treeline band 0.18..0.55 of THEIGHT,
   slope < 0.4, terrain > WATER_LEVEL+1.5, clustered by thresholding a mid-frequency
   noise (forests, not uniform speckle). ~4000 instances PC, ~900 Quest. Per
   instance: position, uniform scale 0.7-1.5, yaw, small hue shift. Static instance
   buffer, one instanced indexed draw. Subtle wind sway on upper cone vertices.
3. tree.frag: N dot L with the fixed sun, sample terrain lightmap b8 at the
   instance base (pass shadow/AO from vertex stage - sample in VERTEX shader b8 once
   per vertex, cheap) so trees sit in terrain shadows correctly. aerial() + aces().
4. grass.frag/rock.frag: also sample lightmap b8 so grass and rocks darken in
   shadow; switch both to aerial() + aces(). Grass gets per-instance tint variation
   (yellowish dry / deep green) if not already present.
5. Rocks: scatter preference near shorelines and scree below cliffs; smooth the
   normals (average face normals at shared vertices) to kill the faceted look.

### sky agent — owns shaders/sky.frag, src/game.c
1. game.c: game_sun_dir returns the fixed sun constant. Keep time advancing.
2. game_init: spawn the player at a lakeside vantage: probe the heightmap via
   terrain_height_at in a spiral until finding a point 1-4m above WATER_LEVEL whose
   forward view (yaw toward map center) crosses water within 120m; set yaw/pitch to
   frame water + peaks. Iterate with screenshots until the default spawn shot looks
   like a postcard.
3. sky.frag: two cloud layers (high thin cirrus streaks stretched along wind +
   broken cumulus), both lit: silver-lining edge brightening via pow(dot(dir,sun))
   near cloud edges, warm color near the sun, cool gray-blue away; keep sun disc +
   glow; add a warm horizon haze band. aces() at the end (replace current mapping).

## Iteration protocol (every agent, after every meaningful change)

Build and LOOK at your work — repeat until it actually looks good:
```
cd D:/repo/vista
make BUILD=build_<yourkey> windows          (isolated build dir, no collisions)
cd build_<yourkey> && VISTA_SMOKE=1 ./vista.exe
```
Read build_<yourkey>/smoke.png as an image. Frame your feature with the camera
override: VISTA_CAM="x,z,yaw,pitch,time" (spawn overridden; y is auto terrain+eye).
Do not declare success from a compile — only from a screenshot you have looked at.

## Constraints

- C11 only, no comments in code, no new libraries, check every VkResult.
- Do not touch vista.h, Makefiles, plat_*, xr.c, vkcore.c (plumbing is pre-wired).
- Keep the Quest budget in mind: everything new must respect the lowq/mobile paths
  (fewer instances, single-tap lightmap is fine and encouraged).
- Existing behavior that must not regress: smoke mode exits 0, zero validation
  errors, all four make targets build.
