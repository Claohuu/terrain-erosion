# Terrain Erosion

A hydraulic erosion simulator written in C++, compiled to WebAssembly, driven
by a React control panel. Fractal noise produces a heightmap; hundreds of
thousands of simulated water droplets then carve it into terrain with real
drainage networks.

Nothing about the rivers is authored. Droplets pick up sediment on steep
ground, carry it downhill, and drop it where the flow slows. Later droplets
fall into the channels earlier ones cut, and valleys emerge from that feedback.

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

Then open http://localhost:5173.

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

## Deliberately out of scope

- **Multithreading.** Droplets are independent until they write, so this
  parallelizes well in principle, but WASM threads require SharedArrayBuffer
  and cross-origin isolation headers — substantial tooling for a static demo.
- **Sediment conservation.** Droplets that run off the edge of the map take
  their sediment with them, so total mass is not conserved. Correct would be
  depositing it at the boundary.
- **GPU.** A compute shader would beat this comfortably. The point here was
  CPU-side simulation performance.

## Layout

```
cpp/terrain.cpp        noise, erosion, and the exported entry points
build.sh               emcc invocation
web/src/App.jsx        React UI, hillshading, benchmark harness
web/src/index.css      styles
```

The C++ exposes two functions. `generate` allocates a heightmap and returns a
pointer; `erode` modifies one in place. The pointer is a byte offset into
WebAssembly's linear memory, and JavaScript reads the same bytes through a
`Float32Array` view — no copying across the boundary.
