import { SHORTCUT_HELP, type ShortcutHelp } from "../../interaction/keyboard.js";
import { PuzzleType } from "../../engine/types.js";
import { el } from "./dom.js";
import { closeButton } from "./puzzle-picker.js";

/// The "?" sheet (docs/en/13-phase6-polish.md D9): every keyboard shortcut,
/// read from interaction/keyboard.ts's own table so the list cannot drift
/// from what the keys do. It shows the keys that apply to the open puzzle:
/// face letters on a cube, number keys on the rest.
export interface ShortcutsDialog {
  element: HTMLDialogElement;
  open(type: PuzzleType): void;
}

/// Which rows apply to a puzzle of this type.
export function shortcutsFor(type: PuzzleType): ShortcutHelp[] {
  const scope = type === PuzzleType.Cubic ? "cube" : "other";
  return SHORTCUT_HELP.filter((row) => row.scope === "all" || row.scope === scope);
}

export function createShortcutsDialog(): ShortcutsDialog {
  const list = el("dl", { class: "shortcuts" });
  const dialog = el(
    "dialog",
    { class: "dialog shortcuts-dialog", attrs: { "aria-labelledby": "shortcuts-title" } },
    el("div", { class: "dialog__head" }, el("h2", { class: "dialog__title", text: "Keyboard shortcuts", attrs: { id: "shortcuts-title" } }), closeButton(() => dialog.close())),
    list,
  );
  dialog.addEventListener("click", (event) => {
    if (event.target === dialog) dialog.close();
  });
  return {
    element: dialog,
    open(type) {
      list.replaceChildren(
        ...shortcutsFor(type).flatMap((row) => [el("dt", {}, el("kbd", { text: row.keys })), el("dd", { text: row.action })]),
      );
      if (!dialog.open) dialog.showModal();
    },
  };
}
