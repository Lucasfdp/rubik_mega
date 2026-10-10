/// The motion of the "you solved it" moment (docs/en/13-phase6-polish.md
/// D9): one slow turn of the view and a light burst of confetti. Pure
/// numbers, so it is tested; ui/celebration.ts draws it.

/// How long the turn of the view takes, in seconds: slow enough to be
/// calm, short enough not to be in the way of the next scramble.
export const SPIN_SEC = 1.8;

/// How far through the spin (0..1) `elapsedSec` is, eased in and out so it
/// starts and ends at rest.
export function spinProgress(elapsedSec: number, durationSec: number = SPIN_SEC): number {
  const t = Math.min(Math.max(elapsedSec / durationSec, 0), 1);
  return t < 0.5 ? 4 * t * t * t : 1 - (-2 * t + 2) ** 3 / 2;
}

export interface Particle {
  x: number;
  y: number;
  vx: number;
  vy: number;
  size: number;
  colour: string;
  /// Seconds it has been alive.
  age: number;
  /// Seconds it lives for.
  life: number;
}

const GRAVITY_PX_PER_SEC2 = 900;
/// Air slows sideways drift, so the burst opens and then falls.
const DRAG_PER_SEC = 1.6;

/// A burst from the middle-top of a `width` x `height` area. `random` is
/// injected (0..1) so a test is repeatable.
export function makeConfetti(count: number, colours: readonly string[], width: number, height: number, random: () => number): Particle[] {
  const originX = width / 2;
  const originY = height * 0.35;
  return Array.from({ length: count }, (): Particle => {
    // Mostly upwards, fanned about 120 degrees.
    const angle = -Math.PI / 2 + (random() - 0.5) * (Math.PI * 2 / 3);
    const speed = height * (0.5 + random() * 0.7);
    return {
      x: originX,
      y: originY,
      vx: Math.cos(angle) * speed,
      vy: Math.sin(angle) * speed,
      size: 5 + random() * 5,
      colour: colours[Math.floor(random() * colours.length)]!,
      age: 0,
      life: 1.4 + random() * 0.9,
    };
  });
}

/// Moves every particle on by `dtSec` and returns the ones still alive.
export function stepConfetti(particles: readonly Particle[], dtSec: number): Particle[] {
  const keep: Particle[] = [];
  for (const p of particles) {
    const age = p.age + dtSec;
    if (age >= p.life) continue;
    const slow = Math.max(1 - DRAG_PER_SEC * dtSec, 0);
    const vx = p.vx * slow;
    const vy = p.vy * slow + GRAVITY_PX_PER_SEC2 * dtSec;
    keep.push({ ...p, age, vx, vy, x: p.x + vx * dtSec, y: p.y + vy * dtSec });
  }
  return keep;
}
