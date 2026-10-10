import { describe, expect, it } from "vitest";
import { Throttle } from "../../src/ui/throttle.js";

describe("Throttle", () => {
  it("lets the first message through at once", () => {
    expect(new Throttle(1000).offer("a", 0)).toBe("a");
  });

  it("holds messages inside the gap and keeps only the latest", () => {
    const t = new Throttle(1000);
    t.offer("a", 0);
    expect(t.offer("b", 200)).toBeNull();
    expect(t.offer("c", 400)).toBeNull();
    expect(t.waitMs(400)).toBe(600);
    expect(t.release(900)).toBeNull(); // not yet
    expect(t.release(1000)).toBe("c");
    expect(t.waitMs(1000)).toBeNull();
  });

  it("starts a new gap from a released message", () => {
    const t = new Throttle(1000);
    t.offer("a", 0);
    t.offer("b", 100);
    t.release(1000);
    expect(t.offer("c", 1500)).toBeNull();
    expect(t.offer("d", 2000)).toBe("d");
  });

  it("passes a message after a quiet gap, dropping any held one", () => {
    const t = new Throttle(1000);
    t.offer("a", 0);
    t.offer("b", 100);
    expect(t.offer("c", 5000)).toBe("c");
    expect(t.release(6000)).toBeNull();
  });

  it("cancel() drops a held message", () => {
    const t = new Throttle(1000);
    t.offer("a", 0);
    t.offer("b", 100);
    t.cancel();
    expect(t.release(2000)).toBeNull();
  });
});
