import {
  AmbientLight,
  CanvasTexture,
  DirectionalLight,
  FrontSide,
  Mesh,
  MeshBasicMaterial,
  PerspectiveCamera,
  PlaneGeometry,
  PMREMGenerator,
  Scene,
  WebGLRenderer,
  type Texture,
} from "three";
import { RoomEnvironment } from "three/examples/jsm/environments/RoomEnvironment.js";
import { CAMERA_FOV_DEG } from "../interaction/framing.js";
import { LOOK } from "./look.js";
import type { TierSettings } from "./quality.js";

/// Everything that is WebGL set-up rather than puzzle: the renderer, the
/// camera, the lights, the room environment, the contact shadow
/// (docs/en/13-phase6-polish.md D5). Scenes know nothing of it; main.ts
/// adds a puzzle's group to `scene` and calls `render()`.

export interface Stage {
  readonly renderer: WebGLRenderer;
  readonly scene: Scene;
  readonly camera: PerspectiveCamera;
  /// How long the room environment took to pre-filter, or null if no tier
  /// has asked for it yet (spec D5: "time it").
  environmentBuildMs(): number | null;
  /// Switches the tier's runtime switches: environment light, ambient
  /// fallback, contact shadow. (Geometry switches need a scene rebuild.)
  applyTier(tier: TierSettings): void;
  /// Sets the render resolution multiplier.
  setPixelRatio(ratio: number): void;
  /// Resizes the canvas and the camera's aspect.
  resize(width: number, height: number): void;
  /// Places and sizes the contact shadow for a puzzle whose bounding
  /// sphere has this radius.
  setSubject(boundingRadius: number): void;
  render(): void;
  dispose(): void;
}

/// Edge of the square canvas the shadow gradient is drawn on. Soft, so
/// it needs very little resolution.
const SHADOW_TEXTURE_PX = 128;

/// A radial gradient, dark in the middle and fully transparent at the
/// edge, as a texture for the contact-shadow quad -- a stand-in for a
/// real shadow map at the cost of one quad (spec D5).
function createShadowTexture(): CanvasTexture {
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = SHADOW_TEXTURE_PX;
  const context = canvas.getContext("2d");
  if (context) {
    const half = SHADOW_TEXTURE_PX / 2;
    const gradient = context.createRadialGradient(half, half, 0, half, half, half);
    gradient.addColorStop(0, `rgba(0, 0, 0, ${LOOK.shadowOpacity})`);
    gradient.addColorStop(0.45, `rgba(0, 0, 0, ${LOOK.shadowOpacity * 0.45})`);
    gradient.addColorStop(1, "rgba(0, 0, 0, 0)");
    context.fillStyle = gradient;
    context.fillRect(0, 0, SHADOW_TEXTURE_PX, SHADOW_TEXTURE_PX);
  }
  return new CanvasTexture(canvas);
}

export function createStage(parent: HTMLElement, tier: TierSettings, pixelRatio: number): Stage {
  // A transparent canvas: the page behind it paints the background
  // gradient in CSS (spec D5), so the theme can change without a render.
  const renderer = new WebGLRenderer({ antialias: true, alpha: true, powerPreference: "high-performance" });
  renderer.setClearColor(0x000000, 0);
  renderer.toneMapping = LOOK.toneMapping;
  renderer.toneMappingExposure = LOOK.toneMappingExposure;
  renderer.setPixelRatio(pixelRatio);
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.domElement.className = "stage-canvas";
  parent.appendChild(renderer.domElement);

  const scene = new Scene();
  const camera = new PerspectiveCamera(CAMERA_FOV_DEG, window.innerWidth / window.innerHeight, 0.1, 200);

  const keyLight = new DirectionalLight(0xffffff, LOOK.keyLightIntensity);
  keyLight.position.set(...LOOK.keyLightPosition);
  scene.add(keyLight);
  // Stands in for the environment on the `low` tier, which skips it.
  const ambientLight = new AmbientLight(0xffffff, LOOK.ambientIntensity);
  scene.add(ambientLight);

  const shadowTexture = createShadowTexture();
  const shadowMaterial = new MeshBasicMaterial({
    map: shadowTexture,
    transparent: true,
    depthWrite: false,
    toneMapped: false,
    // FrontSide: seen from below the plane is culled, so orbiting under
    // the puzzle never draws the shadow across it.
    side: FrontSide,
  });
  const shadow = new Mesh(new PlaneGeometry(1, 1), shadowMaterial);
  shadow.rotation.x = -Math.PI / 2;
  shadow.renderOrder = -1;
  scene.add(shadow);

  let environment: Texture | null = null;
  let environmentMs: number | null = null;
  /// Pre-filters the room environment on first use and keeps it.
  function ensureEnvironment(): Texture {
    if (environment) return environment;
    const started = performance.now();
    const generator = new PMREMGenerator(renderer);
    const room = new RoomEnvironment();
    environment = generator.fromScene(room, 0.04).texture;
    room.dispose();
    generator.dispose();
    environmentMs = performance.now() - started;
    return environment;
  }

  function applyTier(settings: TierSettings): void {
    scene.environment = settings.environment ? ensureEnvironment() : null;
    scene.environmentIntensity = LOOK.environmentIntensity;
    ambientLight.visible = !settings.environment;
    shadow.visible = settings.contactShadow;
  }

  applyTier(tier);

  return {
    renderer,
    scene,
    camera,
    environmentBuildMs: () => environmentMs,
    applyTier,
    setPixelRatio: (ratio) => {
      renderer.setPixelRatio(ratio);
      renderer.setSize(window.innerWidth, window.innerHeight);
    },
    resize: (width, height) => {
      renderer.setSize(width, height);
      camera.aspect = width / height;
    },
    setSubject: (boundingRadius) => {
      shadow.position.y = -boundingRadius * LOOK.shadowDrop;
      shadow.scale.setScalar(boundingRadius * LOOK.shadowSpread);
    },
    render: () => renderer.render(scene, camera),
    dispose: () => {
      shadow.geometry.dispose();
      shadowMaterial.dispose();
      shadowTexture.dispose();
      environment?.dispose();
      renderer.dispose();
      renderer.domElement.remove();
    },
  };
}
