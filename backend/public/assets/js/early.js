// Applies the stored appearance (theme, accent, text size, reduced motion) before the first paint. Loaded from <head> as a
// render-blocking module; main.js and Settings keep it current afterwards.

import { prefs } from "./core/state.js";
import { applyAppearance } from "./core/appearance.js";

applyAppearance(document.documentElement, prefs, window.matchMedia("(prefers-reduced-motion: reduce)").matches);
