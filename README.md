# rubik_mega

Twisty puzzles that solve themselves, step by step, in your browser.

A generic 3D engine for twisty puzzles, with a solver for each one. Scramble a
puzzle by dragging its layers, then watch it solve itself move by move.

**Puzzles:** cubes from 3×3×3 to 8×8×8, Skewb, Pyraminx, Megaminx, Kilominx
and Square-1.

## Features

- Drag any layer to turn it, on every puzzle; orbit and zoom the camera.
- Solve playback with a phase timeline you can seek, a speed stepper, step
  forward and back, and copy-solution (WCA text for Square-1).
- Several solving methods for the 3×3×3 (Kociemba two-phase, Thistlethwaite,
  layer by layer, corners only); dedicated solvers for the other puzzles.
- Undo and redo, animated scrambles, solved-state celebration, keyboard
  controls (press `?` in the app for the list).
- Shareable links: `?p=cube&n=5&s=<seed>` reproduces the same puzzle and
  scramble.
- Light, dark and system themes, high-contrast palettes, sticker glyphs for
  colour-blind use, reduced-motion support.

## How it works

- `core/` is the puzzle engine and solvers, written in C and compiled to
  WebAssembly with Zig.
- `web/` is the app: TypeScript, Three.js and Vite. The WASM module runs in a
  Web Worker so solving never blocks the page.

## Getting started

You need Node.js, Python 3 with `pip` (the first `make wasm` installs Zig
through it) and a C compiler for the native tests.

```sh
# 1. Build the WebAssembly module (the web app needs it)
cd core
make wasm

# 2. Run the app
cd ../web
npm install
npm run dev
```

Open the address Vite prints. If the page loads but no puzzle appears, the
WASM file is missing or stale: rebuild it with `make wasm` and restart the dev
server.

## Commands

| Where | Command | What it does |
|---|---|---|
| `core/` | `make test` | native unit tests |
| `core/` | `make wasm` | WebAssembly build |
| `core/` | `make clean` | remove build output |
| `web/` | `npm run dev` | dev server |
| `web/` | `npm run build` | type-check and production bundle in `web/dist/` |
| `web/` | `npm test` | unit tests (needs `make wasm` first) |

## Repository layout

```
core/   C engine and solvers, native tests, WASM build
web/    TypeScript + Three.js app, tests, worker
```

## Deploying

`npm run build` produces a static site in `web/dist/` with relative asset
URLs, so it works from any path on a static host. Social-preview images need an
absolute URL: set the `og:image` tag in `web/src/index.html` once you know where
it is hosted.
