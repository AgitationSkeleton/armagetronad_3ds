# Nintendo 3DS Port Feasibility

## Decision

Porting Armagetron Advanced to Nintendo 3DS homebrew is feasible, but it is a
platform port rather than a straightforward cross-compile.

The game simulation and most of the networking code are portable C++. The main
work is replacing desktop OpenGL/GLU and SDL platform services, then supplying
3DS-compatible protobuf and XML support. A `.3dsx` should be the first delivery
target. A `.cia` uses the same ARM11 ELF and adds packaging metadata, icon, and
banner, so it does not introduce another game-code port.

The recommended first hardware target is New Nintendo 3DS. Old Nintendo 3DS
support should remain a later performance target until real-device profiling
establishes CPU, memory, and GPU budgets.

## Evidence

- The source tree contains 423 C/C++/header/proto files and about 5.3 MB of
  source under `src`.
- The core engine, network, game, UI, and renderer are separated into source
  directories, but OpenGL calls are not confined to `src/render`.
- The client requires SDL or SDL2, fixed-function OpenGL and GLU, SDL_image,
  SDL_mixer, protobuf, XML, threads, and BSD sockets in the desktop build.
- The installed devkitARM toolchain supplies libctru, Citro3D, Citro2D,
  `3dsxtool`, `smdhtool`, and `picasso`.
- The installed `C:\devkitPro\portlibs\3ds` does not currently supply SDL,
  protobuf, libxml2, or curl libraries.
- libctru exposes BSD sockets through `socInit`, and the JavaTron3DS reference
  demonstrates this on the same machine.
- The supplied PrBoom 3DS port contains a Citro3D fixed-function-style wrapper.
  It covers useful basics such as immediate-mode vertices, matrices, textures,
  blending, depth, alpha testing, and fog. It does not cover all APIs used by
  Armagetron, including GLU, lighting, clipping, texture coordinate generation,
  display lists, client arrays, and optional framebuffer paths.

## Dependency Plan

| Desktop dependency | 3DS plan | Risk |
| --- | --- | --- |
| SDL window/input/timing | Replace with libctru APT, HID, and system timing | Medium |
| OpenGL/GLU | Implement a Citro3D compatibility backend; disable optional effects first | High |
| SDL_image | Decode required PNG/JPEG assets on CPU or preprocess to 3DS textures | Medium |
| SDL_mixer | Add an NDSP backend; ship PCM/ADPCM-friendly assets | Medium |
| BSD sockets | Keep socket layer with a small `__3DS__` service adapter | Low-medium |
| pthread/Boost thread | Use libctru/newlib threading or remove background work initially | Medium |
| protobuf runtime | Cross-build a compatible protobuf runtime or isolate a smaller wire layer | High |
| libxml2 | Cross-build libxml2 or replace only the map/resource parsing subset | Medium-high |
| HTTP resource fetch | Disable initially; use RomFS/SD assets, then add curl later | Low initially |
| Desktop filesystem | Map packaged data to `romfs:/` and writable state to `sdmc:/3ds/armagetronad/` | Medium |

## Graphics Strategy

Use the PrBoom wrapper as a reference, not as a drop-in final renderer.

1. Add a 3DS GL compatibility header selected by `__3DS__`.
2. Implement the frequently used fixed-function subset on Citro3D:
   matrices, immediate-mode batching, color, texture coordinates, blending,
   depth, culling, viewport, and texture upload.
3. Implement `gluPerspective` and `gluLookAt` as matrix helpers.
4. Convert `GL_QUADS` and polygons to indexed triangles.
5. Make display lists execute immediately or cache converted vertex buffers.
6. Disable render-to-texture, advanced GLEW effects, texture coordinate
   generation, desktop stereo modes, and readback for the first playable build.
7. Add features back only after the arena, walls, cycles, HUD, and menu work.

The top screen should initially render at 400x240 with no stereoscopic 3D. The
bottom screen is best used for status, chat, touch keyboard, and later server
browsing rather than a second 3D viewport.

## Proposed Milestones

### M0: Toolchain bootstrap

- Build the shell in `platform/3ds`.
- Produce `.elf`, `.smdh`, and `.3dsx`.
- Verify APT lifecycle, HID input, Citro3D rendering, RomFS, and socket service
  initialization in Citra and on hardware.

### M1: Headless game core

- Add a hand-maintained `aa_config.h` for devkitARM.
- Build tools, math, configuration, simulation, and offline game code.
- Stub rendering, sound, resource downloads, and online play.
- Resolve ARM/newlib assumptions and measure code/data size.

### M2: First playable renderer

- Add the Citro3D compatibility layer.
- Render grid, walls, cycles, camera, text, and menus.
- Use conservative shaders and triangle batches.
- Target one local player and bots.

### M3: Platform UX

- Map circle pad, D-pad, face buttons, shoulder buttons, Start, and Select.
- Add touch keyboard/chat and bottom-screen status.
- Move configuration and logs to SD storage.
- Handle suspend, resume, and Home Menu exit safely.

### M4: Online play

- Initialize SOC before the existing socket stack.
- Port DNS, nonblocking I/O, select/poll behavior, and error translation.
- Add protobuf runtime support and verify protocol compatibility against a
  desktop server.
- Restore server browser and HTTP resource handling after direct-connect works.

### M5: Audio and packaging

- Implement NDSP effects and music.
- Tune memory, frame pacing, and draw-call batching on Old and New 3DS.
- Add production icon/banner metadata and a non-conflicting title ID.
- Package both `.3dsx` and `.cia`.

## Go/No-Go Gates

- **Gate 1:** M0 runs on real hardware and Citra.
- **Gate 2:** M1 links below the 3DS application memory budget with representative
  maps and bots.
- **Gate 3:** M2 sustains acceptable frame pacing in a four-cycle match.
- **Gate 4:** A 3DS client completes an online match with an unmodified desktop
  server.

Failure at Gate 2 would justify porting the older 0.2.8 client instead of current
trunk. Failure at Gate 3 would justify a purpose-built renderer while retaining
the game/network core. Neither outcome implies that a 3DS Armagetron client is
impossible.

## Local References

- `C:\Github\JavaTron3DS\javatron3ds`: APT, Citro2D, NDSP, RomFS, socket service,
  `.3dsx`, and `.cia` packaging patterns.
- `D:\PsyDoom-Project\Misc Sources\PrBoom 3DS Port\PrBoom-Plus-3DS-3ds`:
  Citro3D fixed-function-style GL wrapper.
- `D:\PsyDoom-Project\Misc Sources\ClassiCube\ClassiCube-master\src\3ds`:
  production-sized native Citro3D backend and 3DS platform layer.
- `D:\PsyDoom-Project\Game Console SDKs\Nintendo 3DS\3ds-examples-master`:
  local libctru/Citro3D examples.
- `D:\PsyDoom-Project\LEGOWB3DS`: working local `.3dsx` and `.cia` project layout.

