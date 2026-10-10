/// Whether the page URL asks for the `?debug` overlay. Kept apart from
/// the overlay itself so main.ts can ask without importing it: the
/// overlay is loaded lazily, only when this says yes.
export function debugRequested(search: string): boolean {
  return new URLSearchParams(search).has("debug");
}
