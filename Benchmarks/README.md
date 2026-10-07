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

## Reference results (2026-10-07, M4 Max, 2048x2048)

Wall ms per frame, fit view, best median of 3 rounds, from commit 0dd4d203 plus the anti-aliased clip mask. The shaders were compiled by `bench.sh`.

| File | Draws | Rasterizer | Vello | Vello retained | Skia Ganesh | Ganesh MSAA4 | Skia Graphite |
|---|---:|---:|---:|---:|---:|---:|---:|
| tiger | 301 | 1.02 | 0.81 | 0.79 | 1.45 | 0.63 | 0.43 |
| hawaii | 1,137 | 1.42 | 1.20 | 1.19 | 1.63 | 1.34 | 1.21 |
| london-rail | 10,645 | 0.98 | 2.62 | 1.69 | 8.01 | 6.42 | 7.36 |
| Paris 30K | 50,791 | 1.99 | 12.10 | 7.93 | 56.48 | 30.99 | 30.18 |
| contour | 53,237 | 1.50 | 4.15 | 1.48 | 35.55 | 18.27 | 18.61 |
| spoorkaart | 84,239 | 3.87 | 21.84 | 14.18 | 238.09 | 69.73 | 57.60 |
| designcloud | 86,727 | 2.80 | 12.84 | 5.42 | 46.92 | 46.10 | 55.84 |
| 6000-DRG | 410,998 | 13.32 | 84.08 | 65.85 | 197.48 | 151.40 | 191.90 |
| pathological | 579,735 | 9.40 | 56.21 | 31.56 | 301.06 | 225.44 | 226.67 |

Geometric mean wall vs Rasterizer, over all 15 TestFiles:

| View | Vello | Vello retained | Skia Ganesh | Ganesh MSAA4 | Skia Graphite |
|---|---:|---:|---:|---:|---:|
| fit | 2.28x | 1.53x | 6.72x | 4.95x | 4.68x |
| sweep | 1.86x | 1.53x | 16.9x | 3.44x | 3.09x |

- Scenes under about 1k draws are within noise.
- Separate runs vary by up to about ±10% per library, e.g. Skia Graphite's fit mean was 5.13x in an earlier run that day. Compare builds with interleaved runs, e.g. `--rev`.
- Skia's frames are dominated by single-threaded CPU recording, at about 0.4–0.5 µs per draw.
- Ganesh without MSAA falls back to CPU masks for large anti-aliased paths, so its sweep degrades badly, e.g. spoorkaart at 1.1 s.
