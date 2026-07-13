# Vista — technical specification

Pure-C (C11) Vulkan walking-exploration game. First-person walk across a large procedural
canyon/island landscape with long vistas, dynamic sky, and dense instanced vegetation.
Four build targets from one codebase, make-only builds, no C++ anywhere.

| target      | artifact                | how it presents                                    |
|-------------|--------------------------|----------------------------------------------------|
| windows     | build/vista.exe          | Win32 window + VK_KHR_win32_surface swapchain       |
| windows_vr  | build/vista_vr.exe       | OpenXR (Meta Link / any PC runtime), Vulkan session |
| android     | android/vista.apk        | NativeActivity + VK_KHR_android_surface             |
| android_vr  | android_vr/vistavr.apk   | Quest NativeActivity + OpenXR Vulkan session        |

Future (do not implement, keep abstractions clean): ios, macos, web, webxr, visionos.

## Decisions (fixed — do not relitigate)

- Vulkan 1.1 minimum (Quest 2 baseline). No dynamic rendering; classic VkRenderPass.
- VR stereo: single render pass with VK_KHR_multiview, one XR swapchain with arrayLayers=2.
  Shaders index per-eye matrices with gl_ViewIndex under `#ifdef MULTIVIEW`.
- Vulkan functions loaded via volk (vendor/volk.h, VK_NO_PROTOTYPES everywhere).
- OpenXR loaded dynamically at runtime: LoadLibraryA("openxr_loader.dll") on Windows
  (vendor/openxr_loader.dll is copied next to the exe by make), dlopen("libopenxr_loader.so")
  on Quest (packaged in the APK). XR_NO_PROTOTYPES; bootstrap everything from
  xrGetInstanceProcAddr. Never link the loader at build time.
- VR Vulkan objects are created THROUGH OpenXR (XR_KHR_vulkan_enable2:
  xrCreateVulkanInstanceKHR, xrGetVulkanGraphicsDevice2KHR, xrCreateVulkanDeviceKHR).
  vkcore exposes split creation so xr.c can drive it; flat targets create directly.
- Compile-time target selection: `VISTA_VR` defined for the two VR targets.
  Platform: `_WIN32` / `__ANDROID__`. Multiview shader variant: `MULTIVIEW`.
- Terrain: 2048x2048 uint16 heightmap generated on CPU at startup (FBM + ridged noise,
  fixed seed 1337), uploaded as R16_UNORM. World is TERRAIN_SCALE meters square,
  TERRAIN_HEIGHT meters tall. CPU and GPU sample the SAME uploaded map so collision
  matches rendering exactly (terrain_height_at bilinearly samples the CPU copy).
- Terrain rendering: grid of 64x64 chunk patches, tessellation pipeline
  (VK_PRIMITIVE_TOPOLOGY_PATCH_LIST, quad patches) with distance-based tess levels
  computed in the .tesc stage (edge-midpoint screen-space metric, cracks avoided by
  computing per-edge levels from shared edge midpoints). Frustum culling of chunks on CPU.
  If tessellationShader feature is absent (some phone GPUs): fallback pipeline using
  terrain_static.vert with a pre-subdivided grid mesh per chunk. has_tess in VkCore decides.
- Texturing: splat by slope+height between 3 PBR sets (grass/rock/dirt), each Color +
  NormalGL from ambientCG 2K JPG. Detail FBM in fragment shader breaks tiling. Fog by
  distance, simple Reinhard tonemap. sRGB color targets.
- Sky: fullscreen triangle, analytic gradient atmosphere + sun disc + FBM clouds,
  drawn after terrain with depth test EQUAL-less trick (depth write off, LESS_OR_EQUAL at far).
- Vegetation: instanced grass blades (crossed quads, wind sway in vertex shader) scattered
  procedurally on flat grassy areas near the camera (ring buffer of instances re-scattered
  as the player moves), plus instanced rocks. Target ~100k grass instances on PC,
  ~20k on mobile (VISTA_MOBILE_BUDGETS when __ANDROID__).
- MSAA 4x everywhere (resolve into swapchain/XR image). Depth D24S8 or D32 (probe support).
- Frames in flight: 2. One HOST_VISIBLE|COHERENT persistently-mapped FrameUBO per slot.
- Locomotion: flat = WASD + mouse look (raw deltas), sprint shift, jump space.
  Android flat = left half of screen virtual stick, right half look drag.
  VR = left thumbstick head-relative smooth locomotion, right thumbstick 30-degree snap
  turn, jump = A/right trigger click. Eye height 1.7m flat; VR uses runtime view pose on
  top of body position (STAGE space if available else LOCAL).
- Smoke mode (flat windows only): if env VISTA_SMOKE=1, render 120 frames, then copy the
  last swapchain image to a host buffer and write build/smoke.png via stb_image_write,
  print "SMOKE OK", exit 0. This is the CI/verification hook.

## Per-platform showcase notes (put the technique where it belongs)

