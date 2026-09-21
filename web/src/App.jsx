import { useCallback, useEffect, useRef, useState } from 'react';
import createTerrainModule from './wasm/terrain.js';

const WIDTH = 512;
const HEIGHT = 512;

function normalize(x, y, z) {
  const length = Math.sqrt(x * x + y * y + z * z);
  return { x: x / length, y: y / length, z: z / length };
}

const LIGHT = normalize(-0.6, -0.6, 0.55);

const STOPS = [
  [0.0, [42, 58, 74]],
  [0.3, [68, 92, 78]],
  [0.55, [104, 116, 84]],
  [0.75, [132, 122, 104]],
  [0.9, [158, 154, 148]],
  [1.0, [226, 226, 230]],
];

function elevationColor(t) {
  for (let i = 0; i < STOPS.length - 1; i += 1) {
    const [t0, c0] = STOPS[i];
    const [t1, c1] = STOPS[i + 1];

    if (t <= t1) {
      const f = (t - t0) / (t1 - t0);
      return [
        c0[0] + (c1[0] - c0[0]) * f,
        c0[1] + (c1[1] - c0[1]) * f,
        c0[2] + (c1[2] - c0[2]) * f,
      ];
    }
  }

  return STOPS[STOPS.length - 1][1];
}

function renderHeights(heights, imageData, mode, relief) {
  let min = Infinity;
  let max = -Infinity;

  for (let i = 0; i < heights.length; i += 1) {
    if (heights[i] < min) {
      min = heights[i];
    }
    if (heights[i] > max) {
      max = heights[i];
    }
  }

  const span = Math.max(1e-6, max - min);
  const data = imageData.data;

  for (let y = 0; y < HEIGHT; y += 1) {
    for (let x = 0; x < WIDTH; x += 1) {
      const i = x + y * WIDTH;
      const t = (heights[i] - min) / span;
      const p = i * 4;

      if (mode === 'height') {
        const grey = Math.round(t * 255);
        data[p] = grey;
        data[p + 1] = grey;
        data[p + 2] = grey;
        data[p + 3] = 255;
        continue;
      }

      let xm = i - 1;
      let xp = i + 1;
      let ym = i - WIDTH;
      let yp = i + WIDTH;

      if (x === 0) {
        xm = i;
      }
      if (x === WIDTH - 1) {
        xp = i;
      }
      if (y === 0) {
        ym = i;
      }
      if (y === HEIGHT - 1) {
        yp = i;
      }

      const dzdx = ((heights[xp] - heights[xm]) / span) * relief;
      const dzdy = ((heights[yp] - heights[ym]) / span) * relief;

      const n = normalize(-dzdx, -dzdy, 1.0);
      let shade = n.x * LIGHT.x + n.y * LIGHT.y + n.z * LIGHT.z;

      if (shade < 0) {
        shade = 0;
      }

      shade = 0.25 + 0.75 * shade;

      const rgb = elevationColor(t);

      data[p] = Math.min(255, rgb[0] * shade);
      data[p + 1] = Math.min(255, rgb[1] * shade);
      data[p + 2] = Math.min(255, rgb[2] * shade);
      data[p + 3] = 255;
    }
  }
}

const TIPS = {
  seed: 'Starting number for the randomness. The same seed always produces exactly the same terrain.',
  'feature size': 'How far apart the hills are. Larger values mean fewer, broader landforms.',
  octaves: 'How many layers of noise are stacked. Each added layer contributes finer detail.',
  persistence: 'How much each finer layer counts. Higher values make the terrain rougher.',
  droplets: 'How many water particles are simulated. More droplets carve deeper, more connected valleys.',
  inertia: 'How reluctant water is to change direction. Low carves tight ravines, high carves sweeping valleys.',
  'erode speed': 'How quickly a droplet scrapes sediment off the ground it passes over.',
  'deposit speed': 'How quickly a droplet drops its sediment once the water slows down.',
  'brush radius': 'How wide an area each droplet scrapes. Larger values give smoother valley walls instead of narrow scratches.',
  relief: 'Display only. Exaggerates the lighting to make slopes read more clearly. Does not change the terrain.',
};

