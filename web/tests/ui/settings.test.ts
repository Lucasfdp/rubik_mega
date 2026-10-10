import { describe, expect, it } from "vitest";
import { DEFAULT_SETTINGS, SETTINGS_KEY, loadSettings, parseSettings, saveSettings, serialiseSettings, type SettingsStorage } from "../../src/ui/settings.js";

function fakeStorage(initial: Record<string, string> = {}): SettingsStorage & { data: Record<string, string> } {
  const data = { ...initial };
  return { data, getItem: (k) => data[k] ?? null, setItem: (k, v) => { data[k] = v; } };
}

describe("settings", () => {
  it("round-trips every field", () => {
    const s = {
      theme: "dark",
      quality: "high",
      uiScale: "large",
      movesShown: true,
      animateScramble: false,
      palette: "high-contrast",
      glyphs: true,
      speed: 1440,
      lastPuzzle: "cube:5",
      algorithms: { "cube:5": "Reduction", skewb: "Optimal" },
    } as const;
    expect(parseSettings(serialiseSettings(s))).toEqual(s);
  });

  it("drops stored values that no longer name anything", () => {
    const parsed = parseSettings(
      JSON.stringify({ version: 1, speed: 123, lastPuzzle: "cube:99", palette: "neon", algorithms: { "cube:3": "Kociemba", nope: "x", skewb: 5 } }),
    );
    expect(parsed.speed).toBeNull();
    expect(parsed.lastPuzzle).toBeNull();
    expect(parsed.palette).toBe("classic");
    expect(parsed.algorithms).toEqual({ "cube:3": "Kociemba" });
  });

  it("falls back to defaults for missing, corrupt or future-version data", () => {
    expect(parseSettings(null)).toEqual(DEFAULT_SETTINGS);
    expect(parseSettings("{not json")).toEqual(DEFAULT_SETTINGS);
    expect(parseSettings(JSON.stringify({ version: 99, theme: "dark" }))).toEqual(DEFAULT_SETTINGS);
  });

  it("keeps valid fields and defaults only the invalid ones", () => {
    const parsed = parseSettings(JSON.stringify({ version: 1, theme: "dark", quality: "ultra", uiScale: 3, movesShown: "yes" }));
    expect(parsed).toEqual({ ...DEFAULT_SETTINGS, theme: "dark" });
  });

  it("saves and loads through storage", () => {
    const storage = fakeStorage();
    const s = { ...DEFAULT_SETTINGS, theme: "light" as const };
    saveSettings(s, storage);
    expect(storage.data[SETTINGS_KEY]).toBeDefined();
    expect(loadSettings(storage)).toEqual(s);
  });

  it("honours the legacy move-list key until a save writes the new object", () => {
    expect(loadSettings(fakeStorage({ "rubik_mega.movesShown": "1" })).movesShown).toBe(true);
    expect(loadSettings(fakeStorage({ "rubik_mega.movesShown": "0" })).movesShown).toBe(false);
  });

  it("loads defaults without storage", () => {
    expect(loadSettings(null)).toEqual(DEFAULT_SETTINGS);
  });
});
