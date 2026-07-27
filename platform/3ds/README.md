# Armagetron Advanced for Nintendo 3DS

This target compiles the canonical Armagetron Advanced client sources for ARM11
and packages the required game data in RomFS. It is not the earlier renderer
demo.

## Implemented

- top-screen 400x240 gameplay and menus through a Citro3D fixed-function
  compatibility layer;
- bottom-screen canonical menu background while menus are active;
- bottom-screen full-arena map during gameplay, using Armagetron's HUD map code
  for cycles, trails, walls, and zones;
- native circle-pad, D-pad, face-button, shoulder-button, and Start/Select
  bindings;
- Nintendo 3DS software keyboard for editable menu text;
- writable configuration, cache, resources, and screenshots under
  `sdmc:/3ds/armagetronad`;
- SDL mixer audio and network client dependencies;
- `.3dsx` output with embedded SMDH and RomFS;
- installable `.cia` packaging;
- screenshots to `sdmc:/3ds/armagetronad/screenshots` on L+R+Select;
- a crash reporter and an SD card runtime log.

## Build

```powershell
powershell -ExecutionPolicy Bypass -File .\platform\3ds\build.ps1 -Clean
```

The output is `platform/3ds/armagetronad-3ds.3dsx`.

Build both `.3dsx` and `.cia`:

```powershell
powershell -ExecutionPolicy Bypass -File .\platform\3ds\build.ps1 -Clean -Cia
```

The CIA script uses the read-only `makerom.exe` and `bannertool.exe` supplied in
`D:\PsyDoom-Project\LEGOWB3DS\tools`. Their paths and the development title ID
can be overridden through `package-cia.ps1` parameters.

## Default Controls

- Circle Pad or D-pad left/right: turn
- Circle Pad down, D-pad down, A, or B: brake
- X: switch view
- Y: glance back
- L/R and ZL/ZR: glance left/right
- C-stick: free camera look, on a New 3DS or with a Circle Pad Pro
- Start: game menu
- Select: scoreboard
- A in menus: select/open
- B or Start in menus: back
- L/R in menus: adjust left/right
- L+R+Select: screenshot

Chat is not bound to a button. It opens the software keyboard, which takes
both screens over mid-round, so it is left to the player to bind under Player
Setup. The same goes for the cockpit buttons that switch HUD readouts, which
also have a menu under System Setup.

## Diagnostics

The client writes a runtime log to `sdmc:/3ds/armagetronad/armagetron.log` and,
if it takes a CPU fault, a register dump to
`sdmc:/3ds/armagetronad/crash.log`. Both are also written to the debug output
channel, which Citra prints to its log when `Debug.Emulated` is set to `Trace`.

Creating an empty file at `sdmc:/3ds/armagetronad/debug_input` turns on verbose
tracing without a rebuild: HID and SDL input events, menu navigation, and a
memory report every 300 frames.

The startup log reports the memory the console actually granted, for example:

```
runtime: model=new3ds stack=1024KiB heap=95280KiB linear=24576KiB
memory[startup]: heapUsed=874KiB/95280KiB linearFree=0KiB/24576KiB ...
```

A single player match measured about 20 MiB of application heap, 17 MiB of
linear heap and a 20 KiB peak main thread stack. If a console reports much less
than that in `runtime:`, it does not have enough memory to run the client from
that entry point.

## Debugging with Citra

Citra's Qt frontend renders correctly but ignores synthetic keyboard input, so
scripted runs use the SDL frontend (`citra.exe`), where emulated buttons
respond. To get a call stack for a fault, set `use_gdbstub=true` in
`sdl2-config.ini` and attach:

```
arm-none-eabi-gdb platform/3ds/armagetronad-3ds.elf
(gdb) target remote localhost:24689
(gdb) break svcBreak
(gdb) continue
```

libctru reports fatal conditions through `svcBreak`, and Citra does not stop
the process when it happens, so breaking on `svcBreak` is the reliable way to
catch the first failure rather than the resulting storm of repeats.

## Runtime Status

The full client boots in Citra Nightly 2104. The first-run language menu, main
menu, offline single-player arena, top-screen HUD and bottom-screen live
cycle/trail map have been validated. A single player match that previously
faulted about seven seconds in now runs through repeated rounds of scripted
play with no emulated fault.

Two defects that explain both the emulator instability and the hardware black
screen have been fixed: the main thread stack was libctru's 32 KiB default,
carved out of the bottom of the heap so overruns silently corrupted the heap
instead of faulting, and the GL compatibility layer used citro3d immediate
mode, which overflowed the GPU command buffer in a busy scene.

A New 3DS running the `.3dsx` from the Homebrew Launcher reported a 95 MiB
application heap and a 24 MiB linear heap, the same grant an emulator gives,
so memory was never the reason it failed to start there. It died in the
autotools install prefix relocation: the launcher passes a path such as
`sdmc:/3ds/armagetronad-3ds.3dsx` as `argv[0]`, and the relocation code could
not reconcile that with the compiled in `/usr/local/bin`. Emulators pass an
empty `argv`, which short circuited the same code, so the failure only ever
appeared on hardware. There is no prefix to relocate on this console and the
relocation is now skipped.

Still requiring hardware validation: whether anything else fails after that
point, physical input feel, DSP audio, sleep and resume, long-session
stability, and CIA installation.

## Not Yet Done

- `socInit` is never called, so online play cannot work yet.
- Textures are uploaded at their desktop resolution, which is the largest
  consumer of linear heap. A texture budget would buy the most Old 3DS
  headroom.
- The 3DS-specific settings from `FULL_PORT_PLAN.md` (bottom-screen mode, map
  mode and rotation, stereoscopic top screen, New 3DS performance preference)
  are not implemented.
- `glReadPixels` is implemented against a display transfer and cannot be
  verified under Citra, whose hardware renderer does not write render targets
  back to guest memory. It needs a check on a console.
