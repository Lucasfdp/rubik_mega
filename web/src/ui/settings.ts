import { PALETTE_CHOICES, type PaletteChoice } from "../render/palettes.js";
import { QUALITY_CHOICES, type QualityChoice } from "../render/quality.js";
import { PLAYBACK_SPEEDS_DEG_PER_SEC } from "../interaction/solve-playback.js";
import { puzzleFromKey } from "./puzzle-sizes.js";

/// The user's persistent choices, in one versioned object
/// (docs/en/13-phase6-polish.md D10): `rubik_mega.settings.v1`. Parsing
/// is strict about shape and forgiving about content -- anything missing,
/// mistyped or from a future version falls back to its default instead of
/// throwing, because storage is user-editable and can outlive the code
/// that wrote it.

export type ThemeChoice = "system" | "light" | "dark";
export const THEME_CHOICES: readonly ThemeChoice[] = ["system", "light", "dark"];

/// Interface size: scales every rem in the interface, so text, controls
/// and spacing grow together. The 3D view is unaffected.
export type UiScale = "small" | "medium" | "large";
export const UI_SCALE_CHOICES: readonly UiScale[] = ["small", "medium", "large"];
/// Root font size as a percentage of the browser's, per scale.
export const UI_SCALE_PERCENT: Readonly<Record<UiScale, number>> = { small: 90, medium: 100, large: 115 };

export interface Settings {
  theme: ThemeChoice;
  quality: QualityChoice;
  uiScale: UiScale;
  /// Whether the move list is open. null: never chosen, so the layout
  /// decides (open beside the puzzle on wide screens, closed on phones).
  movesShown: boolean | null;
  /// Whether Scramble plays its turns (true) or snaps to the result.
  animateScramble: boolean;
  /// Sticker colours: the classic scheme or a high-contrast one.
  palette: PaletteChoice;
  /// A letter or shape on every sticker, so colour is never the only cue.
  glyphs: boolean;
  /// Playback speed in degrees per second, one of the stepper's presets.
  speed: number | null;
  /// The puzzle last open, as ui/puzzle-sizes.ts's puzzleKey(), or null.
  lastPuzzle: string | null;
  /// The solver last chosen for each puzzle, by name (an index would
  /// silently change meaning if the list were ever reordered).
  algorithms: Readonly<Record<string, string>>;
}

export const DEFAULT_SETTINGS: Readonly<Settings> = {
  theme: "system",
  quality: "auto",
  uiScale: "medium",
  movesShown: null,
  animateScramble: true,
  palette: "classic",
  glyphs: false,
  speed: null,
  lastPuzzle: null,
  algorithms: {},
};

export const SETTINGS_VERSION = 1;
export const SETTINGS_KEY = `rubik_mega.settings.v${SETTINGS_VERSION}`;
/// Where "move list shown" lived before settings were one object.
const LEGACY_MOVES_SHOWN_KEY = "rubik_mega.movesShown";

function oneOf<T extends string>(value: unknown, allowed: readonly T[], fallback: T): T {
  return typeof value === "string" && (allowed as readonly string[]).includes(value) ? (value as T) : fallback;
}

/// Only string-to-string entries survive; a stored object is user-editable.
function algorithmsFrom(value: unknown): Record<string, string> {
  if (typeof value !== "object" || value === null || Array.isArray(value)) return {};
  const kept: Record<string, string> = {};
  for (const [key, name] of Object.entries(value)) {
    if (typeof name === "string" && puzzleFromKey(key)) kept[key] = name;
  }
  return kept;
}

