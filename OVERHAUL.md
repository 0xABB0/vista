# Vista graphics overhaul — plan, status, and session handoff

## Goal

Owner verdict on the current visuals: "worse than Oblivion". Target: a landscape you'd
screenshot at any hour — more beautiful than Crysis 3. Approved decisions:
1. **Nothing is sacred** — SPEC.md decisions may be revisited when it buys visual quality.
2. **macOS headless dev runner** is the iteration loop (this Mac, MoltenVK, M3 Pro).
3. **Quality tiers** — one codebase; tier 1 = desktop full pipeline, tier 0 = mobile/Quest scaled.
4. **Dynamic time-of-day sun** — no sun-dependent bakes; sun-independent bakes (AO, horizon) fine.

## Current status

- [x] **S0 macOS headless runner** — DONE, verified. `make macos && cd build && VISTA_SMOKE=1 ./vista`
  renders 120 frames on MoltenVK and writes `smoke.png`. `VISTA_CAM="x,z,yaw,pitch,time"` frames the
  camera, `VISTA_SHOT=name.png` sets output, `VISTA_SMOKE_FRAMES=n` frame count, `VISTA_NO_TESS=1`
  forces the static terrain path, `VISTA_TIER=0|1` overrides quality tier. Android/Quest APKs build
  from this Mac (`make android`, `make android_vr`; NDK toolchain dir + symlink-deref fixes applied).
  Assets: `make assets` downloads the ambientCG texture sets into `assets/tex/`. A self-referencing
  `assets/tex` symlink was committed earlier; with `core.symlinks=false` it checks out as a 40-byte
  text file that blocks `make assets` — delete that file first (done in this worktree). The symlink
  should be dropped from git.
- [x] **S1 HDR skeleton** — DONE, verified. Scene renders linear HDR into RGBA16F MSAA (tier 0 probes
  R11G11B10) + resolve; final fullscreen pass does exposure → AgX → grade → vignette → grain into the
  swapchain/offscreen target. New FrameUBO; set 0 (global) / set 1 (texset) split; TIER specialization
  constant; shared `shaders/inc/common.glsl`; per-shader `aces()` deleted. All five targets compile.
- [x] **S2 Hillaire atmosphere + dynamic sun** — DONE, verified. The "black sky + black rectangles"
  defect was never atmosphere math (all three LUTs dumped clean): it was two MoltenVK-specific
  hazards, intermittent per run (~5/12 baseline). Fix: tessellated terrain now renders in its own
  render pass on MoltenVK (`VkCore.tess_split_pass`; `vkc_pass_scene_pre/_main`, split
  `scene_record_terrain/_rest`), memoryless MSAA attachments are disabled in that mode, and the
  init transmittance/multiscatter LUTs render as two fenced submissions. See "MoltenVK pitfalls".
  Verified: 8/8 tier-1 and 8/8 tier-0 smoke runs clean; golden-hour spawn, noon and evening skies
  correct (sun disc, warm horizon, aerial perspective). Outputs cluster into two byte variants
  differing only in grass/tree A2C edge noise — benign; md5-compare against a known-good PNG
  therefore needs both references.
- [x] **S3 Bloom** — DONE, verified on screenshots (sun disc halo, bloomed pond glints, no fireflies
  across repeated runs, tier 0 clean). Dual-filter chain in render.c + shaders/bloom_down.frag /
  bloom_up.frag (Karis on first down, 9-tap tent additive up via `load_existing`), composite in
  post_final.frag, fully gated off on tier 0. Also: water.frag gained a two-lobe sun-glitter term
  (Blinn pow 900 core + pow 64 path, fresnel/visibility scaled) because pre-S3 water specular never
  got HDR-bright enough to bloom; that term is shared ALU on both tiers, so tier-0 lake highlights
  are brighter than the S2 baseline (broad saturated patch, no glow) — revisit in S8 which reworks
  water specular to GGX anyway. Note: repeated same-cam runs now come out byte-identical.
