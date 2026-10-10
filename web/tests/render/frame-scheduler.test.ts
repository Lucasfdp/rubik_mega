import { describe, expect, it } from "vitest";
import { FrameScheduler, MAX_FRAME_DT_SEC, type FrameClock } from "../../src/render/frame-scheduler.js";

/// A clock the test drives by hand: frames run only when `flush` says so.
class ManualClock implements FrameClock {
  #callbacks = new Map<number, (nowMs: number) => void>();
  #nextHandle = 1;
  request(callback: (nowMs: number) => void): number {
    const handle = this.#nextHandle++;
    this.#callbacks.set(handle, callback);
    return handle;
  }
  cancel(handle: number): void {
    this.#callbacks.delete(handle);
  }
  get pending(): number {
    return this.#callbacks.size;
  }
  /// Runs every pending frame at `nowMs`.
  flush(nowMs: number): void {
    const due = [...this.#callbacks.values()];
    this.#callbacks.clear();
    for (const callback of due) callback(nowMs);
  }
}

describe("FrameScheduler", () => {
  it("does nothing until a frame is requested", () => {
    const clock = new ManualClock();
    new FrameScheduler(clock, () => false);
    expect(clock.pending).toBe(0);
  });

  it("many requests in one tick cost a single frame", () => {
    const clock = new ManualClock();
    let frames = 0;
    const scheduler = new FrameScheduler(clock, () => {
      frames++;
      return false;
    });
    scheduler.request();
    scheduler.request();
    scheduler.request();
    expect(clock.pending).toBe(1);
    clock.flush(16);
    expect(frames).toBe(1);
  });

  it("keeps asking for frames while the callback reports motion, then goes quiet", () => {
    const clock = new ManualClock();
    let remaining = 3;
    const scheduler = new FrameScheduler(clock, () => --remaining > 0);
    scheduler.request();
    for (let t = 16; t <= 160; t += 16) clock.flush(t);
    expect(scheduler.framesRendered).toBe(3);
    // The point of the whole class: at rest, no frame is waiting.
    expect(scheduler.isScheduled).toBe(false);
    expect(clock.pending).toBe(0);
  });

  it("renders zero further frames at rest, however long it waits", () => {
    const clock = new ManualClock();
    const scheduler = new FrameScheduler(clock, () => false);
    scheduler.request();
    clock.flush(16);
    const settled = scheduler.framesRendered;
    for (let t = 32; t < 10_000; t += 16) clock.flush(t);
    expect(scheduler.framesRendered).toBe(settled);
  });

  it("wakes again on a later request", () => {
    const clock = new ManualClock();
    const scheduler = new FrameScheduler(clock, () => false);
    scheduler.request();
    clock.flush(16);
    scheduler.request();
    expect(clock.pending).toBe(1);
    clock.flush(5000);
    expect(scheduler.framesRendered).toBe(2);
  });

  it("a request made during a frame is honoured even when that frame reports rest", () => {
    const clock = new ManualClock();
    let scheduler: FrameScheduler;
    let first = true;
    scheduler = new FrameScheduler(clock, () => {
      if (first) {
        first = false;
        scheduler.request(); // e.g. a scene resync landing mid-frame
      }
      return false;
    });
    scheduler.request();
    clock.flush(16);
    expect(clock.pending).toBe(1);
  });

  it("passes 0 on the first frame after rest, then real intervals, clamped", () => {
    const clock = new ManualClock();
    const steps: number[] = [];
    let remaining = 3;
    const scheduler = new FrameScheduler(clock, (dtSec) => {
      steps.push(dtSec);
      return --remaining > 0;
    });
    scheduler.request();
    clock.flush(1000);
    clock.flush(1016);
    clock.flush(61_016); // a hidden tab: a minute between frames
    expect(steps[0]).toBe(0);
    expect(steps[1]).toBeCloseTo(0.016, 6);
    expect(steps[2]).toBe(MAX_FRAME_DT_SEC);
    // After a rest the next frame starts from 0 again, not from the gap.
    scheduler.request();
    clock.flush(900_000);
    expect(steps[3]).toBe(0);
  });

  it("dispose cancels the pending frame", () => {
    const clock = new ManualClock();
    const scheduler = new FrameScheduler(clock, () => false);
    scheduler.request();
    scheduler.dispose();
    expect(clock.pending).toBe(0);
    expect(scheduler.isScheduled).toBe(false);
  });
});
