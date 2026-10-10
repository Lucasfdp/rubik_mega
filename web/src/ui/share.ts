/// The Share button (docs/en/13-phase6-polish.md D10): the phone's own
/// share sheet where there is one, the clipboard elsewhere. What it did is
/// returned so the caller can say so; it never throws, because a user who
/// dismisses the share sheet has done nothing wrong.
export type ShareOutcome = "shared" | "copied" | "cancelled" | "failed";

/// The parts of `navigator` this uses, so a test can supply fakes.
export interface ShareEnvironment {
  share?: (data: { title: string; url: string }) => Promise<void>;
  canShare?: (data: { url: string }) => boolean;
  clipboard?: { writeText(text: string): Promise<void> };
}

export async function shareLink(url: string, title: string, env: ShareEnvironment = navigator as ShareEnvironment): Promise<ShareOutcome> {
  if (env.share && (env.canShare?.({ url }) ?? true)) {
    try {
      await env.share({ title, url });
      return "shared";
    } catch (error) {
      // AbortError: the user closed the sheet.
      if (error instanceof Error && error.name === "AbortError") return "cancelled";
      // Any other failure: fall through to the clipboard.
    }
  }
  if (env.clipboard) {
    try {
      await env.clipboard.writeText(url);
      return "copied";
    } catch {
      return "failed";
    }
  }
  return "failed";
}