const DEFAULTS = {
  seed: 1337,
  gridSize: 140,
  octaves: 6,
  persistence: 0.5,
  lacunarity: 2.0,
  droplets: 150000,
  inertia: 0.05,
  erodeSpeed: 0.3,
  depositSpeed: 0.3,
  radius: 3,
};

export default function App() {
  const canvasRef = useRef(null);
  const moduleRef = useRef(null);
  const mapsRef = useRef({ raw: null, eroded: null });

  const [params, setParams] = useState(DEFAULTS);
  const [view, setView] = useState('eroded');
  const [mode, setMode] = useState('shaded');
  const [relief, setRelief] = useState(120);
  const [timing, setTiming] = useState(null);
  const [busy, setBusy] = useState(false);
  const [stale, setStale] = useState(false);
  const [ready, setReady] = useState(false);
  const [bench, setBench] = useState(null);

  const draw = useCallback(() => {
    const maps = mapsRef.current;
    let heights = maps.eroded;

    if (view === 'raw') {
      heights = maps.raw;
    }

    if (!heights || !canvasRef.current) {
      return;
    }

    const ctx = canvasRef.current.getContext('2d');
    const image = ctx.createImageData(WIDTH, HEIGHT);
    renderHeights(heights, image, mode, relief);
    ctx.putImageData(image, 0, 0);
  }, [view, mode, relief]);

  const run = useCallback((p) => {
    const mod = moduleRef.current;

    if (!mod) {
      return;
    }

    setBusy(true);

    setTimeout(() => {
      const genStart = performance.now();
      const ptr = mod._generate(WIDTH, HEIGHT, p.seed, p.gridSize,
        p.octaves, p.persistence, p.lacunarity);
      const genMs = performance.now() - genStart;

      const floatIndex = ptr / 4;
      const shared = mod.HEAPF32.subarray(floatIndex, floatIndex + WIDTH * HEIGHT);

      const raw = new Float32Array(shared);

      const erodeStart = performance.now();
      mod._erode(ptr, WIDTH, HEIGHT, p.droplets, p.seed,
        p.inertia, p.erodeSpeed, p.depositSpeed, p.radius);
      const erodeMs = performance.now() - erodeStart;

      const eroded = new Float32Array(shared);

      mod._free(ptr);

      mapsRef.current = { raw, eroded };

      let perSecond = 0;
      if (erodeMs > 0) {
        perSecond = Math.round(p.droplets / (erodeMs / 1000));
      }

      setTiming({ genMs, erodeMs, droplets: p.droplets, perSecond });
      setBusy(false);
      setStale(false);
    }, 16);
  }, []);

  const runBenchmark = useCallback((p, runs) => {
    const mod = moduleRef.current;

    if (!mod) {
      return;
    }

    setBusy(true);

    setTimeout(() => {
      const samples = [];

      for (let i = 0; i < runs; i += 1) {
        const ptr = mod._generate(WIDTH, HEIGHT, p.seed, p.gridSize,
          p.octaves, p.persistence, p.lacunarity);

        const t0 = performance.now();
        mod._erode(ptr, WIDTH, HEIGHT, p.droplets, p.seed,
          p.inertia, p.erodeSpeed, p.depositSpeed, p.radius);
        samples.push(performance.now() - t0);

        mod._free(ptr);
      }

      samples.sort((a, b) => a - b);

      const median = samples[Math.floor(samples.length / 2)];

      setBench({
        runs,
        median,
        min: samples[0],
        max: samples[samples.length - 1],
        perSecond: Math.round(p.droplets / (median / 1000)),
        droplets: p.droplets,
      });
      setBusy(false);
    }, 16);
  }, []);

  useEffect(() => {
    let cancelled = false;

    createTerrainModule().then((mod) => {
      if (cancelled) {
        return;
      }
      moduleRef.current = mod;
      setReady(true);
      run(DEFAULTS);
    });

    return () => {
      cancelled = true;
    };
  }, [run]);

  useEffect(() => {
    draw();
  }, [draw, timing]);

  const update = (key, value) => {
    setParams((prev) => ({ ...prev, [key]: value }));
    setStale(true);
  };

  return (
    <div className="app">
      <header>
        <h1>Terrain Erosion</h1>
        <span className="sub">C++ · WebAssembly · hydraulic erosion</span>
      </header>

      <div className="layout">
        <div className="viewport">
          <canvas ref={canvasRef} width={WIDTH} height={HEIGHT} />

          <div className="toolbar">
            <div className="seg">
              <button className={view === 'raw' ? 'on' : ''} onClick={() => setView('raw')}>
                before
              </button>
              <button className={view === 'eroded' ? 'on' : ''} onClick={() => setView('eroded')}>
                after
              </button>
            </div>

            <div className="seg">
              <button className={mode === 'shaded' ? 'on' : ''} onClick={() => setMode('shaded')}>
                shaded
              </button>
              <button className={mode === 'height' ? 'on' : ''} onClick={() => setMode('height')}>
                heightmap
              </button>
            </div>
          </div>

          {timing && (
            <p className="stats">
              generate <b>{timing.genMs.toFixed(0)}ms</b> · erode{' '}
              <b>{timing.droplets.toLocaleString()}</b> droplets in{' '}
              <b>{timing.erodeMs.toFixed(0)}ms</b> ·{' '}
              <b>{timing.perSecond.toLocaleString()}</b> droplets/sec
            </p>
          )}
        </div>

        <aside className="panel">
          <h2>Terrain</h2>
          <Slider label="seed" value={params.seed} min={1} max={9999} step={1}
            onChange={(v) => update('seed', v)} />
          <Slider label="feature size" value={params.gridSize} min={20} max={300} step={5}
            onChange={(v) => update('gridSize', v)} />
          <Slider label="octaves" value={params.octaves} min={1} max={9} step={1}
            onChange={(v) => update('octaves', v)} />
          <Slider label="persistence" value={params.persistence} min={0.1} max={0.9} step={0.05}
            onChange={(v) => update('persistence', v)} />

          <h2>Erosion</h2>
          <Slider label="droplets" value={params.droplets} min={0} max={500000} step={10000}
            onChange={(v) => update('droplets', v)} />
          <Slider label="inertia" value={params.inertia} min={0} max={0.9} step={0.05}
            onChange={(v) => update('inertia', v)} />
          <Slider label="erode speed" value={params.erodeSpeed} min={0.05} max={1} step={0.05}
            onChange={(v) => update('erodeSpeed', v)} />
          <Slider label="deposit speed" value={params.depositSpeed} min={0.05} max={1} step={0.05}
            onChange={(v) => update('depositSpeed', v)} />
          <Slider label="brush radius" value={params.radius} min={1} max={8} step={1}
            onChange={(v) => update('radius', v)} />

          <h2>Display</h2>
          <Slider label="relief" value={relief} min={20} max={400} step={10}
            onChange={setRelief} />

          <button className="primary" disabled={busy || !ready} onClick={() => run(params)}>
            {busy ? 'simulating…' : 'regenerate'}
          </button>

          {stale && !busy && <p className="hint">parameters changed — hit regenerate</p>}

          <button
            className="ghost"
            disabled={busy || !ready}
            onClick={() => runBenchmark(params, 7)}
          >
            benchmark (7 runs)
          </button>

          {bench && (
            <p className="bench">
              median <b>{bench.median.toFixed(0)}ms</b>
              <br />
              spread {bench.min.toFixed(0)}–{bench.max.toFixed(0)}ms over {bench.runs} runs
              <br />
              <b>{bench.perSecond.toLocaleString()}</b> droplets/sec
            </p>
          )}

          <button
            className="ghost"
            onClick={() => {
              setParams(DEFAULTS);
              setStale(true);
            }}
          >
            reset
          </button>
        </aside>
      </div>
    </div>
  );
}

function Slider({ label, value, min, max, step, onChange }) {
  return (
    <label className="slider">
      <span className="row">
        <span className="help" data-tip={TIPS[label]}>{label}</span>
        <b>{value}</b>
      </span>
      <input
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        onChange={(e) => onChange(parseFloat(e.target.value))}
      />
    </label>
  );
}
