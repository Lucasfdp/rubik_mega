/// Ambient module declarations Vite provides for asset imports (CSS,
/// images, etc.) -- needed from this UI increment onward since
/// ui/solve-panel.css (ui/solve-panel.ts's stylesheet) is the first
/// plain side-effect `import "./*.css"` anywhere in web/src/; nothing
/// before this needed non-TS imports typed.
/// <reference types="vite/client" />