- [x] **S4 Shadows** — DONE, verified on screenshots (same camera at t=25/150/280: long east shadows →
  near-none → long west shadows; silhouette tree/rock shadows anchored at bases, no blobs left; ridge
  shot shows the horizon-map massif shadow with light pools through crest gaps; repeat runs
  byte-identical; tier 0 clean). Implementation: src/procgen.c unifies CPU noise + forest_mask (veg
  3-octave clump variant won) and is in all platform source lists. Horizon map: CPU bake at init
  (terrain.c bake_horizon, ~0.3 s, 512²×8 azimuths → 2×RGBA8); lightmap R now dead (255), G=AO,
  B=canopy kept. CSM: D16 2D-array (tier 1 3×2048 radii 18/60/200, tier 0 2×1024 radii 20/90),
  camera-centered bounding-sphere cascades + texel snap, depth-only caster passes (vkc_pass_depth /
  vkc_pipe_depth, bias 4/1.75) recorded before the scene pass; casters = terrain STATIC path (pitfall
  1) + trees + rocks, ShadowPC{mat4 vp; vec4 chunk} push constant. Set 0 grew to 9 bindings: b6/b7
  horizon A/B, b8 cascade array via comparison sampler (portability subset now enables
  mutableComparisonSamplers — was a validation error otherwise). FrameUBO += cascade_vp[3],
  cascade_radii, cascade_texel. shaders/inc/shadow.glsl sun_visibility() = min(horizon smoothstep by
  sun azimuth/elevation, CSM 9-tap PCF w/ normal-offset bias) used by terrain/grass/tree/rock/water.
  Blob shadows deleted (veg.c pcShadow double-draws, tree/rock shadow pipelines, shader mode
  branches); rock.vert noise moved to shaders/inc/noise3.glsl.
- [ ] **S5 Terrain material** — hex-tiling (IQ single-lookup variant tier 0), triplanar above slope
  threshold (GPU-Gems-3 normal blend), height-based splat blending, load roughness maps (pack into
  color alpha in assets_io.c), GGX specular, snow sparkle + dithered snowline. The albedo GAIN hacks
  are already deleted. Verify: closeup, ridge, snowline shots — no visible tiling at mid distance.
- [ ] **S6 Trees** — procedural pine: trunk + alpha-tested needle cards from a procedurally generated
  atlas (init-time), spherical canopy normals, backlit translucency term, cell-based CPU culling
  (28k trees currently drawn uncull ×2), far cross-card LOD. DELETE cone builder (veg.c build_cone/
  tree_gen). Investigate floating rocks while in veg.c (visible in early smoke shots — rocks hanging
  in the sky; scatter or instance-buffer bug). Verify: forest near/far + frame time.
- [ ] **S7 Grass** — Tsushima-style: curved blades, Voronoi clump orientation/height, two-scale wind,
  view-space thickening, translucency + spec ridge, base AO, distance density falloff. DELETE crossed
  quads. Verify: closeup and mid-distance shots.
- [ ] **S8 Water** — flow-mapped dual-scrolling normal detail, keep heightmap-depth absorption + foam,
  GGX sun glints; tier 1: planar reflection pass (half-res mirrored terrain+trees+sky, mirrored
  viewproj in UBO, discard below plane); tier 0 keeps analytic sky reflection. Verify: lake shots
  both tiers, reflections contain trees.
- [ ] **S9 Clouds + god rays + polish** — tier 1: quarter-res raymarched volumetric clouds
  (Perlin-Worley 128³ RGBA8 3D texture generated on CPU at init, weather coverage map, HG phase +
  Beer, 32–48 steps), composited in sky.frag via screen-space sample so geometry occludes free; tier
  0: improved 2D layer. Screen-space god rays: sky writes sun-visibility mask into scene alpha
  (opaque writes a=0; grass A2C alpha-blend trick: colorFactors ONE/ZERO, alphaFactors ZERO/ONE),
  half-res radial blur toward CPU-projected sun screen pos. Final exposure/grade tuning, postcard
  spawn framing, rewrite SPEC.md/QUALITY.md to describe the new pipeline. Verify: sun-behind-clouds,
  dawn shafts through trees, postcard default spawn.

## Architecture (implemented S0–S2)

- **Pass graph tier 1**: per-frame sky-view LUT (192×108) → scene pass (MSAA RGBA16F + depth →
  resolve, SHADER_READ_ONLY) → final post pass (exposure/AgX/grade/vignette/grain) → target.
  On MoltenVK with tessellation (`VkCore.tess_split_pass`) the scene pass splits in two: terrain
  pass (clear, store MSAA color+depth) then main pass (load, veg/water/sky, resolve); MSAA
  attachments are regular device memory in that mode, transient/lazy elsewhere.
  Startup: transmittance LUT 256×64 + multi-scatter LUT 32×32 (two separately fenced one-shot
  fragment passes). Tier 0: same minus future bloom/god rays/clouds; R11G11B10 when supported
  (runtime probe).
