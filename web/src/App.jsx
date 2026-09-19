import { useEffect, useState } from 'react';
import createTerrainModule from './wasm/terrain.js';

export default function App() {
  const [status, setStatus] = useState('loading WebAssembly...');

  useEffect(() => {
    let cancelled = false;

    // The module loads asynchronously, so everything that touches C++ has to
    // wait for this promise. Once resolved, `mod` is the bridge: it holds the
    // exported functions and the shared memory buffer.
    createTerrainModule().then((mod) => {
      if (cancelled) {
        return;
      }

      // cwrap builds a JS function that calls into C++. The strings describe
      // the C signature: returns a number, takes two numbers.
      const add = mod.cwrap('add', 'number', ['number', 'number']);
      const result = add(2, 3);

      setStatus(`C++ says 2 + 3 = ${result}`);
    });

    return () => {
      cancelled = true;
    };
  }, []);

  return (
    <div style={{ fontFamily: 'system-ui', padding: 40 }}>
      <h1>Terrain Erosion</h1>
      <p>Phase 0 — toolchain check</p>
      <p style={{ fontSize: 20 }}>{status}</p>
    </div>
  );
}
