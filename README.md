# Armagetron Advanced for Nintendo 3DS

A port of [Armagetron Advanced](https://gitlab.com/armagetronad/armagetronad) to
the Nintendo 3DS. It is the whole game, not a reimplementation: the same
physics, the same configuration language, the same network protocol, so it
plays on the public servers alongside desktop clients.

The upstream project's own README is [here](README).

## Getting it

Grab the newest [release](../../releases). There are two files and you only
need one of them:

| File | What to do with it |
| --- | --- |
| `armagetronad-3ds.cia` | Install it with FBI. It appears on the HOME menu with its own icon. |
| `armagetronad-3ds.3dsx` | Copy it to `sdmc:/3ds/` and start it from the Homebrew Launcher. |

Either needs a 3DS running custom firmware. Both builds are identical inside;
the CIA is simply the more convenient of the two.

## The two screens

The top screen is the game. The bottom screen is the arena map during play, and
the menu during menus.

Menus are split: the title and the help text for whatever is selected stay on
the top screen, while the list of options sits on the touch screen, where it
can be tapped. The D-pad and the face buttons still work, so nothing forces you
to use the touch screen.

## Controls

| Button | Action |
| --- | --- |
| Circle Pad, D-pad | Left and right turn, down brakes |
| A, B | Brake |
| L, ZL | Glance left |
| R, ZR | Glance right |
| Y | Glance back |
| X | Change camera |
| Start | In-game menu |
| Select | Scores |
| C-stick | Free look, on a New 3DS or with a Circle Pad Pro |
| L + R + Select | Screenshot, written to `sdmc:/3ds/armagetronad/screenshots` |

Every one of these can be changed under Player Setup. Chat is deliberately left
unbound: it opens the software keyboard, which takes over both screens
mid-round, so it does not belong on a button that is easy to hit while driving.
Bind `CHAT` yourself if you want it.

## Settings worth knowing about

Under **Screen Settings**:

- **3D depth** — how far the 3D slider is allowed to go. The slider is the real
  control; with it at the bottom the picture is flat and costs nothing, because
  the second eye is only drawn when there is parallax to draw.
- **Texture detail** — the largest texture the game keeps. A moviepack drawn for
  a desktop can ask for more texture memory than the console has; lower this if
  one runs you out of memory.
- **Trail line width** — a cycle trail seen end-on is a single line. Raise this
  if trails are hard to follow at a distance.

Under **HUD Elements**: the score, speed and status readouts on the top screen
can each be switched off.

## Your own content

On first run the game creates `sdmc:/3ds/armagetronad`, which works like the
user data directory of the desktop client:

```
sdmc:/3ds/armagetronad/
    config/         your settings, written here
    moviepack/      a moviepack: drop its contents in so settings.cfg is here
    textures/       replacement textures
    models/         replacement models
    sound/          replacement sounds
    music/          music, played in the menus
    resource/       cockpits and other downloaded resources
    screenshots/    L + R + Select
    var/            server bookmarks and the like
```

Anything you put there is used in preference to the copy packed into the
application, so desktop content works unchanged. Enable a moviepack under
System Setup, Misc Stuff.

## Building it

Releases are built by [the workflow](.github/workflows/build.yml) on Ubuntu, so
that is the reference. You need [devkitPro](https://devkitpro.org/) with
devkitARM, libctru and citro3d, plus these portlibs:

```
dkp-pacman -S 3ds-sdl 3ds-sdl_mixer 3ds-sdl_image 3ds-libpng \
             3ds-libjpeg-turbo 3ds-zlib 3ds-bzip2 3ds-freetype \
             3ds-curl 3ds-mbedtls 3ds-libmad 3ds-libvorbisidec \
             3ds-libogg 3ds-libmikmod
```

protobuf, libxml2, FTGL and the boost headers are not in the portlibs, so they
are vendored under `platform/3ds/vendor` and cross built from there.

```sh
sh platform/3ds/build-dependencies.sh   # protobuf, libxml2, FTGL, and protoc
sh platform/3ds/make-version-header.sh  # src/tTrueVersion.h, from the git history
sh platform/3ds/prepare-romfs.sh        # packs config, textures, sounds, music
make -C platform/3ds -j"$(nproc)"       # -> armagetronad-3ds.3dsx
sh platform/3ds/package-cia.sh          # -> armagetronad-3ds.cia
```

The first step takes far longer than the rest put together and only needs
redoing when the vendored sources change.

On Windows the same steps are `build-dependencies` by hand, then
`platform\3ds\build.ps1` and `platform\3ds\package-cia.ps1`.

Packaging a CIA also needs
[makerom](https://github.com/3DSGuy/Project_CTR) and
[bannertool](https://github.com/carstene1ns/3ds-bannertool); point `MAKEROM` and
`BANNERTOOL` at them if they are not on the path.

### Releasing

Push a tag beginning with `v`. The release workflow builds through the same
path as every other build and publishes the 3dsx and the CIA against that tag.

```sh
git tag v0.1.0
git push origin v0.1.0
```

## What is different from the desktop client

The port is the same game, but the console is not a PC, and some things had to
change to fit it:

- The renderer is a compatibility layer over citro3d rather than OpenGL, since
  the PICA200 has no fixed-function pipeline in the OpenGL sense.
- Textures are rescaled on load to what the hardware accepts: powers of two, at
  most 512 texels by default. Moviepacks that ignore this still work.
- Menus and the HUD are laid out for 400x240 and split across the two screens.
- The floor uses a single texture. The desktop client's two-texture floor
  saturates to white at this resolution.
- Music, moviepacks and cockpits are read from the SD card as described above,
  since there is no install prefix to read them from.

## Licence

Armagetron Advanced is GPLv2, and so is this port. See [COPYING.txt](COPYING.txt).
The vendored dependencies under `platform/3ds/vendor` keep their own licences.
