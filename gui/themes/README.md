# SVG palette experiment

This study lives on `experiment/svg-themes`, based on draft PR #77's `5b51152`.
The theme changes have not been added to that PR. Palettes were sampled from the
user's six-panel reference; the original silver is an exact rendering baseline.

| Theme | Surface color |
| --- | --- |
| Slate | `#747B7B` |
| Silver | `#A7A7A7` |
| Yellow | `#ADB575` |
| Green | `#699757` |
| Purple | `#6F3B99` |
| Orange | `#9A6429` |

Run from the repository root with SVG support enabled:

```sh
./build/op1emu nand.bin --ui=svg --floating-ui --theme=green
./build/op1-ui-preview --floating --theme=green gui/ui-svg.json
```

The second command uses a synthetic four-color framebuffer and needs no firmware.
Right-click the SVG window to open the six-theme menu; macOS also accepts
Control-click. Each item has a color swatch and the active preset has a checkmark.
Click to select, or use Up/Down and Enter. Escape, an outside click, or focus loss
dismisses the menu. In floating mode Escape dismisses an open menu first, then a
subsequent Escape restores the title bar. The selection lasts for the current
session; there is no saved preference yet.

Omitting `--theme` uses the original SVG. A bad/missing startup theme
reports its error and recovers to original SVG colors. The PNG panel reports
that themes are unavailable and retains its original colors.

Copy a palette JSON and pass its path with `--theme path/to/custom.json`. Version
1 requires four `#RRGGBB` colors: `body`, `panel`, `keycap`, and `encoder-ring`.
The presets assign the same base color to each surface, matching the reference,
but custom files can set each independently. Colored encoder caps, the volume
knob, black keys, labels and the live firmware display keep their existing paint.

`gui/op1.svg` remains byte-for-byte unchanged. `gui/op1-themeable.svg` is a
class-only derivative: one body, 60 panels, 54 keycaps/rims and four encoder rings.
resvg 0.49.0 applies the palette through its stylesheet option before parsing.
The parsed tree and physical-resolution texture use the existing cache. There
is no per-frame recoloring. Runtime selection renders a replacement tree/texture
on a worker while the previous background remains visible. The main thread swaps
it in when ready, reusing the window and background GL texture; firmware pixels,
input mappings and audio are not restarted. A failed switch keeps the current
theme, and rapid selections use the latest requested palette. The menu stays
inside the window and its text raster follows physical framebuffer density.
Geometry, screen coordinates, controls, gradients
and all 217 filters are shared with the original artwork.

Regenerate or audit the derivative (Python standard library only):

```sh
python3 tools/prepare_theme_svg.py
python3 tools/prepare_theme_svg.py --check
```

The generator checks the original artwork's SHA-256 and paint counts. A new
source export requires reviewing the assignments before updating that guard.

Render the local comparison with Pillow installed in the selected Python:

```sh
cmake --build build --target op1-svg-render
python3 tools/compare_svg_themes.py
```

Outputs go to `build/visual-comparisons/themes/`: six transparent PNGs at each of
1x and 2x, and `six-themes-comparison.png`. The sheet uses actual resvg renders
with blank screens; it contains no firmware image. Pillow is needed only to
assemble this review sheet.

Regression coverage checks palette validation, independent surface assignments,
pixel-identical silver/unthemed derivative at 1x and 2x, identical alpha across
all six themes, unchanged screen and encoder-cap pixels, and dark black-key
centers. The OpenGL harness clicks all six popup items, checks the active row,
keyboard/outside/focus dismissal, prevention of accidental instrument input,
fresh RGB565 uploads during worker jobs, texture/window reuse, rapid selections
and recovery from a missing palette. Original PNG/build-disabled checks remain
available without Rust.

Local macOS arm64 validation: 24/24 SVG-build CTests passed (the loopback socket
test was rerun with network access); all four PNG-only UI/theme CTests passed.
Both builds passed normal and floating/fallback OpenGL harnesses. The SVG runs
started in Purple/Green and verified all six palettes with the diagnostic screen.

The menu update also passed the four targeted raster/theme/layout CTests and the
normal/floating OpenGL harnesses on local macOS, including the PNG-only build's
normal and floating-request fallback checks.

A live firmware-246 session also accepted Yellow, Slate, Orange, Purple and
Silver selections without restarting the emulator process. Native screenshots
confirmed the background changed while the live Tape screen stayed in place.
For a reproducible firmware-free popup image, set `OP1_UI_MENU_CAPTURE` to a PNG
path when running `op1-ui-preview --test` (the destination directory must exist).
