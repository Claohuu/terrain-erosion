# Engineering notes

Detail that would otherwise bury the [README](README.md): how the simulation is
tested, what was optimized and by how much, how it scales, and why the
benchmark needed a warm-up phase before any of its numbers meant anything.

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

- **Per-step work** (found by the scaling study below, not by guessing).
  Three changes to the droplet loop, together worth about **13%**:

  - The height sampled at the end of a step is the height the *next* step
    starts from, and it was being recomputed. Carrying it forward removes one
    bilinear blend per step. Results stay bit-identical, which the determinism
    test verifies.
  - Normalising the direction vector used two divides. One reciprocal and two
    multiplies is cheaper, since division has several times the latency.
  - `1 - inertia` was recomputed inside a loop running roughly four million
    times. Hoisted out.

  Measured control-to-control: 212ms before, ~185ms after.

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
brush still bought 11%, but this said where the next win had to come from.

Acting on it produced the per-step optimizations listed above, worth another
13%. Without this sweep the obvious next move would have been to optimize the
brush further, which the data says would have achieved very little.

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

The drift check also earns its keep across repeat runs: benchmarking three
times back to back heats the machine cumulatively, and reported drift climbs
from 0.5% to 13% to 25% while the *starting* control holds steady near 185ms.
So when comparing two versions of the code, compare first-run controls on a
cool machine rather than numbers taken mid-session.

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