- windows: multi-draw of terrain chunks, persistent mapping, aniso 8, 100k instances.
- android: tessellation LOD is the story; keep swapchain FIFO, 3 images, and use
  VK_PRESENT_MODE_FIFO_KHR only. Respect suspend/resume: destroy/recreate swapchain +
  surface on APP_CMD_TERM_WINDOW/APP_CMD_INIT_WINDOW without losing game state.
- windows_vr / android_vr: multiview single-pass stereo, xrWaitFrame-paced loop, poses
  queried with the frame's predictedDisplayTime, XR_ENVIRONMENT_BLEND_MODE_OPAQUE, MSAA
  resolve directly into XR swapchain images. Handle XR_SESSION_STATE_* transitions
  (READY->begin, STOPPING->end, EXITING->quit) and XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING.

## Module map (one file per module; contracts in src/vista.h — MATCH EXACTLY)

- src/vkcore.c — volk init, instance/device creation (direct AND xr-driven split),
  swapchain (flat), render pass (multiview variant when layers==2), MSAA + depth targets,
  framebuffers, per-frame sync/command buffers, buffer/image/texture/shader helpers,
  one-shot command helper, FrameUBO ring, descriptor pool + common sampler.
- src/scene.c — owns pipelines' shared layout (set 0 = FrameUBO + heightmap + splat
  textures; push constants for per-draw data), calls terrain/sky/veg init/record in order.
- src/terrain.c — heightmap generation (also exposes terrain_height_at for CPU),
  chunk grid + frustum cull, tess + static pipelines, records terrain draws.
- src/sky.c — sky pipeline + record.
- src/veg.c — instance scatter + grass/rock pipelines + record.
- src/game.c — GameState, walk physics (accel, friction, gravity, ground snap to
  terrain_height_at + capsule step), input mapping to movement, view matrices for flat.
- src/assets_io.c — stb_image decode of ambientCG JPGs -> vkcore textures; asset read
  through plat_load_asset so it works from APK or disk.
- src/plat_win.c — WinMain/main, Win32 window, raw mouse input, message pump, flat main
  loop, smoke mode; when VISTA_VR: skips window, runs xr loop.
- src/plat_android.c — android_main via android_native_app_glue (vendor/), lifecycle
  state machine, input events, flat android loop; when VISTA_VR: xr loop.
- src/xr.c — dynamic loader bootstrap, XrInstance/system/session, vulkan_enable2 device
  creation through vkcore split, reference space, action set (thumbsticks/buttons),
  XR swapchain (2-layer) + framebuffers via vkcore, frame loop helpers.
- src/math.c — implements the non-inline m4/q4 prototypes declared in vista.h
  (m4identity, m4mul, m4perspective, m4frustum_xr, m4look, m4from_rt, m4inverse_rt,
  q4rotate). Column-major, Vulkan clip space (Y down handled via negative viewport OR
  baked into projection — pick one, document in the file with the projection math itself,
  and be consistent everywhere).

## Assets

`make assets` downloads CC0 sets from ambientCG (2K JPG) into assets/tex/<Name>/:
Grass004, Rock035, Ground048. URL form:
https://ambientcg.com/get?file=<Name>_2K-JPG.zip (curl -L, then unzip).
Used maps: <Name>_2K-JPG_Color.jpg and <Name>_2K-JPG_NormalGL.jpg.
If an ID 404s, pick the closest existing ambientCG ID and update the Makefile.

## Shaders (shaders/, GLSL 450, compiled by glslc at build time)

terrain.vert terrain.tesc terrain.tese terrain.frag terrain_static.vert
sky.vert sky.frag grass.vert grass.frag rock.vert rock.frag
Each compiled twice: plain -> build/shaders/<name>.spv and with -DMULTIVIEW
--target-env=vulkan1.1 -> build/shaders/<name>.mv.spv. Loaded via plat_load_asset
("shaders/<name>[.mv].spv"). APK builds pack build/shaders into APK assets.

## Builds (make only; top Makefile + android/Makefile + android_vr/Makefile)

Host tools assumed on PATH (already true on this machine): clang (VS2022), glslc
(VULKAN_SDK also set), Git-bash coreutils, zip/unzip/envsubst, java/keytool, adb, make.
Android: ANDROID_HOME set; NDK auto-detected like rawdrawandroid does; android-34 jar
at $(ANDROID_HOME)/platforms/android-34/android.jar; keystore per-project via keytool.
Windows compile: clang -O2, link user32 gdi32 (no vulkan-1.lib — volk loads dynamically).
Android compile: NDK clang, arm64-v8a only, API 29, -shared -uANativeActivity_onCreate,
link -landroid -llog -lm (no libvulkan — volk dlopens it).
The android_vr APK packages vendor/libopenxr_loader.so in lib/arm64-v8a/ and uses the
Quest manifest (headtracking feature, com.oculus.intent.category.VR, supportedDevices).

## Quality bar

- Zero Vulkan validation errors in smoke mode with VK_LAYER_KHRONOS_validation.
- 72+ fps target on Quest 2 class hardware: respect mobile budgets, no per-frame
  allocations, no vkQueueWaitIdle in the frame loop, transitions via subpass dependencies.
- No global mutable state except the platform singletons already in vista.h.
- No comments in code.
