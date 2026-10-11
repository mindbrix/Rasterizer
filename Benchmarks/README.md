# Benchmarks: Rasterizer vs Vello & Skia on TestFiles

```bash
Benchmarks/bench.sh                                  # 3 rounds at 2048x2048, all configs
Benchmarks/bench.sh --rounds 1 --size 4096x4096 --configs rasterizer,vello,skia_graphite
Benchmarks/bench.sh --rev <commit>                   # benchmark another commit's sources, TestFiles & shaders
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
- `skia_ganesh` and `skia_ganesh_msaa4`: Skia's Ganesh backend, with 1 or 4 samples.
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

Wall ms per frame, fit view, best median of 3 rounds, from commit 4f1bc7b3. The shaders were compiled by `bench.sh`.

| File | Draws | Rasterizer | Vello | Vello retained | Skia Ganesh | Ganesh MSAA4 | Skia Graphite |
|---|---:|---:|---:|---:|---:|---:|---:|
| tiger | 301 | 0.93 | 0.89 | 0.83 | 1.40 | 0.64 | 0.47 |
| hawaii | 1,137 | 1.51 | 1.23 | 1.08 | 1.47 | 1.29 | 1.19 |
| london-rail | 10,643 | 1.02 | 1.90 | 1.20 | 6.82 | 5.78 | 7.19 |
| Paris 30K | 50,791 | 1.95 | 11.78 | 7.64 | 52.40 | 29.50 | 29.40 |
| contour | 53,237 | 1.51 | 3.96 | 1.48 | 28.64 | 17.32 | 18.79 |
| spoorkaart | 84,287 | 4.21 | 21.33 | 14.01 | 224.94 | 67.24 | 58.34 |
| designcloud | 86,728 | 2.77 | 11.93 | 5.82 | 43.22 | 41.07 | 54.25 |
| 6000-DRG | 414,657 | 15.48 | 108.84 | 89.10 | 216.60 | 156.43 | 200.62 |
| pathological | 580,473 | 9.00 | 56.86 | 32.18 | 296.09 | 221.57 | 226.48 |

Geometric mean wall vs Rasterizer, over all 23 TestFiles:

| View | Vello | Vello retained | Skia Ganesh | Ganesh MSAA4 | Skia Graphite |
|---|---:|---:|---:|---:|---:|
| fit | 2.16x | 1.62x | 4.89x | 3.70x | 2.85x |
| sweep | 1.80x | 1.75x | 11.1x | 2.75x | 1.92x |

- Scenes under about 1k draws are within noise. 8 of the 23 TestFiles were added since the 2026-10-07 results. 4 have under 500 draws, & Skia is fastest on all of them, which lowers the means. Over the original 15, the fit means are 2.25x, 1.52x, 6.29x, 4.56x & 4.66x, close to the earlier 2.28x, 1.53x, 6.72x, 4.95x & 4.68x.
- 6000-DRG now draws its zero-length round-capped dots, which the PDF parser dropped before. Its draws went from 410,998 to 414,657, as runs of dots with the same state are one draw.
- Separate runs vary by up to about ±10% per library. Compare builds with interleaved runs, e.g. `--rev`.
- Rasterizer's frames are short, so other GPU work on the machine, such as a busy app UI, can slow them 2–3x. Run on AC power with other GPU apps closed, & check `gpu_min` against `gpu_med` across rounds in `runs.jsonl`.
- Skia's frames are dominated by single-threaded CPU recording, at about 0.4–0.5 µs per draw.
- Ganesh without MSAA falls back to CPU masks for large anti-aliased paths, so its sweep degrades badly, e.g. spoorkaart at 1.1 s.