- **Sync**: single queue, single cmdbuf; all ordering via render-pass external dependencies; no
  barriers in the frame path; offscreen targets single-buffered, initialLayout UNDEFINED (the
  split main pass loads them in COLOR/DEPTH_ATTACHMENT_OPTIMAL).
- **Descriptors**: set 0 (per frame slot): b0 FrameUBO, b1 heightmap R16, b2 lightmap (R=baked sun
  shadow — dies in S4, G=AO, B=canopy), b3 transmittance LUT, b4 multiscatter LUT, b5 sky-view LUT.
  set 1 = generic 8-sampler "texset" (scene_alloc_texset/scene_write_texset): material set (6 slots)
  bound in scene_record; post/LUT passes bind their own texsets. One pipeline layout {set0, texset},
  128B push constants, everywhere.
- **FrameUBO** (vista.h): viewproj[2], campos, sundir, sun_radiance(rgb)+exposure(w),
  ambient_zenith, ambient_horizon, timeparams(time, todfrac, quality, tessq),
  post(vignette, grain). CPU fills via game_fill_light → atmos_update (src/atmos.c mirrors the GLSL
  atmosphere: sun transmittance, zenith/horizon ambient integration, exposure curve).
- **Files**: src/render.c (frame graph owner: HDR targets, LUTs, post pipeline, render_record);
  src/atmos.c (CPU atmosphere); src/plat_macos.c (headless runner); shaders/inc/common.glsl (UBO,
  TIER, constants), shaders/inc/atmosphere.glsl (medium, LUT mappings, atm_sky, aerial);
  fullscreen.vert, post_final.frag, lut_transmittance/multiscatter/skyview.frag.
  vkcore.c helpers: vkc_image_create, vkc_pass_scene, vkc_pass_color(load_existing), vkc_pass_begin/
  destroy, vkc_tier_spec, OffChain (headless), portability ext probing, VISTA_NO_TESS/VISTA_TIER.
- **Platforms**: flat/XR/headless all call render_record(cmd, slot, final_fb, w, h). The old
  3-attachment vkc_targets_create is now final-target-only (single attachment, per-image fbs).

## Technique references (researched, licenses checked)

| Area | Technique | Reference |
|---|---|---|
| Tone map | AgX analytic + warm/cool grade | github.com/bWFuanVzYWth/AgX, github.com/dmnsgn/glsl-tone-map |
| Bloom | Dual-filter mip chain, Karis average | Jimenez SIGGRAPH 2014 (iryoku.com) |
| Sky | Hillaire 2020 scalable sky LUTs | github.com/sebh/UnrealEngineSkyAtmosphere (MIT) |
| Far shadows | Baked horizon map, 8 azimuths | iquilezles.org/articles/terrainmarching |
| Near shadows | Stable CSM + normal-offset bias + PCF | github.com/TheRealMJP/Shadows (MIT) |
| Terrain | Hex-tiling, triplanar w/ GG3 normal blend, height splat | github.com/mmikk/hextile-demo (MIT), bgolus article |
| Grass | GoT clumps/wind/thickening | GDC 2021 talk; github.com/GarrettGunnell/Grass, github.com/2Retr0/GodotGrass (MIT) |
| Trees | Needle cards, spherical normals, translucency; impostors far | GPU Gems 3 ch.16; shaderbits octahedral impostors; github.com/wojtekpil/Godot-Octahedral-Impostors (MIT) |
| Water | Depth absorption + foam + planar reflection | Far Cry 5 / Sea of Thieves talks; github.com/Chrisknyfe/boujie_water_shader (MIT) |
| Clouds | Schneider raymarch, Perlin-Worley | github.com/clayjohn/godot-volumetric-cloud-demo-v2 (MIT) |
| God rays | Screen-space radial blur | GPU Gems 3 ch.13 |

## Build & verify loop (from worktree root)

