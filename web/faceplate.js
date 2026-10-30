// Browser-native SVG paint, sharing the artwork and calibration from PR #77.
// The emulator's Worker and framebuffer are independent of panel changes.
import pngLayout from "../gui/ui.json";
import pngUrl from "../gui/ui.png";
import svgLayout from "./panel/layout.json";
import svgUrl from "./panel/op1.svg?url";
import slate from "./panel/themes/slate.json";
import silver from "./panel/themes/silver.json";
import yellow from "./panel/themes/yellow.json";
import green from "./panel/themes/green.json";
import purple from "./panel/themes/purple.json";
import orange from "./panel/themes/orange.json";

const themes = { slate, silver, yellow, green, purple, orange };

export async function loadFaceplate(onLayout) {
  const image = document.getElementById("faceplate");
  const panel = document.getElementById("panel-style");
  const theme = document.getElementById("theme");
  const status = document.getElementById("panel-status");
  const params = new URLSearchParams(location.search);
  panel.value = params.get("ui") === "png" ? "png" : "svg";
  theme.value = Object.hasOwn(themes, params.get("theme")) ? params.get("theme") : "silver";
  let requested = 0;
  let source;
  let currentUrl;
  let currentLayout;

  async function prepare(kind, palette) {
    if (kind === "png") {
      const candidate = new Image();
      candidate.src = pngUrl;
      await candidate.decode();
      return { url: pngUrl, layout: pngLayout,
        size: { width: candidate.naturalWidth, height: candidate.naturalHeight } };
    }
    // Only our bundled artwork is parsed; no user HTML or remote SVG is inserted.
    source ??= fetch(svgUrl).then((response) => {
      if (!response.ok) throw new Error(`SVG: HTTP ${response.status}`);
      return response.text();
    });
    const doc = new DOMParser().parseFromString(await source, "image/svg+xml");
    if (doc.querySelector("parsererror")) throw new Error("Invalid panel SVG");
    const style = doc.createElementNS("http://www.w3.org/2000/svg", "style");
    style.textContent = Object.entries(themes[palette].colors)
      .map(([role, color]) => `.op1-${role} { fill: ${color}; }`).join("\n");
    doc.documentElement.prepend(style);
    const url = URL.createObjectURL(new Blob([new XMLSerializer().serializeToString(doc)], { type: "image/svg+xml" }));
    try {
      const candidate = new Image();
      candidate.src = url;
      await candidate.decode();
      return { url, layout: svgLayout, size: svgLayout };
    } catch (error) {
      URL.revokeObjectURL(url);
      throw error;
    }
  }

  async function select() {
    const request = ++requested;
    const kind = panel.value, palette = theme.value;
    theme.disabled = kind === "png";
    status.textContent = "";
    let prepared;
    try {
      prepared = await prepare(kind, palette);
    } catch (error) {
      if (request !== requested) return;
      console.warn("Panel could not be loaded:", error);
      if (kind !== "svg") {
        status.textContent = "The panel could not be loaded. Reload to try again.";
        return;
      }
      // Keep image and input calibration together, including on a failed decode.
      panel.value = "png";
      theme.disabled = true;
      status.textContent = "SVG unavailable; using the original PNG panel.";
      prepared = await prepare("png");
    }
    if (request !== requested) {
      if (prepared.url.startsWith("blob:")) URL.revokeObjectURL(prepared.url);
      return;
    }
    if (prepared.layout !== currentLayout) {
      onLayout(prepared.layout, prepared.size);
      currentLayout = prepared.layout;
    }
    image.src = prepared.url;
    document.getElementById("device").dataset.panel = panel.value;
    if (currentUrl?.startsWith("blob:")) URL.revokeObjectURL(currentUrl);
    currentUrl = prepared.url;
  }

  panel.addEventListener("change", () => select().catch((error) => {
    console.warn(error);
    status.textContent = "The panel could not be loaded. Reload to try again.";
  }));
  theme.addEventListener("change", () => panel.dispatchEvent(new Event("change")));
  await select();
}
