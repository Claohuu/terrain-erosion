import { useEffect, useRef, useState } from 'react';
import createTerrainModule from './wasm/terrain.js';

const WIDTH = 512;
const HEIGHT = 512;

export default function App() {
  const canvasRef = useRef(null);
  const [status, setStatus] = useState('loading WebAssembly...');

  useEffect(() => {
    let cancelled = false;

    createTerrainModule().then((mod) => {
      if (cancelled) {
        return;
      }

      // cwrap wraps a C function so JavaScript can call it. The strings are
      // the C signature: generate returns a number (the pointer), and takes
      // four numbers.
      let generate;

      try {
        generate = mod.cwrap('generate', 'number', [
          'number', 'number', 'number', 'number',
        ]);
      } catch {
        setStatus('generate() not found — write it in cpp/terrain.cpp, then run ./build.sh');
        return;
      }

      const seed = 1337;
      const gridSize = 64.0;

      const start = performance.now();
      const ptr = generate(WIDTH, HEIGHT, seed, gridSize);
      const elapsed = performance.now() - start;

      if (ptr === 0) {
        setStatus('generate() returned null — allocation failed?');
        return;
      }

      // ptr is a BYTE offset into the shared memory. HEAPF32 views that same
      // memory as 32-bit floats, so the float index is ptr / 4. subarray does
      // not copy -- it hands back a window onto the bytes C++ just wrote.
      const floatIndex = ptr / 4;
      const heights = mod.HEAPF32.subarray(floatIndex, floatIndex + WIDTH * HEIGHT);

      // Turn heights into greyscale pixels. Canvas wants RGBA, one byte each.
      const ctx = canvasRef.current.getContext('2d');
      const image = ctx.createImageData(WIDTH, HEIGHT);

      let min = Infinity;
      let max = -Infinity;

      for (let i = 0; i < heights.length; i += 1) {
        if (heights[i] < min) {
          min = heights[i];
        }
        if (heights[i] > max) {
          max = heights[i];
        }

        const grey = Math.max(0, Math.min(255, Math.round(heights[i] * 255)));
        const p = i * 4;
        image.data[p] = grey;
        image.data[p + 1] = grey;
        image.data[p + 2] = grey;
        image.data[p + 3] = 255;
      }

      ctx.putImageData(image, 0, 0);

      // C++ malloc'd this and will never free it on its own. Without this the
      // memory leaks on every regenerate.
      mod._free(ptr);

      setStatus(
        `${WIDTH}x${HEIGHT} in ${elapsed.toFixed(1)}ms · range ${min.toFixed(3)} to ${max.toFixed(3)}`
      );
    });

    return () => {
      cancelled = true;
    };
  }, []);

  return (
    <div style={{ fontFamily: 'system-ui', padding: 32, color: '#e4e4ec', background: '#16161c', minHeight: '100vh' }}>
      <h1 style={{ fontSize: 20, margin: '0 0 4px' }}>Terrain Erosion</h1>
      <p style={{ color: '#8b8b9c', fontSize: 13, margin: '0 0 16px' }}>{status}</p>
      <canvas
        ref={canvasRef}
        width={WIDTH}
        height={HEIGHT}
        style={{ border: '1px solid #2c2c38', imageRendering: 'pixelated' }}
      />
    </div>
  );
}