/// Settings from stored text, field by field.
export function parseSettings(raw: string | null): Settings {
  if (raw === null) return { ...DEFAULT_SETTINGS };
  let data: unknown;
  try {
    data = JSON.parse(raw);
  } catch {
    return { ...DEFAULT_SETTINGS };
  }
  if (typeof data !== "object" || data === null || (data as { version?: unknown }).version !== SETTINGS_VERSION) {
    return { ...DEFAULT_SETTINGS };
  }
  const fields = data as Record<string, unknown>;
  return {
    theme: oneOf(fields.theme, THEME_CHOICES, DEFAULT_SETTINGS.theme),
    quality: oneOf(fields.quality, QUALITY_CHOICES, DEFAULT_SETTINGS.quality),
    uiScale: oneOf(fields.uiScale, UI_SCALE_CHOICES, DEFAULT_SETTINGS.uiScale),
    movesShown: typeof fields.movesShown === "boolean" ? fields.movesShown : DEFAULT_SETTINGS.movesShown,
    animateScramble: typeof fields.animateScramble === "boolean" ? fields.animateScramble : DEFAULT_SETTINGS.animateScramble,
    palette: oneOf(fields.palette, PALETTE_CHOICES, DEFAULT_SETTINGS.palette),
    glyphs: typeof fields.glyphs === "boolean" ? fields.glyphs : DEFAULT_SETTINGS.glyphs,
    speed: (PLAYBACK_SPEEDS_DEG_PER_SEC as readonly unknown[]).includes(fields.speed) ? (fields.speed as number) : DEFAULT_SETTINGS.speed,
    lastPuzzle: typeof fields.lastPuzzle === "string" && puzzleFromKey(fields.lastPuzzle) ? fields.lastPuzzle : DEFAULT_SETTINGS.lastPuzzle,
    algorithms: algorithmsFrom(fields.algorithms),
  };
}

export function serialiseSettings(settings: Settings): string {
  return JSON.stringify({ version: SETTINGS_VERSION, ...settings });
}

/// The minimal storage surface used, so a test can pass a fake.
export interface SettingsStorage {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

/// The browser's localStorage, or null where it is unavailable (private
/// mode, blocked site data): every access can throw, so reaching for it
/// is guarded here once.
export function browserStorage(): SettingsStorage | null {
  try {
    return window.localStorage;
  } catch {
    return null;
  }
}

/// Stored settings, or the defaults. The old standalone move-list key is
/// honoured once, until the first save writes the new object.
export function loadSettings(storage: SettingsStorage | null = browserStorage()): Settings {
  if (!storage) return { ...DEFAULT_SETTINGS };
  try {
    const raw = storage.getItem(SETTINGS_KEY);
    if (raw !== null) return parseSettings(raw);
    const legacy = storage.getItem(LEGACY_MOVES_SHOWN_KEY);
    if (legacy !== null) return { ...DEFAULT_SETTINGS, movesShown: legacy === "1" };
  } catch {
    // fall through to the defaults
  }
  return { ...DEFAULT_SETTINGS };
}

/// Saves, silently doing nothing where storage is unavailable: not
/// remembering a choice is harmless, a thrown error mid-click is not.
export function saveSettings(settings: Settings, storage: SettingsStorage | null = browserStorage()): void {
  if (!storage) return;
  try {
    storage.setItem(SETTINGS_KEY, serialiseSettings(settings));
  } catch {
    // not remembered this time
  }
}

/// The browser-chrome colour that goes with each theme (index.html's
/// <meta name="theme-color"> pair follows the SYSTEM theme; a theme chosen
/// here needs its own).
export const THEME_COLOUR: Readonly<Record<Exclude<ThemeChoice, "system">, string>> = { dark: "#0b0d12", light: "#e7ebf3" };

/// Applies the theme and interface scale to the page. The CSS reads
/// `data-theme` (absent means follow the system) and the root font size.
/// A chosen theme also gets a theme-color tag, placed first so it beats the
/// system-following pair; choosing System removes it again.
export function applyAppearance(settings: Settings, root: HTMLElement = document.documentElement): void {
  if (settings.theme === "system") delete root.dataset.theme;
  else root.dataset.theme = settings.theme;
  root.style.fontSize = `${UI_SCALE_PERCENT[settings.uiScale]}%`;

  const head = root.ownerDocument.head;
  let meta = head.querySelector<HTMLMetaElement>("meta[name='theme-color'][data-chosen]");
  if (settings.theme === "system") {
    meta?.remove();
    return;
  }
  if (!meta) {
    meta = root.ownerDocument.createElement("meta");
    meta.name = "theme-color";
    meta.dataset.chosen = "";
    head.prepend(meta);
  }
  meta.content = THEME_COLOUR[settings.theme];
}