```
make macos && cd build && VISTA_SMOKE=1 ./vista            # writes build/smoke.png, exit 0 + "SMOKE OK"
VISTA_CAM="1200,600,2.4,-0.05,30" VISTA_SHOT=x.png VISTA_SMOKE_FRAMES=5 ./vista   # framed shot
```
Fixed-vantage harness: `tools/capture.sh <prefix> [names...]` builds macos, renders every row of
`tools/vantages.tsv` (name, VISTA_CAM or `-`, frames, tier or `-`, repeat count) into
`build/shots/<prefix>_<name>.png`, logs stdout to `build/shots/<prefix>.log`, extracts validation
messages, exits nonzero on any failure. `SKIP_BUILD=1` skips the build. Add a row (proven exact
coords only) whenever a stage discovers a new useful vantage; capture agents should run this
harness for the standard set and hand-frame only genuinely new subjects.
Read the PNG with the Read tool and LOOK at it. Standard vantages: default spawn (lakeside),
`VISTA_CAM` ridge/valley shots, and 3 times of day via the time field (25=morning golden hour,
150=noon, 280=evening). Terrain spans ±2000 on x/z (TSCALE 4000) and the camera clamps inside
that; keep VISTA_CAM coords well within it or you shoot from the map edge. Intermittent defects
exist (GPU scheduling dependent): judge a change on several runs, not one. Per stage also: `make android && make android_vr` (verifies shared C +
shaders for all targets; Windows compiles only on the owner's box — keep plat_win.c/xr.c changes
conservative). Validation layer auto-enabled under VISTA_SMOKE; watch stdout for errors.

## MoltenVK pitfalls (learned fixing S2 — respect these in every later stage)

1. **Tessellated draws poison the rest of their render pass.** MoltenVK emulates tessellation with
   a compute pre-pass and splits the Metal encoder at the tessellated draw; draws recorded after it
   in the same VkRenderPass are unreliable (dropped or depth-broken → the old "terrain only, black
   sky" frames). Rule: on MoltenVK a tessellated draw is the only draw in its render pass. This is
   what `VkCore.tess_split_pass` implements; if a later stage adds tessellated water or similar, it
   needs the same isolation.
2. **Memoryless (TRANSIENT + LAZILY_ALLOCATED) attachments cannot survive an encoder split.** Tile
   memory is discarded at the split → the blocky garbage-rectangle mosaics. Transient MSAA is kept
   for Android/Quest and disabled when `tess_split_pass` is set.
3. **Don't trust render-pass external dependencies across passes inside one command buffer for
   write→sample chains at init.** The multiscatter LUT intermittently read a half-written
   transmittance LUT despite correct subpass dependencies (whole-frame R/B-NaN → the "green
   frames"). One-shot LUT chains submit each pass with its own fence wait (cheap, init only).
   The per-frame skyview→scene chain has shown no such failure; if it ever does, the same
   treatment applies.
4. Validation stays silent for all of the above — they are Metal translation behaviors, not spec
   violations. A compiling, validation-clean build proves nothing here: look at several PNGs.

## Process constraints

- Work only in this worktree; the owner reviews uncommitted changes. NEVER commit or push (CLAUDE.md).
- No comments in code, C11 only, check every VkResult, no new runtime libraries.
- Every stage leaves all five targets compiling; smoke mode exits 0 with zero validation errors.
- Quest 2 budget: tier 0 avoids bloom/god rays/clouds/planar reflections; minimize fullscreen passes.
- The old per-file noise duplication should keep shrinking (procgen.c in S4; shaders/inc as needed).

## Keeping context lean (workflow orchestration)

Run each stage as one Workflow invocation from the main session. Suggested shape per stage:
1. `agent` (implementation): gets this file's stage spec + the relevant file list; edits code in this
   worktree; returns a diff summary only.
2. `agent` (capture, model=haiku effort=low): runs `tools/capture.sh <stage>` + the android builds
   verbatim and returns paths + validation/build tail. Never a frontier-model agent; it executes
   commands, it does not think. New vantages are added to tools/vantages.tsv by the implementation
   agent so the harness stays the single capture path.
3. `agent` (judge): Reads the PNGs, scores them against the stage's verification criteria, returns
   pass/fail + concrete visual defects.
4. Loop 1–3 until the judge passes (bound the loop, e.g. 4 iterations, then surface to the owner).
Main session only reads the final judge report and the last screenshots; it never ingests file dumps.
Debugging sub-problems (like the S2 defect) also fit this: a finder workflow with hypothesis agents
each testing one suspect (LUT dump, ms-bypass, mapping check) in parallel, reporting one verdict each.
