# Terrain Erosion

A hydraulic erosion simulator written in C++, compiled to WebAssembly, driven
by a React control panel.

Fractal noise produces a heightmap, then hundreds of thousands of simulated
water droplets carve it into terrain. Each droplet picks up sediment on steep
ground, carries it downhill, and drops it where the flow slows. Later droplets
fall into the channels earlier ones cut, and river valleys emerge from that
feedback — nothing about the drainage network is authored.

**[Live demo](https://claohuu.github.io/terrain-erosion/)**

```
sliders  →  generate()  →  erode()  →  hillshade  →  canvas
              C++/WASM     C++/WASM      JS
```

Everything runs in the browser. There is no server.

## Running it

Requires [Emscripten](https://emscripten.org/) for the C++ and Node for the
frontend.

```bash
./build.sh          # compiles cpp/terrain.cpp to web/src/wasm/terrain.js
cd web
npm install
npm run dev
```

Then open http://localhost:5173/terrain-erosion/ — the path prefix matches how
GitHub Pages serves it.

**Re-run `./build.sh` after any C++ change.** The JavaScript hot-reloads; the
WebAssembly does not.

Tests and the scaling benchmark:

```bash
./run_tests.sh      # 27 checks, native build, no browser needed
./run_bench.sh      # scaling across resolution, droplets, and brush radius
```

## Using it

- **before / after** toggles between the raw noise and the eroded result
- **shaded / heightmap** switches between lit relief and raw greyscale
- **droplets** is the main erosion dial; 0 leaves the terrain untouched
- **inertia** controls how sharply water turns. Low values carve tight ravines,
  high values carve sweeping valleys that cut across contours
- **brush radius** sets how wide each droplet scrapes. 1 gives pixel-wide
  scratches, larger values give sloped valley walls

Parameters take effect when you press **regenerate** — a full run takes a few
hundred milliseconds and blocks the page while it works.

## How it works

**Generation.** Random values are placed at the corners of a coarse lattice and
interpolated in between, which is what makes nearby points similar rather than
producing static. Several layers at doubling frequency and halving amplitude
are summed, giving large landforms with fine detail riding on top.

**Erosion.** Each droplet samples the slope beneath it, steers by blending its
existing direction with downhill, then compares how much sediment it can carry
against how much it holds. Carrying too much, it deposits; too little, it
erodes. It speeds up descending and loses water to evaporation until it dies.

The simulation is deterministic — the same seed always produces the same
terrain, so every result is reproducible and every bug repeats.

## Layout

```
cpp/terrain.cpp                 noise, erosion, exported entry points
cpp/test_terrain.cpp            native test suite
cpp/bench_terrain.cpp           native scaling benchmark
web/src/App.jsx                 React UI, hillshading, in-page benchmark
build.sh                        emcc invocation
.github/workflows/deploy.yml    test, build, publish to Pages
```

The C++ exposes two functions. `generate` allocates a heightmap and returns a
pointer; `erode` modifies one in place. That pointer is a byte offset into
WebAssembly's linear memory, and JavaScript reads the same bytes through a
`Float32Array` view — nothing is copied across the boundary.

## Engineering notes

Performance work, test coverage, scaling measurements, and benchmarking
methodology are in [ENGINEERING.md](ENGINEERING.md).
