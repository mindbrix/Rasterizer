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

## Reference results (2026-10-15, MacBook Air M4, 2048x2048)

Wall ms per frame, fit view, best median of 3 rounds, from commit 5c0faffc. The shaders were compiled by `bench.sh`.

| File | Draws | Rasterizer | Vello | Vello retained | Skia Ganesh | Skia Graphite |
|---|---:|---:|---:|---:|---:|---:|
| tiger | 301 | 1.35 | 1.43 | 1.42 | 3.56 | 0.86 |
| hawaii | 1,137 | 2.81 | 2.34 | 2.11 | 3.58 | 2.40 |
| london-rail | 10,643 | 1.76 | 3.90 | 2.86 | 8.36 | 7.83 |
| Paris 30K | 50,791 | 4.94 | 28.09 | 24.57 | 66.08 | 34.58 |
| contour | 53,237 | 3.32 | 6.49 | 3.99 | 30.82 | 19.88 |
| spoorkaart | 84,287 | 9.87 | 44.34 | 37.18 | 327.85 | 66.27 |
| designcloud | 86,728 | 8.02 | 21.16 | 15.10 | 49.88 | 68.87 |
| 6000-DRG | 414,657 | 20.04 | 129.98 | 109.58 | 233.40 | 224.56 |
| pathological | 580,473 | 18.14 | 106.74 | 81.97 | 324.32 | 267.01 |

Geometric mean wall vs Rasterizer, over all 20 TestFiles:

| View | Vello | Vello retained | Skia Ganesh | Skia Graphite |
|---|---:|---:|---:|---:|
| fit | 2.16x | 1.86x | 4.97x | 2.44x |
| sweep | 1.70x | 1.93x | 10.6x | 1.95x |

- The MacBook Air is the reference because it's a more typical machine than an M4 Max. Rasterizer does most of its work on the GPU, so its lead depends on the GPU's size: on the M4 Max, at commit 91497828, the fit means were 3.42x, 2.60x, 8.63x & 4.83x. The other libraries are mostly limited by the CPU, & were only 1.4–1.7x slower on the Air.
- Rasterizer is fastest on 13 of the 20 TestFiles in the fit view. Skia Graphite is faster on the 5 smallest that aren't reschart, e.g. tiger at 0.86 ms against 1.35 ms, Vello retained on hawaii, & Skia Ganesh on Manchester_Union_Democrat. Rasterizer takes under 5 ms on all of them.
- Separate runs vary by up to about ±10% per library. Compare builds with interleaved runs, e.g. `--rev`.
- Rasterizer's frames are short, so other GPU work on the machine, such as a busy app UI, can slow them 2–3x. Run on AC power with other GPU apps closed, & check `gpu_min` against `gpu_med` across rounds in `runs.jsonl`.
- Skia's frames are dominated by single-threaded CPU recording, at about 0.3–0.6 µs per draw for Graphite on the larger files.
- Ganesh without MSAA falls back to CPU masks for large anti-aliased paths, so its sweep degrades badly, e.g. spoorkaart at 2.6 s.
