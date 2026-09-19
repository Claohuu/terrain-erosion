# Terrain Erosion Simulator

Project context for Claude Code. Read this before suggesting or writing code.

## What this is

A C++ heightmap generator and hydraulic erosion simulator, compiled to
WebAssembly, driven by a React control panel. You move sliders, it regenerates
live, and noise-blob hills turn into terrain with real drainage networks.

The erosion is the point. Thousands of simulated water droplets pick up
sediment on steep slopes, carry it downhill, and deposit it where flow slows.
Rivers and valleys emerge from the simulation rather than being authored.

This is a portfolio/resume project. Its value depends on the author being able
to explain every design decision in an interview, so understanding matters more
than speed of delivery.

## Structure

```
sliders  →  generate()  →  erode()  →  render()  →  pixels
```

One program, one direction, every step visible on screen. The entire state is
a single array:

```cpp
float heights[width * height];
```

No networking, no threads, no protocol, no second process. Deterministic: the
same seed produces the same result every run, so every bug reproduces.

## Author background — read this carefully

- Strong Java / OOP background.
- **Graphics exposure is narrower than the resume suggests at a glance.** Has
  written a ray tracer in C# (mirror reflections, perspective correction) --
  that part is real. Has NOT done procedural mesh generation, and shader work
  (ShaderGraph, HLSL) is largely forgotten. Do not assume familiarity with
  noise functions, procedural generation, or graphics math. Ask before
  skipping an explanation.
- **Beginner at C++.** Assume no prior knowledge of pointers, raw buffers,
  headers vs source files, manual memory, or C-style error handling. Explain
  language mechanics alongside the algorithm, not just the algorithm.
- Has shipped commercial AR (8thWall) and Unity game work, and firmware in C++
  on embedded targets.

## How to work with the author

- **Explain before writing code.** One concept at a time.
- **Concrete mechanics beat metaphors.** What worked previously: "the OS keeps
  a table, each row has two boxes of bytes." What did not work: extended
  analogies. Show the actual data and the actual loop.
- **Two new concepts per message, maximum.** The author will say when it is too
  much; treat that as the loop working, not as a complaint.
- **Answer "why does this work."** Reasoning behind decisions, not just working
  solutions.
- **Prefer minimal changes** to existing code over rewrites.
- **Do not use ternary operators.**

## Division of work

Agreed split, because a previous project failed by being handed over complete:

- **The author writes:** the noise function, the droplet loop, the erosion math
  — every part of the actual algorithm. These are what an interviewer will
  probe.
- **Claude writes:** Emscripten configuration, WASM bindings, React
  scaffolding, build setup, and the canvas plumbing. Tooling that costs time
  and teaches nothing.

Do not hand over finished algorithm files. Explain, then let the author write
it.

## Build plan

Two full days. Each phase must work and be visible on screen before moving on.

| Phase | What | Budget |
|---|---|---|
| 0 | Emscripten working; one C++ function called from JS | 1.5h |
| 1 | Value noise + fractal octaves, rendered as a grayscale heightmap | 3.5h |
| 2 | Droplet simulation: flow downhill, draw paths, no sediment yet | 3h |
| 3 | Full erosion: sediment capacity, deposition, tuning | 3h |
| 4 | React controls: sliders, seed, regenerate, before/after | 2h |
| 5 | Hillshading so it reads as terrain | 1.5h |
| 6 | Measure droplets/sec, profile, one optimization, README | 1.5h |

If behind schedule, cut phase 5. **Do not cut phase 6** — the performance
numbers are what make this read as engineering rather than art.

Multithreading is deliberately out of scope. WASM threads require
SharedArrayBuffer and special server headers, and that tooling would consume
half the budget.

## Known hard parts

- **The WASM memory boundary.** C++ produces a large float array; JavaScript
  reads it to draw. They share one block of memory and JS gets a *view* into
  it rather than a copy. This is the only genuinely new concept in the project.
- **Erosion parameter tuning.** Six coupled parameters (sediment capacity,
  deposition rate, evaporation, droplet lifetime, inertia, erosion radius).
  Wrong values either do nothing visible or flatten the terrain entirely. It
  will feel broken when it is merely untuned. Build the droplet-path debug view
  early so the water can be watched rather than guessed at.
- **Grid indexing.** Row-major, `x + y * width`. Off-by-one and transposed-axis
  bugs are the most common failure and show up as visible seams or smearing.

## Resume framing

The author is targeting both game studios and big tech. Their resume is already
graphics-shaped, so this project must be presented as **simulation performance
work**, not as pretty pictures:

- Report droplets/second, not "it looks nice"
- Profile, find the real bottleneck, fix it, show before and after
- Verify correctness: does sediment picked up equal sediment deposited?

## Environment

- OS: Windows 11
- Toolchain: Emscripten (emsdk at `C:\Users\catzh\emsdk`, outside this repo)
- Frontend: React via Vite
- No server. The build is a static page, deployable to `claohuu.github.io`.

## Git conventions

- Commit at the end of each phase, describing what works
- Never commit build output (`build/`, `node_modules/`, `dist/`, `*.wasm`)
- The author handles all pushes. Do not add co-author trailers to commits.
