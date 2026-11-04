# Benchmarks: Rasterizer vs Vello, Skia & Rive on TestFiles

```bash
Benchmarks/bench.sh                                  # 3 rounds at 2048x2048, all configs
Benchmarks/bench.sh --rounds 1 --size 4096x4096 --configs rasterizer,vello,skia_ganesh
Benchmarks/bench.sh --rev <commit>                   # benchmark another commit's sources, TestFiles & shaders
Benchmarks/bench.sh --configs rasterizer --contexts 1  # cap Rasterizer at one context, a baseline for its slicing
Benchmarks/bench.sh --viewer TestFiles/pathological.pdf  # the interactive viewer, below
```

`bench.sh` is self-contained. It does all of the following under `Benchmarks/build/`, which ignores itself:
- writes the harness sources
- fetches & patches `vello_encoding`, & sparse-clones `rive-runtime`
- installs the pinned Rust toolchain
- builds Rasterizer's harness, Vello, Skia & Rive
- compiles the shaders from the checkout
- exports the TestFiles
- runs the quality pass & interleaved timing rounds

It writes `results/<stamp>-<rev>-<WxH>/summary.md`, alongside `info.txt`, which records the machine, toolchain & crate versions.
Its header comment covers the requirements & method.

Configs:
- `rasterizer`: the Metal renderer.
- `vello` and `vello_retained`: Vello 0.11, rebuilding the scene each frame, or appending one built scene with each frame's view.
- `skia_ganesh`: Skia's Ganesh backend.
- `rive`: the Rive renderer, rive-runtime `runtime-v0.1.698` on Metal, in the mode it picks, which is raster ordering on Apple GPUs.
  Its shaders are compiled synchronously & its dithering is off. `rivebench --atomics` or `--msaa N` tries its other modes.

Skia's Graphite backend isn't benchmarked. It renders with internal 4x MSAA & depth-tests every draw to skip hidden pixels, so it isn't comparable with the analytic anti-aliasing of the others.

## Viewer

`bench.sh --viewer [file]` builds `raviewer`, then opens the file in it. The file can be an RSD dump, or an SVG or PDF, which is
exported on the fly. With no file, it opens `TestFiles/tiger.svg`.

The viewer draws the same view with any of five renderers:
- Rasterizer, with rabench's renderer, so its timings match the benchmark's
- CoreGraphics
- Rive, Skia Ganesh & Vello, with their benchmark replays

Controls:
- Trackpad: two-finger scrolling pans. Pinching zooms & rotating rotates, around the pointer.
- Mouse: dragging pans, & the wheel zooms.
- Keys:
  - `1`–`5` or tab picks the renderer.
  - `f` fits the view, as does a double click or a two-finger double tap.
  - `c` toggles continuous redraw, for timing.
  - Left & right step through the file's directory.
  - ⌘O opens a file, as does dropping one on the window.

The title shows the renderer's median frame times over its last 30 frames. As in the benchmark, each frame waits for the GPU,
so it shows CPU time to submit, submit to GPU done, & Metal GPU time where there is one. CoreGraphics only has CPU time.

`raviewer <metallib> <file> --snapshot <dir>` draws the fit view with each renderer in turn, then quits. It writes
`<dir>/<file>_<renderer>.png` & prints each title line. Vello draws to its window only while the window is visible.

## Mapping caveats

The replays draw Rasterizer's parsed draws, so all libraries render the same geometry. Where semantics differ:
- Stroke widths: Rasterizer scales positive widths by sqrt|det|, the others by the full affine. These are the same for the uniform scales used here.
  - Negative widths are device pixels, recomputed every frame. Skia draws -1 as a native hairline.
  - Rive widens strokes thinner than its anti-aliasing ramp & fades them, so sub-pixel detail, like pathological's dots, is heavier.
  - The retained modes keep the fit view's widths.
- Miter joins: Rasterizer caps both segments at a turn of more than 150 degrees. Vello & Skia use a miter limit of 3.864, & Rive's
  is fixed at 4.
- Clip paths: all are anti-aliased & even-odd, applied once per run of draws with the same clip. Rasterizer uses a coverage mask; commits before it used an aliased stencil, & `--rev` benchmarks them that way.
- Gradients & images: these are exported as Rasterizer stores them. Each library interpolates & samples in its own way.
- Quadratic curves: Rive draws only cubics, so rivebench elevates quads to cubics.

## Reference results (2026-10-15, MacBook Air M4, 2048x2048)

Wall ms per frame, fit view, best median of 3 rounds, from commit 5c0faffc. The shaders were compiled by `bench.sh`.

| File | Draws | Rasterizer | Vello | Vello retained | Skia Ganesh |
|---|---:|---:|---:|---:|---:|
| tiger | 301 | 1.35 | 1.43 | 1.42 | 3.56 |
| hawaii | 1,137 | 2.81 | 2.34 | 2.11 | 3.58 |
| london-rail | 10,643 | 1.76 | 3.90 | 2.86 | 8.36 |
| Paris 30K | 50,791 | 4.94 | 28.09 | 24.57 | 66.08 |
| contour | 53,237 | 3.32 | 6.49 | 3.99 | 30.82 |
| spoorkaart | 84,287 | 9.87 | 44.34 | 37.18 | 327.85 |
| designcloud | 86,728 | 8.02 | 21.16 | 15.10 | 49.88 |
| 6000-DRG | 414,657 | 20.04 | 129.98 | 109.58 | 233.40 |
| pathological | 580,473 | 18.14 | 106.74 | 81.97 | 324.32 |

Geometric mean wall vs Rasterizer, over all 20 TestFiles:

| View | Vello | Vello retained | Skia Ganesh |
|---|---:|---:|---:|
| fit | 2.16x | 1.86x | 4.97x |
| sweep | 1.70x | 1.93x | 10.6x |

- The MacBook Air is the reference because it's a more typical machine than an M4 Max. Rasterizer does most of its work on the GPU, so its lead depends on the GPU's size: on the M4 Max, at commit 91497828, the fit means were 3.42x, 2.60x & 8.63x. The other libraries are mostly limited by the CPU, & were only 1.4–1.7x slower on the Air.
- In the fit view, Rasterizer is slower than another library on at most 6 of the 20 TestFiles, all small, e.g. Vello retained on hawaii & Skia Ganesh on Manchester_Union_Democrat. Rasterizer takes under 5 ms on all of them.
- Separate runs vary by up to about ±10% per library. Compare builds with interleaved runs, e.g. `--rev`.
- Rasterizer's frames are short, so other GPU work on the machine, such as a busy app UI, can slow them 2–3x. Run on AC power with other GPU apps closed, & check `gpu_min` against `gpu_med` across rounds in `runs.jsonl`.
- Skia's frames are dominated by single-threaded CPU recording.
- Ganesh without MSAA falls back to CPU masks for large anti-aliased paths, so its sweep degrades badly, e.g. spoorkaart at 2.6 s.
