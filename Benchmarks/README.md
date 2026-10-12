# Benchmarks: Rasterizer vs Vello & Skia on TestFiles

```bash
Benchmarks/bench.sh                                  # 3 rounds at 2048x2048, all configs
Benchmarks/bench.sh --rounds 1 --size 4096x4096 --configs rasterizer,vello,skia_graphite
Benchmarks/bench.sh --rev <commit>                   # benchmark another commit's sources, TestFiles & shaders
Benchmarks/bench.sh --configs rasterizer --contexts 1  # cap Rasterizer at one context, a baseline for its slicing
```

`bench.sh` is self-contained. It does all of the following under `Benchmarks/build/`, which ignores itself:
- writes the harness sources
- fetches & patches `vello_encoding`
- installs the pinned Rust toolchain
- builds Rasterizer's harness, Vello & Skia
- compiles the shaders from the checkout
- exports the TestFiles
- runs the quality pass & interleaved timing rounds

It writes `results/<stamp>-<rev>-<WxH>/summary.md`, alongside `info.txt`, which records the machine, toolchain & crate versions.
Its header comment covers the requirements & method.

Configs:
- `rasterizer`: the Metal renderer.
- `vello` and `vello_retained`: Vello 0.11, rebuilding the scene each frame, or appending one built scene with each frame's view.
- `skia_ganesh`: Skia's Ganesh backend.
- `skia_graphite`: Skia's Graphite backend.

## Mapping caveats

The replays draw Rasterizer's parsed draws, so all libraries render the same geometry. Where semantics differ:
- Stroke widths: Rasterizer scales positive widths by sqrt|det|, the others by the full affine. These are the same for the uniform scales used here.
  - Negative widths are device pixels, recomputed every frame. Skia draws -1 as a native hairline.
  - The retained modes keep the fit view's widths.
- Miter joins: Rasterizer caps both segments at a turn of more than 150 degrees. The others use a miter limit of 3.864.
- Clip paths: all are anti-aliased & even-odd, applied once per run of draws with the same clip. Rasterizer uses a coverage mask; commits before it used an aliased stencil, & `--rev` benchmarks them that way.
- Gradients & images: these are exported as Rasterizer stores them. Each library interpolates & samples in its own way.

## Reference results (2026-10-11, M4 Max, 2048x2048)

Wall ms per frame, fit view, best median of 3 rounds, from commit 91497828. The shaders were compiled by `bench.sh`.

| File | Draws | Rasterizer | Vello | Vello retained | Skia Ganesh | Skia Graphite |
|---|---:|---:|---:|---:|---:|---:|
| tiger | 301 | 0.45 | 0.83 | 0.89 | 1.36 | 0.44 |
| hawaii | 1,137 | 1.07 | 1.21 | 1.10 | 1.48 | 1.19 |
| london-rail | 10,643 | 0.58 | 1.96 | 1.14 | 7.52 | 7.66 |
| Paris 30K | 50,791 | 1.52 | 11.76 | 7.67 | 54.53 | 28.41 |
| contour | 53,237 | 1.08 | 4.08 | 1.48 | 28.57 | 18.85 |
| spoorkaart | 84,287 | 3.34 | 21.46 | 14.08 | 218.79 | 56.44 |
| designcloud | 86,728 | 2.33 | 11.98 | 5.90 | 43.45 | 55.23 |
| 6000-DRG | 414,657 | 13.06 | 109.14 | 89.55 | 214.25 | 196.62 |
| pathological | 580,473 | 7.14 | 56.90 | 32.32 | 291.77 | 224.51 |

Geometric mean wall vs Rasterizer, over all 20 TestFiles:

| View | Vello | Vello retained | Skia Ganesh | Skia Graphite |
|---|---:|---:|---:|---:|
| fit | 3.42x | 2.60x | 8.63x | 4.83x |
| sweep | 2.36x | 2.26x | 18.6x | 2.90x |

- Scenes under about 1k draws are within noise. Rasterizer now uses fewer contexts for them, & coalesces the contexts' edges passes, so it's level with or faster than the others on all of them.
- 5 of the 20 TestFiles were added since the 2026-10-07 results. Over the original 15, the fit means are 3.14x, 2.16x, 8.79x & 6.57x, against the earlier 2.28x, 1.53x, 6.72x & 4.68x.
- 6000-DRG now draws its zero-length round-capped dots, which the PDF parser dropped before. Its draws went from 410,998 to 414,657, as runs of dots with the same state are one draw.
- Separate runs vary by up to about ±10% per library. Compare builds with interleaved runs, e.g. `--rev`.
- Rasterizer's frames are short, so other GPU work on the machine, such as a busy app UI, can slow them 2–3x. Run on AC power with other GPU apps closed, & check `gpu_min` against `gpu_med` across rounds in `runs.jsonl`.
- Skia's frames are dominated by single-threaded CPU recording, at about 0.4–0.5 µs per draw.
- Ganesh without MSAA falls back to CPU masks for large anti-aliased paths, so its sweep degrades badly, e.g. spoorkaart at 1.1 s.
