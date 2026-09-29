// The icon set: drawn for MobileLab in the SF Symbols idiom (24px grid, 1.6 stroke, round caps and joins,
// monochrome via currentColor). Names follow docs/design/xcode-interface.md section 4. Everything is inline SVG,
// installed once as a <symbol> sprite; `icon(name)` returns a <use> reference. No icon fonts, no emoji.
import { raw } from "./util.js";

function gearPath() {
  const teeth = 8;
  const outer = 9.6;
  const inner = 7.4;
  const pts = [];
  for (let i = 0; i < teeth; i += 1) {
    const base = (i / teeth) * Math.PI * 2 - Math.PI / 2;
    const half = Math.PI / teeth;
    const a = [base - half * 0.62, inner];
    const b = [base - half * 0.36, outer];
    const c = [base + half * 0.36, outer];
    const d = [base + half * 0.62, inner];
    pts.push(a, b, c, d);
  }
  return "M" + pts.map(([ang, r]) => `${(12 + Math.cos(ang) * r).toFixed(2)} ${(12 + Math.sin(ang) * r).toFixed(2)}`).join("L") + "Z";
}

const FILL = 'fill="currentColor"';

const ICONS = {
  "sidebar.left": '<rect x="3.5" y="5" width="17" height="14" rx="3.2"/><path d="M9.6 5v14"/><path d="M6.3 9h1M6.3 12h1M6.3 15h1" stroke-width="1.3"/>',
  "sidebar.right": '<rect x="3.5" y="5" width="17" height="14" rx="3.2"/><path d="M14.4 5v14"/><path d="M16.7 9h1M16.7 12h1M16.7 15h1" stroke-width="1.3"/>',
  "sidebar.bottom": '<rect x="3.5" y="5" width="17" height="14" rx="3.2"/><path d="M3.5 14.4h17"/><path d="M8 16.7h8" stroke-width="1.3"/>',
  "play.fill": `<path d="M8.2 5.6v12.8a.6.6 0 0 0 .9.5l10.2-6.4a.6.6 0 0 0 0-1L9.1 5.1a.6.6 0 0 0-.9.5z" ${FILL}/>`,
  "play": '<path d="M8.2 5.6v12.8a.6.6 0 0 0 .9.5l10.2-6.4a.6.6 0 0 0 0-1L9.1 5.1a.6.6 0 0 0-.9.5z"/>',
  "stop.fill": `<rect x="6" y="6" width="12" height="12" rx="2.6" ${FILL}/>`,
  "plus": '<path d="M12 5v14M5 12h14"/>',
  "minus": '<path d="M5 12h14"/>',
  "xmark": '<path d="M6.2 6.2l11.6 11.6M17.8 6.2L6.2 17.8"/>',
  "checkmark": '<path d="M5.2 12.8l4.4 4.4L18.8 7.4"/>',
  "chevron.right": '<path d="M9.4 5.4l6.6 6.6-6.6 6.6"/>',
  "chevron.left": '<path d="M14.6 5.4L8 12l6.6 6.6"/>',
  "chevron.down": '<path d="M5.4 9.4L12 16l6.6-6.6"/>',
  "chevron.up": '<path d="M5.4 14.6L12 8l6.6 6.6"/>',
  "chevron.up.chevron.down": '<path d="M8 9.4l4-4 4 4M8 14.6l4 4 4-4"/>',
  "triangle.right": `<path d="M8.5 6l8 6-8 6z" ${FILL} stroke-width="1.2"/>`,
  "triangle.down": `<path d="M6 8.5l6 8 6-8z" ${FILL} stroke-width="1.2"/>`,
  "folder.fill": '<path class="i-folder" d="M3 7.6A2.1 2.1 0 0 1 5.1 5.5h3.9c.6 0 1.1.2 1.5.7l.9 1.1c.3.4.8.6 1.3.6h6.2A2.1 2.1 0 0 1 21 10v7.4a2.1 2.1 0 0 1-2.1 2.1H5.1A2.1 2.1 0 0 1 3 17.4z" stroke-width="1"/>',
  "folder": '<path d="M3 7.6A2.1 2.1 0 0 1 5.1 5.5h3.9c.6 0 1.1.2 1.5.7l.9 1.1c.3.4.8.6 1.3.6h6.2A2.1 2.1 0 0 1 21 10v7.4a2.1 2.1 0 0 1-2.1 2.1H5.1A2.1 2.1 0 0 1 3 17.4z"/>',
  "iphone": '<rect x="6.8" y="2.6" width="10.4" height="18.8" rx="2.7"/><path d="M10.6 5.4h2.8M10.7 18.6h2.6" stroke-width="1.3"/>',
  "ipad": '<rect x="4.4" y="3" width="15.2" height="18" rx="2.6"/><path d="M10.7 18.2h2.6" stroke-width="1.3"/>',
  "cpu": '<rect x="6.8" y="6.8" width="10.4" height="10.4" rx="1.8"/><rect x="9.6" y="9.6" width="4.8" height="4.8" rx=".8" stroke-width="1.3"/><path d="M10 3.4v3.4M14 3.4v3.4M10 17.2v3.4M14 17.2v3.4M3.4 10h3.4M3.4 14h3.4M17.2 10h3.4M17.2 14h3.4"/>',
  "memorychip": '<rect x="3.4" y="6.6" width="17.2" height="10.2" rx="1.8"/><path d="M7 16.8v2.8M10.3 16.8v2.8M13.7 16.8v2.8M17 16.8v2.8M8 10.2h8" stroke-width="1.4"/>',
  "internaldrive": '<rect x="3.2" y="7.6" width="17.6" height="9.4" rx="2.6"/><path d="M6.6 12.3h5.2" stroke-width="1.5"/><path d="M16.2 12.3h.01" stroke-width="2.4"/>',
  "network": '<circle cx="12" cy="12" r="8.8"/><path d="M3.2 12h17.6M12 3.2c2.5 2.5 3.6 5.4 3.6 8.8S14.5 18.3 12 20.8C9.5 18.3 8.4 15.4 8.4 12S9.5 5.7 12 3.2z"/>',
  "gauge": '<path d="M4.2 17.6a8.6 8.6 0 1 1 15.6 0"/><path d="M12 13.4l3.6-4.2"/><path d="M12 13.4h.01" stroke-width="2.6"/>',
  "magnifyingglass": '<circle cx="10.6" cy="10.6" r="6.4"/><path d="M15.4 15.4l5 5"/>',
  "plus.magnifyingglass": '<circle cx="10.6" cy="10.6" r="6.4"/><path d="M15.4 15.4l5 5M10.6 8v5.2M8 10.6h5.2" />',
  "minus.magnifyingglass": '<circle cx="10.6" cy="10.6" r="6.4"/><path d="M15.4 15.4l5 5M8 10.6h5.2"/>',
  "equal.magnifyingglass": '<circle cx="10.6" cy="10.6" r="6.4"/><path d="M15.4 15.4l5 5M8.2 9.4h4.8M8.2 12h4.8"/>',
  "fit.magnifyingglass": '<circle cx="10.6" cy="10.6" r="6.4"/><path d="M15.4 15.4l5 5M8.2 11.6V8.2h3.4M13 9.6v3.4H9.6"/>',
  "exclamationmark.triangle": '<path d="M12 4.1l9 15.4H3z"/><path d="M12 10v4.4"/><path d="M12 17.1h.01" stroke-width="2.2"/>',
  "exclamationmark.triangle.fill": `<path d="M12 4.1l9 15.4H3z" ${FILL}/><path class="i-knock" d="M12 10v4.2"/><path class="i-knock" d="M12 17h.01" stroke-width="2.2"/>`,
  "exclamationmark.circle.fill": `<circle cx="12" cy="12" r="9" ${FILL} stroke="none"/><path class="i-knock" d="M12 7.4v5.4"/><path class="i-knock" d="M12 16.3h.01" stroke-width="2.2"/>`,
  "diamond": '<path d="M12 3.2L20.8 12 12 20.8 3.2 12z"/>',
  "checkmark.diamond": '<path d="M12 3.2L20.8 12 12 20.8 3.2 12z"/><path d="M8.4 12.2l2.6 2.6 4.8-5.2"/>',
  "checkmark.diamond.fill": `<path d="M12 3.2L20.8 12 12 20.8 3.2 12z" ${FILL}/><path class="i-knock" d="M8.4 12.2l2.6 2.6 4.8-5.2"/>`,
  "xmark.diamond.fill": `<path d="M12 3.2L20.8 12 12 20.8 3.2 12z" ${FILL}/><path class="i-knock" d="M9.3 9.3l5.4 5.4M14.7 9.3l-5.4 5.4"/>`,
  "minus.diamond": '<path d="M12 3.2L20.8 12 12 20.8 3.2 12z"/><path d="M8.8 12h6.4"/>',
  "circle.fill": `<circle cx="12" cy="12" r="6" ${FILL} stroke="none"/>`,
  "circle": '<circle cx="12" cy="12" r="6"/>',
  "checkmark.circle.fill": `<circle cx="12" cy="12" r="9" ${FILL} stroke="none"/><path class="i-knock" d="M8 12.3l2.8 2.8 5.2-5.6"/>`,
  "xmark.circle.fill": `<circle cx="12" cy="12" r="9" ${FILL} stroke="none"/><path class="i-knock" d="M9 9l6 6M15 9l-6 6"/>`,
  "doc.text": '<path d="M6.6 3.4h7.2l4.6 4.6v11a1.6 1.6 0 0 1-1.6 1.6H6.6A1.6 1.6 0 0 1 5 19V5a1.6 1.6 0 0 1 1.6-1.6z"/><path d="M13.6 3.6v4.8h4.6M8.6 12.6h6.8M8.6 16h6.8"/>',
  "doc.on.doc": '<rect x="8.4" y="8.4" width="11.6" height="12" rx="2.2"/><path d="M15.6 8.4V5.8a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8.6a2 2 0 0 0 2 2h2.4"/>',
  "gearshape": `<path d="${gearPath()}"/><circle cx="12" cy="12" r="3"/>`,
  "trash": '<path d="M4.4 7h15.2M9.6 7V4.9c0-.5.4-.9.9-.9h3c.5 0 .9.4.9.9V7M6.4 7l.8 11.9a1.6 1.6 0 0 0 1.6 1.5h6.4a1.6 1.6 0 0 0 1.6-1.5L17.6 7M10 11v6M14 11v6"/>',
  "camera": '<path d="M4 8.6A1.6 1.6 0 0 1 5.6 7h2l1.2-2h6.4l1.2 2h2A1.6 1.6 0 0 1 20 8.6v8.8a1.6 1.6 0 0 1-1.6 1.6H5.6A1.6 1.6 0 0 1 4 17.4z"/><circle cx="12" cy="12.8" r="3.3"/>',
  "arrow.clockwise": '<path d="M19.4 12.6A7.4 7.4 0 1 1 17 6.8"/><path d="M19.6 4.4v4.2h-4.2"/>',
  "arrow.down.circle": '<circle cx="12" cy="12" r="8.6"/><path d="M12 7.8v8.2M8.6 12.8L12 16.2l3.4-3.4"/>',
  "square.and.arrow.down": '<path d="M12 3.6v11.2M8.2 11.2L12 15l3.8-3.8"/><path d="M8 8.4H6.6A1.6 1.6 0 0 0 5 10v8.4a1.6 1.6 0 0 0 1.6 1.6h10.8a1.6 1.6 0 0 0 1.6-1.6V10a1.6 1.6 0 0 0-1.6-1.6H16"/>',
  "line.3.horizontal.decrease.circle": '<circle cx="12" cy="12" r="8.8"/><path d="M7.6 9.6h8.8M9.4 12.4h5.2M10.9 15.2h2.2"/>',
  "ellipsis.circle": '<circle cx="12" cy="12" r="8.8"/><path d="M8.2 12h.01M12 12h.01M15.8 12h.01" stroke-width="2.4"/>',
  "bolt.horizontal": `<path d="M3.6 13.2l9.4-7.4v4.7h7.4l-9.4 7.4v-4.7z" ${FILL} stroke-width="1.1"/>`,
  "bolt": `<path d="M13.4 3.2L6.4 13.4h5l-.8 7.4 7-10.2h-5z" ${FILL} stroke-width="1"/>`,
  "info.circle": '<circle cx="12" cy="12" r="8.8"/><path d="M12 11v5.4"/><path d="M12 7.9h.01" stroke-width="2.3"/>',
  "info.circle.fill": `<circle cx="12" cy="12" r="9" ${FILL} stroke="none"/><path class="i-knock" d="M12 11v5.2"/><path class="i-knock" d="M12 7.9h.01" stroke-width="2.3"/>`,
  "pause.circle": '<circle cx="12" cy="12" r="8.8"/><path d="M10 8.8v6.4M14 8.8v6.4"/>',
  "clock": '<circle cx="12" cy="12" r="8.8"/><path d="M12 7v5.2l3.4 2"/>',
  "questionmark.circle": '<circle cx="12" cy="12" r="8.8"/><path d="M9.6 9.6a2.5 2.5 0 1 1 3.6 2.2c-.9.5-1.3 1.1-1.3 2.1"/><path d="M11.9 16.9h.01" stroke-width="2.2"/>',
  "square.grid.2x2": '<rect x="3.6" y="3.6" width="7.2" height="7.2" rx="1.9"/><rect x="13.2" y="3.6" width="7.2" height="7.2" rx="1.9"/><rect x="3.6" y="13.2" width="7.2" height="7.2" rx="1.9"/><rect x="13.2" y="13.2" width="7.2" height="7.2" rx="1.9"/>',
  "arrow.left.arrow.right": '<path d="M4 8.6h15.6M15.8 4.8l3.8 3.8-3.8 3.8M20 15.4H4.4M8.2 11.6l-3.8 3.8 3.8 3.8"/>',
  "list.bullet.indent": '<path d="M9.6 6.6h10M9.6 12h10M9.6 17.4h10M4.4 6.6h.01M4.4 12h.01M4.4 17.4h.01" />',
  "text.alignleft": '<path d="M4 6.4h16M4 10.8h10.4M4 15.2h16M4 19.6h10.4"/>',
  "eye": '<path d="M2.8 12S6 5.8 12 5.8 21.2 12 21.2 12 18 18.2 12 18.2 2.8 12 2.8 12z"/><circle cx="12" cy="12" r="2.8"/>',
  "square.stack": '<path d="M12 4l8 4-8 4-8-4z"/><path d="M4 12l8 4 8-4M4 16l8 4 8-4"/>',
  "person.crop.square.fill": `<rect x="3.6" y="3.6" width="16.8" height="16.8" rx="3.6" ${FILL} stroke="none"/><circle class="i-knock" cx="12" cy="9.6" r="2.6" fill="#fff" stroke="none"/><path class="i-knock" d="M6.8 18.2c.6-2.6 2.6-3.8 5.2-3.8s4.6 1.2 5.2 3.8" fill="#fff" stroke="none"/>`,
  "tag.fill": `<path d="M4.4 8.2a2.6 2.6 0 0 1 2.6-2.6h8.6c.7 0 1.3.3 1.7.8l3 3.6a1.1 1.1 0 0 1 0 1.4l-3 3.6a2.2 2.2 0 0 1-1.7.8H7a2.6 2.6 0 0 1-2.6-2.6z" ${FILL} stroke="none"/>`,
  "tag": '<path d="M4.4 8.2a2.6 2.6 0 0 1 2.6-2.6h8.6c.7 0 1.3.3 1.7.8l3 3.6a1.1 1.1 0 0 1 0 1.4l-3 3.6a2.2 2.2 0 0 1-1.7.8H7a2.6 2.6 0 0 1-2.6-2.6z"/>',
  "list.bullet": '<path d="M9 6.6h11M9 12h11M9 17.4h11M4.4 6.6h.01M4.4 12h.01M4.4 17.4h.01" stroke-width="2"/>',
  "rectangle.stack": '<rect x="4" y="9" width="16" height="10.6" rx="2.4"/><path d="M7 5.6h10"/>',
  "tray.full": '<path d="M4 13.4l2.2-7a1.6 1.6 0 0 1 1.5-1.1h8.6a1.6 1.6 0 0 1 1.5 1.1l2.2 7"/><path d="M4 13.4v4.2A1.8 1.8 0 0 0 5.8 19.4h12.4A1.8 1.8 0 0 0 20 17.6v-4.2h-4.6a3.4 3.4 0 0 1-6.8 0z"/>',
  "person": '<circle cx="12" cy="8.4" r="3.6"/><path d="M5 20c.7-3.8 3.4-5.6 7-5.6s6.3 1.8 7 5.6"/>',
  "link": '<path d="M10.2 13.8a3.6 3.6 0 0 0 5.1 0l3-3a3.6 3.6 0 0 0-5.1-5.1l-1 1"/><path d="M13.8 10.2a3.6 3.6 0 0 0-5.1 0l-3 3a3.6 3.6 0 0 0 5.1 5.1l1-1"/>',
  "stethoscope": '<path d="M6 3.8v5.6a4 4 0 0 0 8 0V3.8"/><path d="M10 13.4v1.2a4.4 4.4 0 0 0 8.8 0v-2.2"/><circle cx="18.8" cy="10.6" r="1.8"/>',
  "mark": '<rect class="i-mark-bg" x="1" y="1" width="22" height="22" rx="5.8"/><rect class="i-mark-fg" x="8.4" y="4.4" width="7.2" height="15.2" rx="2" stroke-width="1.7"/><path class="i-mark-fg" d="M10.8 7h2.4" stroke-width="1.5"/>',
  "breakpoint.fill": `<path d="M4.6 7.4A2.4 2.4 0 0 1 7 5h8.6c.7 0 1.4.3 1.8.9l3.2 4.3a2.4 2.4 0 0 1 0 2.9l-3.2 4.3c-.4.6-1.1.9-1.8.9H7a2.4 2.4 0 0 1-2.4-2.4z" ${FILL} stroke="none"/>`,
  "arrow.uturn.left": '<path d="M9 6.4L4.8 10.6 9 14.8"/><path d="M4.8 10.6h9.6a4.8 4.8 0 0 1 0 9.6H10"/>',
  "thread": `<circle cx="12" cy="12" r="9" ${FILL} stroke="none"/><path class="i-knock" d="M9.4 8.2v7.6M12 7.4v9.2M14.6 8.2v7.6" stroke-width="1.5"/>`,
  "list.dash": '<path d="M4.6 7h14.8M4.6 12h14.8M4.6 17h14.8"/>'
};

export const iconNames = Object.keys(ICONS);

export function spriteMarkup() {
  return (
    '<svg xmlns="http://www.w3.org/2000/svg" width="0" height="0" focusable="false">' +
    iconNames.map((name) => `<symbol id="i-${name.replace(/\./g, "-")}" viewBox="0 0 24 24">${ICONS[name]}</symbol>`).join("") +
    "</svg>"
  );
}

export function installSprite(target) {
  target.innerHTML = spriteMarkup();
}

/** Returns SVG markup for an icon. `cls` adds size / colour classes (for example "ic-18 c-accent"). */
export function icon(name, cls = "") {
  if (!ICONS[name]) return raw("");
  return raw(`<svg class="ic${cls ? " " + cls : ""}" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><use href="#i-${name.replace(/\./g, "-")}"/></svg>`);
}
