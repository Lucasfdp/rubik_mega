import { el } from "./dom.js";
import { icon } from "./icons.js";

/// Small dismissable messages (docs/en/13-phase6-polish.md D8). A failure
/// that does not stop the app (a scramble that threw, a solve that found
/// nothing, a clipboard that said no) used to overwrite the status line or
/// vanish into the console; now it says so, and goes away by itself.
/// Errors use role="alert" so a screen reader reads them at once; the rest
/// are polite.

export type ToastKind = "info" | "success" | "error";

const DURATION_MS: Readonly<Record<ToastKind, number>> = { info: 4000, success: 5000, error: 8000 };
/// More than this many at once and the oldest goes: a loop of failures
/// must not bury the page.
const MAX_VISIBLE = 3;

export class ToastHost {
  readonly element: HTMLElement;

  constructor() {
    this.element = el("div", { class: "toasts" });
  }

  show(message: string, kind: ToastKind = "info"): void {
    const dismiss = el("button", { class: "toast__close", props: { type: "button" }, attrs: { "aria-label": "Dismiss" } }, icon("close", "icon icon--small"));
    const toast = el(
      "div",
      { class: `toast toast--${kind}`, attrs: { role: kind === "error" ? "alert" : "status" } },
      el("span", { class: "toast__text", text: message }),
      dismiss,
    );
    const remove = (): void => {
      window.clearTimeout(timer);
      toast.remove();
    };
    dismiss.addEventListener("click", remove);
    const timer = window.setTimeout(remove, DURATION_MS[kind]);
    this.element.append(toast);
    while (this.element.children.length > MAX_VISIBLE) this.element.firstElementChild?.remove();
  }
}

/// The one host the app shows its toasts in; the shell mounts its element.
export const toasts = new ToastHost();
