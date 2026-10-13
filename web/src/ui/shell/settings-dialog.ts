import { PALETTE_CHOICES, type PaletteChoice } from "../../render/palettes.js";
import { QUALITY_CHOICES, type QualityChoice } from "../../render/quality.js";
import { THEME_CHOICES, UI_SCALE_CHOICES, type Settings, type ThemeChoice, type UiScale } from "../settings.js";
import { el } from "./dom.js";
import { closeButton } from "./puzzle-picker.js";

/// The settings dialog (docs/en/13-phase6-polish.md D7): theme, interface
/// size and graphics quality, each a row of radio buttons styled as a
/// segmented control. Real radio inputs, so the keyboard and screen
/// readers work without extra code.

export interface SettingsDialog {
  element: HTMLDialogElement;
  /// Opens the dialog showing `settings` and, for Auto quality, which
  /// tier it currently resolves to.
  open(settings: Settings, autoQualityNote: string): void;
}

const THEME_LABELS: Readonly<Record<ThemeChoice, string>> = { system: "System", light: "Light", dark: "Dark" };
const SCALE_LABELS: Readonly<Record<UiScale, string>> = { small: "Small", medium: "Medium", large: "Large" };
const PALETTE_LABELS: Readonly<Record<PaletteChoice, string>> = { classic: "Classic", "high-contrast": "High contrast" };
const QUALITY_LABELS: Readonly<Record<QualityChoice, string>> = { auto: "Auto", low: "Low", standard: "Standard", high: "High" };

function segmented<T extends string>(
  name: string,
  legend: string,
  values: readonly T[],
  labels: Readonly<Record<T, string>>,
  onSelect: (value: T) => void,
): { element: HTMLFieldSetElement; set(value: T): void } {
  const inputs = new Map<T, HTMLInputElement>();
  const element = el("fieldset", { class: "segmented" }, el("legend", { text: legend }));
  const row = el("div", { class: "segmented__row" });
  for (const value of values) {
    const input = el("input", { props: { type: "radio", name, value } });
    input.addEventListener("change", () => {
      if (input.checked) onSelect(value);
    });
    inputs.set(value, input);
    row.append(el("label", { class: "segmented__option" }, input, el("span", { text: labels[value] })));
  }
  element.append(row);
  return { element, set: (value) => void (inputs.get(value)!.checked = true) };
}

/// An on/off setting: a real checkbox with the switch role, so it is
/// announced as a switch and toggled by Space.
function toggle(label: string, hint: string, onToggle: (on: boolean) => void): { element: HTMLElement; set(on: boolean): void } {
  const input = el("input", { props: { type: "checkbox" }, attrs: { role: "switch" } });
  input.addEventListener("change", () => onToggle(input.checked));
  const element = el(
    "label",
    { class: "toggle" },
    el("span", { class: "toggle__text" }, el("span", { class: "toggle__label", text: label }), el("span", { class: "toggle__hint", text: hint })),
    input,
  );
  return { element, set: (on) => void (input.checked = on) };
}

export function createSettingsDialog(onChange: (settings: Settings) => void): SettingsDialog {
  let current: Settings;
  const update = (patch: Partial<Settings>): void => {
    current = { ...current, ...patch };
    onChange(current);
  };

  const theme = segmented("theme", "Theme", THEME_CHOICES, THEME_LABELS, (value) => update({ theme: value }));
  const scale = segmented("ui-scale", "Interface size", UI_SCALE_CHOICES, SCALE_LABELS, (value) => update({ uiScale: value }));
  const quality = segmented("quality", "Graphics quality", QUALITY_CHOICES, QUALITY_LABELS, (value) => update({ quality: value }));
  const palette = segmented("palette", "Sticker colours", PALETTE_CHOICES, PALETTE_LABELS, (value) => update({ palette: value }));
  const glyphs = toggle("Letters on stickers", "Marks every sticker with its face letter or a shape, so colour is never the only clue.", (on) => update({ glyphs: on }));
  const animate = toggle("Animate scramble", "Play the scramble's turns instead of jumping to the result.", (on) => update({ animateScramble: on }));
  const note = el("p", { class: "segmented__note" });
  quality.element.append(note);

  const dialog = el(
    "dialog",
    { class: "dialog settings", attrs: { "aria-labelledby": "settings-title" } },
    el("div", { class: "dialog__head" }, el("h2", { class: "dialog__title", text: "Settings", attrs: { id: "settings-title" } }), closeButton(() => dialog.close())),
    theme.element,
    scale.element,
    palette.element,
    glyphs.element,
    animate.element,
    quality.element,
  );
  dialog.addEventListener("click", (event) => {
    if (event.target === dialog) dialog.close();
  });

  return {
    element: dialog,
    open(settings, autoQualityNote) {
      current = settings;
      theme.set(settings.theme);
      scale.set(settings.uiScale);
      quality.set(settings.quality);
      palette.set(settings.palette);
      glyphs.set(settings.glyphs);
      animate.set(settings.animateScramble);
      note.textContent = autoQualityNote;
      if (!dialog.open) dialog.showModal();
    },
  };
}
