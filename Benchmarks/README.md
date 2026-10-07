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
- Clip paths: Rasterizer uses an aliased even-odd stencil. Vello & Skia clip anti-aliased, once per run of draws with the same clip.
- Gradients & images: these are exported as Rasterizer stores them. Each library interpolates & samples in its own way.

## Reference results (2026-10-07, M4 Max, 2048x2048)

Wall ms per frame, fit view, best median of 3 rounds:

| File | Draws | Rasterizer | Vello | Vello retained | Skia Ganesh | Ganesh MSAA4 | Skia Graphite |
|---|---:|---:|---:|---:|---:|---:|---:|
| tiger | 301 | 0.97 | 0.82 | 0.78 | 1.38 | 0.63 | 1.05 |
| hawaii | 1,137 | 1.41 | 1.19 | 1.08 | 1.56 | 1.50 | 1.19 |
| london-rail | 10,645 | 1.05 | 2.54 | 1.67 | 7.98 | 6.36 | 7.84 |
| Paris 30K | 50,791 | 2.11 | 12.47 | 8.00 | 58.28 | 30.99 | 29.19 |
| contour | 53,237 | 1.55 | 4.08 | 1.47 | 29.52 | 18.05 | 18.85 |
| spoorkaart | 84,239 | 3.92 | 21.60 | 14.00 | 250.47 | 74.92 | 60.83 |
| designcloud | 86,727 | 2.79 | 12.01 | 5.44 | 46.51 | 52.15 | 59.46 |
| 6000-DRG | 410,998 | 14.27 | 83.75 | 65.87 | 208.16 | 155.55 | 198.29 |
| pathological | 579,735 | 10.03 | 55.78 | 31.11 | 308.47 | 236.46 | 230.34 |

Geometric mean wall vs Rasterizer:

| View | Vello | Vello retained | Skia Ganesh | Ganesh MSAA4 | Skia Graphite |
|---|---:|---:|---:|---:|---:|
| fit | 2.32x | 1.46x | 6.65x | 4.82x | 5.13x |
| sweep | 1.80x | 1.53x | 17.3x | 3.42x | 3.08x |

- Scenes under about 1k draws are within noise.
- Skia's frames are dominated by single-threaded CPU recording, at about 0.4–0.5 µs per draw.
- Ganesh without MSAA falls back to CPU masks for large anti-aliased paths, so its sweep degrades badly, e.g. spoorkaart at 1.1 s.

These numbers used the Oct 5 DerivedData Release metallib, from before `bench.sh` compiled its own.
