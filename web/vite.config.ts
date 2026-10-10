import { defineConfig } from "vite";
import { fileURLToPath } from "node:url";

// Scaffolding-era config, deliberately minimal. Worker wiring
// (docs/en/03-architecture.md S3) lands with docs/en/04-roadmap.md
// Phase 0 step 4, not here.
export default defineConfig({
  root: "src",
  // Relative asset URLs, so the build works from any path (a GitHub Pages
  // project site, a subfolder) until the host is decided.
  base: "./",
  build: {
    outDir: "../dist",
    emptyOutDir: true,
    // three alone is ~566 kB minified and cannot be split further by us;
    // the limit sits just above it so the warning still fires if our own
    // code ever grows past it (it is ~59 kB today).
    chunkSizeWarningLimit: 600,
    rolldownOptions: {
      output: {
        // three is the bulk of the download and changes far less often
        // than our own code: its own chunk lets a returning visitor
        // re-fetch only what changed (docs/en/13-phase6-polish.md D3).
        codeSplitting: { groups: [{ name: "three", test: /node_modules[\\/]three[\\/]/ }] },
      },
    },
  },
  server: {
    fs: {
      // Dev server's static-file guard defaults to this project's own
      // root (web/) and below. core/build/rubik_mega.wasm lives one
      // level up, in the sibling core/ package -- web/src/engine/
      // fetches it straight from there rather than copying it into
      // web/ (docs/en/03-architecture.md S9: core/ and web/ are
      // deliberately siblings, not one nested in the other).
      allow: [fileURLToPath(new URL("..", import.meta.url))],
    },
  },
});
