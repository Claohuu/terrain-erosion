# Terrain Erosion

A hydraulic erosion simulator written in C++, compiled to WebAssembly, driven
by a React control panel. Fractal noise produces a heightmap; hundreds of
thousands of simulated water droplets then carve it into terrain with real
drainage networks.

Nothing about the rivers is authored. Droplets pick up sediment on steep
ground, carry it downhill, and drop it where the flow slows. Later droplets
fall into the channels earlier ones cut, and valleys emerge from that feedback.

**[Live demo](https://claohuu.github.io/terrain-erosion/)**

```
sliders  →  generate()  →  erode()  →  hillshade  →  canvas
              C++/WASM     C++/WASM      JS
```

## Running it

Requires [Emscripten](https://emscripten.org/) for the C++ build and Node for
the frontend.

```bash
./build.sh          # compiles cpp/terrain.cpp to web/src/wasm/terrain.js
cd web
npm install
npm run dev
```

Then open http://localhost:5173/terrain-erosion/ (the path prefix matches how
GitHub Pages serves it).

`build.sh` must be re-run after any C++ change — the JavaScript hot-reloads,
the WebAssembly does not.

## How it works

### Generation

**Value noise.** Random values are placed only at the corners of a coarse
lattice, and any point in between is bilinearly interpolated from the four
surrounding corners. Random-per-pixel would be static, with no relationship
between neighbours; interpolating a sparse lattice is what makes nearby points
similar, which is what reads as landscape.

The corner values come from a hash of the coordinates rather than a random
stream. Every pixel inside a cell asks for the same four corners, so the
function has to be repeatable — `rand()` would give a different answer each
call and produce noise, not noise *functions*.

Interpolation weights run through a smoothstep curve. With raw linear weights
the slope changes abruptly at every cell boundary, leaving a visible diamond
grid across the output.

**Fractal octaves.** One layer of value noise is smooth blobs. Summing several
layers, each at double the frequency and half the amplitude, adds detail at
every scale — large landforms with fine roughness riding on top.

### Erosion

Each droplet spawns at a random point carrying water, speed, and no sediment,
then for up to 30 steps:

1. Samples the height and gradient beneath it, bilinearly
2. Steers by blending its existing direction with the downhill gradient — the
   `inertia` parameter. Pure gradient-following gives jittery paths that never
   cut across terrain; pure inertia ignores the landscape entirely
3. Computes its **sediment capacity**, which scales with speed, water, and how
   steeply it just descended
4. Carrying more than capacity, or moving uphill, it **deposits**. Carrying
   less, it **erodes**
5. Converts height lost into speed, and loses a little water to evaporation

Deposition is bilinear onto the four cells under the droplet — silt settles
where the water is. Erosion is spread over a distance-weighted circular brush,
because removing height from a single cell leaves pixel-wide scratches rather
than sloped valley walls.

The simulation is fully deterministic: the same seed always produces the same
terrain, which makes every result reproducible and every bug repeatable.

## Testing

```bash
./run_tests.sh
```

Builds the simulation natively with `g++` and runs 27 checks — no Emscripten,
no browser. The C++ is plain portable code; only the exported entry points care
about WebAssembly, and those are guarded by `#ifdef __EMSCRIPTEN__`.

What it covers:

- **Determinism.** Same seed produces a byte-identical heightmap, before and
  after erosion. This is the property the whole debugging story rests on.
- **Numerical sanity.** No NaN or infinity anywhere, including on perfectly
  flat terrain where the slope is zero and capacity maths could divide by
  nothing.
- **Bounds safety.** The heightmap is allocated with sentinel guard bands on
  either side, and erosion is run at every brush radius from 1 to 8. Any write
  past the logical buffer trips a guard. This is the test for the optimization
  that removed per-cell bounds checking — the spawn margin has to be provably
  wide enough, not just wide enough in practice.
- **Mass accounting.** Total height is summed before and after. Erosion must
  never *increase* it, since that would mean sediment appearing from nowhere.
  The measured loss is **3.7%**, which is sediment carried off the map edge by
  droplets that ran out of bounds — the known simplification, now quantified.
- **Degenerate inputs.** A map smaller than the brush, zero droplets, and
  perfectly flat terrain.

CI runs the suite on Linux, where AddressSanitizer and UBSan are available, and
a failure blocks the deploy. `run_tests.sh` detects sanitizer support and falls
back to the guard-band checks on toolchains without it (MinGW does not ship the
runtime).

## Performance

512×512 heightmap, 150,000 droplets, brush radius 3. Each figure is the median
of 7 runs, with the observed spread — a single timing moves by tens of percent
with GC and CPU frequency scaling, so one-shot numbers are not measurements.

| Version | Median | Spread | Droplets/sec |
|---|---|---|---|
| Baseline (`-O2`) | 491ms | 467–569ms | 305,748 |
| Height-only sampler | 473ms | 447–551ms | 316,924 |
| Flat brush offsets, no bounds check | 421ms | 368–435ms | 355,956 |
| `-O3` | **370ms** | 359–391ms | **405,625** |

**Net: 25% faster, 33% more throughput.**

Notes on each step:

- **Height-only sampler.** The droplet loop needs the full gradient where it
  currently is, but only the height where it moved to. Splitting the function
  so the second call skips the gradient produced a change inside the noise
  band — the compiler was already inlining and eliminating the dead
  computation. Recorded here because a non-result is still a result.

- **Flat brush offsets.** The innermost loop ran roughly 130 million times
  (150k droplets × 30 steps × ~29 brush cells), and each iteration did two
  multiplies to compute an index plus four comparisons to bounds-check it.
  Storing each brush cell as a single precomputed flat offset removed the
  arithmetic. Confining droplet spawns to an interior margin wide enough that
  the brush can never leave the map removed the bounds check entirely.

- **`-O3`.** Free, and it also tightened the variance noticeably.

The first call into WebAssembly is roughly 2.5× slower than steady state, since
it pays module compilation. This is visible as the gap between the single-run
readout and the benchmark median, and is the reason the benchmark discards
nothing but still reports a median rather than a first sample.

## Scaling

```bash
./run_bench.sh
```

Native benchmark (`-O3`, same flags as the WebAssembly build) sweeping
resolution, droplet count, and brush radius.

**Resolution** — 100,000 droplets, radius 3:

| Size | Heightmap | Median | Droplets/sec | vs 256 |
|---|---|---|---|---|
| 256x256 | 0.2 MB | 229ms | 435,739 | 100% |
| 512x512 | 1.0 MB | 231ms | 432,963 | 99% |
| 1024x1024 | 4.0 MB | 241ms | 415,696 | 95% |
| 2048x2048 | 16.0 MB | 255ms | 392,973 | 90% |

Throughput is nearly flat. Droplets wander, so access is spatially scattered
and a 16 MB heightmap cannot stay resident in cache — but each droplet does
enough arithmetic per step that the misses are largely hidden. A 10% cost for
64x the map is cheaper than expected.

**Droplet count** — 512x512, radius 3:

| Droplets | Median | Droplets/sec | ms per 10k |
|---|---|---|---|
| 25,000 | 50ms | 500,507 | 19.98 |
| 50,000 | 106ms | 471,845 | 21.19 |
| 100,000 | 201ms | 498,551 | 20.06 |
| 200,000 | 384ms | 521,203 | 19.19 |
| 400,000 | 783ms | 510,820 | 19.58 |

Cost per 10,000 droplets stays within a few percent across a 16x range, so the
simulation is linear in droplet count. Droplets do not interact, so this is
what should happen — worth confirming rather than assuming.

**Brush radius** — 512x512, 100,000 droplets:

| Radius | Brush cells | Median | Droplets/sec |
|---|---|---|---|
| 1 | 5 | 160ms | 625,419 |
| 2 | 13 | 171ms | 586,030 |
| 3 | 29 | 197ms | 507,349 |
| 4 | 49 | 231ms | 433,291 |
| 5 | 81 | 328ms | 305,093 |
| 6 | 113 | 348ms | 287,186 |

The interesting one. Brush cells grow **22.6x** from radius 1 to 6, but runtime
only grows **2.2x**. The inner brush loop is therefore *not* the dominant cost —
per-step work (gradient sampling, steering, the square root) is. Optimizing the
brush still bought 11%, but this says where the next 11% would have to come
from, and it is not there.

### Measurement methodology

The first version of this benchmark reported that throughput **halved** at
2048x2048, and the obvious explanation was a cache cliff — 16 MB does not fit
in L2 or L3, droplets access memory unpredictably, so misses dominate. It was a
tidy story.

It was also wrong. The same configuration measured in three different sweeps
gave 112ms, 218ms, and 175ms — a 2x spread on identical work. The resolution
sweep ran first, on a boosted clock; by the later sweeps the CPU had settled to
its sustained frequency. The 2048x2048 case looked slow because it was measured
last, not because it was large.

Median-of-N does not catch this, because every sample within a group sits at
the same point on the thermal curve.

The benchmark now:

1. Runs the simulation for 8 seconds before recording anything, to reach a
   steady clock
2. Measures one fixed control configuration at the start and again at the end,
   and prints the drift between them
3. Prints a warning if that drift exceeds 5%

With warm-up in place the control agrees to within **0.5%** across the run, and
the real degradation at 2048x2048 is 10% rather than 50%.

## Deliberately out of scope

- **Multithreading.** Droplets are independent until they write, so this
  parallelizes well in principle, but WASM threads require SharedArrayBuffer
  and cross-origin isolation headers — substantial tooling for a static demo.
- **Sediment conservation.** Droplets that run off the edge of the map take
  their sediment with them, so total mass is not conserved. Correct would be
  depositing it at the boundary.
- **GPU.** A compute shader would beat this comfortably. The point here was
  CPU-side simulation performance.

## Deployment

Pushing to `main` triggers `.github/workflows/deploy.yml`, which installs
Emscripten, compiles the C++, builds the frontend, and publishes to GitHub
Pages. The WebAssembly is compiled in CI rather than committed, so the
repository holds no build artifacts.

Because Pages serves a project repo from `/<repo-name>/`, Vite's `base` is set
to `/terrain-erosion/` unconditionally — including in dev and preview, so all
three environments resolve assets identically.

## Layout

```
cpp/terrain.cpp                 noise, erosion, and the exported entry points
cpp/test_terrain.cpp            native test suite
cpp/bench_terrain.cpp           native scaling benchmark
build.sh                        emcc invocation
run_tests.sh                    native test build and run
run_bench.sh                    native benchmark build and run
web/src/App.jsx                 React UI, hillshading, benchmark harness
web/src/index.css               styles
.github/workflows/deploy.yml    build and publish to Pages
```

The C++ exposes two functions. `generate` allocates a heightmap and returns a
pointer; `erode` modifies one in place. The pointer is a byte offset into
WebAssembly's linear memory, and JavaScript reads the same bytes through a
`Float32Array` view — no copying across the boundary.
