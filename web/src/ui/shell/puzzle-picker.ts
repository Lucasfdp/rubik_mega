import { PuzzleType } from "../../engine/types.js";
import { DEFAULT_SIZE, PUZZLE_CHOICES, SUPPORTED_SIZES, choiceIndexFor } from "../puzzle-sizes.js";
import { el } from "./dom.js";
import { icon, puzzleGlyph } from "./icons.js";

/// The puzzle picker (docs/en/13-phase6-polish.md D7): a grid of cards,
/// one per puzzle, each with a picture of it, and a row of size chips
/// that applies to the cube. It replaces the two native <select>s. A
/// <dialog> gives modal behaviour, focus containment and Escape-to-close
/// for free.

export interface PuzzlePicker {
  element: HTMLDialogElement;
  /// Opens the picker with the current puzzle marked.
  open(current: { type: PuzzleType; size: number }): void;
  close(): void;
}

export function createPuzzlePicker(onPick: (type: PuzzleType, size: number) => void): PuzzlePicker {
  const grid = el("div", { class: "picker__grid" });
  const sizeRow = el("div", { class: "picker__sizes", attrs: { role: "group", "aria-label": "Cube size" } });
  const dialog = el(
    "dialog",
    { class: "dialog picker", attrs: { "aria-labelledby": "picker-title" } },
    el(
      "div",
      { class: "dialog__head" },
      el("h2", { class: "dialog__title", text: "Choose a puzzle", attrs: { id: "picker-title" } }),
      closeButton(() => dialog.close()),
    ),
    grid,
    el("p", { class: "picker__label", text: "Cube size" }),
    sizeRow,
  );

  let currentSize = DEFAULT_SIZE;

  function pick(type: PuzzleType, size: number): void {
    dialog.close();
    onPick(type, size);
  }

  PUZZLE_CHOICES.forEach((choice, index) => {
    const card = el("button", { class: "card", props: { type: "button" }, attrs: { "data-choice": String(index), "aria-pressed": "false" } });
    card.append(puzzleGlyph(choice.glyph), el("span", { class: "card__label", text: choice.label }));
    card.addEventListener("click", () => pick(choice.type, choice.hasSize ? currentSize : choice.size));
    grid.append(card);
  });

  for (const size of SUPPORTED_SIZES) {
    const chip = el("button", { class: "chip", text: String(size), props: { type: "button" }, attrs: { "data-size": String(size), "aria-pressed": "false", "aria-label": `${size} by ${size} by ${size}` } });
    chip.addEventListener("click", () => pick(PuzzleType.Cubic, size));
    sizeRow.append(chip);
  }

  // A click on the backdrop (the dialog element itself, outside its
  // content box) dismisses it.
  dialog.addEventListener("click", (event) => {
    if (event.target === dialog) dialog.close();
  });

  return {
    element: dialog,
    open(current) {
      const chosen = choiceIndexFor(current.type, current.size);
      currentSize = current.type === PuzzleType.Cubic ? current.size : DEFAULT_SIZE;
      grid.querySelectorAll<HTMLElement>(".card").forEach((card) => card.setAttribute("aria-pressed", String(Number(card.dataset.choice) === chosen)));
      sizeRow
        .querySelectorAll<HTMLElement>(".chip")
        .forEach((chip) => chip.setAttribute("aria-pressed", String(current.type === PuzzleType.Cubic && Number(chip.dataset.size) === current.size)));
      if (!dialog.open) dialog.showModal();
    },
    close: () => dialog.close(),
  };
}

/// The round close button both dialogs share.
export function closeButton(onClick: () => void): HTMLButtonElement {
  const button = el("button", { class: "icon-button", props: { type: "button" }, attrs: { "aria-label": "Close" } }, icon("close"));
  button.addEventListener("click", onClick);
  return button;
}
