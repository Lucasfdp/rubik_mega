import { el } from "./shell/dom.js";

/// The screen shown when the app cannot start (docs/en/13-phase6-polish.md
/// D8): a plain explanation and a Reload button, with the technical
/// detail folded away. It replaces a raw red <pre> that was fine while the
/// app was a developer tool and not for a link someone else opens.
///
/// Built from DOM nodes with textContent, so an error message can never
/// inject markup.
export function showFatal(error: unknown, root: HTMLElement = document.body): void {
  console.error("failed to start rubik_mega:", error);
  const reload = el("button", { class: "btn btn--primary", text: "Reload", props: { type: "button" } });
  reload.style.flex = "0 0 auto";
  reload.addEventListener("click", () => window.location.reload());
  const detail = error instanceof Error ? `${error.name}: ${error.message}\n${error.stack ?? ""}`.trim() : String(error);
  const screen = el(
    "div",
    { class: "fatal", attrs: { role: "alert" } },
    el("h1", { text: "Something went wrong" }),
    el("p", { text: "The puzzle could not start. Reloading usually fixes it; if it keeps happening, your browser may not support WebGL or WebAssembly." }),
    reload,
    el("details", {}, el("summary", { text: "Technical details" }), el("pre", { text: detail })),
  );
  removeSplash();
  root.replaceChildren(screen);
}

/// Removes the loading splash (index.html) once the first frame is up.
export function removeSplash(): void {
  document.getElementById("splash")?.remove();
}
