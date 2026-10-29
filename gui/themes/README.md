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
This prototype selects themes at launch; it does not yet provide a theme menu or
saved preference. Omitting `--theme` uses the original SVG. A bad/missing theme
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
is no per-frame recoloring. Geometry, screen coordinates, controls, gradients
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
centers. The OpenGL harness checks the RGB565 screen overlay after each theme
and recovery from a missing palette. Original PNG/build-disabled checks remain
available without Rust.

Local macOS arm64 validation: 24/24 SVG-build CTests passed (the loopback socket
test was rerun with network access); all four PNG-only UI/theme CTests passed.
Both builds passed normal and floating/fallback OpenGL harnesses. The SVG runs
started in Purple/Green and verified all six palettes with the diagnostic screen.
