import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// GitHub Pages serves a project repo from /<repo-name>/, so every asset URL
// needs that prefix. Set unconditionally rather than only for builds, so dev
// and preview serve from the same path as production -- otherwise `vite
// preview` serves at the root and the built HTML's asset URLs 404.
export default defineConfig({
  plugins: [react()],
  base: '/terrain-erosion/',
});
