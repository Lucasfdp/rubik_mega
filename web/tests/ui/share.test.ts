import { describe, expect, it, vi } from "vitest";
import { shareLink } from "../../src/ui/share.js";

const URL_ = "https://example.test/?p=cube&n=3&s=1&m=25";

describe("shareLink", () => {
  it("uses the share sheet where there is one", async () => {
    const share = vi.fn(async () => {});
    expect(await shareLink(URL_, "Cube", { share })).toBe("shared");
    expect(share).toHaveBeenCalledWith({ title: "Cube", url: URL_ });
  });

  it("copies the link where there is no share sheet", async () => {
    const writeText = vi.fn(async () => {});
    expect(await shareLink(URL_, "Cube", { clipboard: { writeText } })).toBe("copied");
    expect(writeText).toHaveBeenCalledWith(URL_);
  });

  it("copies instead when the share sheet says it cannot share this", async () => {
    const share = vi.fn(async () => {});
    const writeText = vi.fn(async () => {});
    expect(await shareLink(URL_, "Cube", { share, canShare: () => false, clipboard: { writeText } })).toBe("copied");
    expect(share).not.toHaveBeenCalled();
  });

  it("treats a dismissed share sheet as nothing, not a failure", async () => {
    const share = vi.fn(async () => {
      throw new DOMException("closed", "AbortError");
    });
    const writeText = vi.fn(async () => {});
    expect(await shareLink(URL_, "Cube", { share, clipboard: { writeText } })).toBe("cancelled");
    expect(writeText).not.toHaveBeenCalled();
  });

  it("falls back to the clipboard when sharing fails some other way", async () => {
    const share = vi.fn(async () => {
      throw new Error("boom");
    });
    const writeText = vi.fn(async () => {});
    expect(await shareLink(URL_, "Cube", { share, clipboard: { writeText } })).toBe("copied");
  });

  it("reports failure when nothing works", async () => {
    expect(await shareLink(URL_, "Cube", {})).toBe("failed");
    expect(await shareLink(URL_, "Cube", { clipboard: { writeText: async () => { throw new Error("denied"); } } })).toBe("failed");
  });
});
