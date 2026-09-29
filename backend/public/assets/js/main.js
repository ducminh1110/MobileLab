// MobileLab dashboard entry point: wires the modules together and starts the data layer.

import { state, invalidate } from "./core/state.js";
import { onAuthRequired } from "./core/api.js";
import { start } from "./core/sync.js";
import { hooks } from "./core/actions.js";
import { initDispatch } from "./ui/dispatch.js";
import { initShell } from "./ui/shell.js";
import { initToolbar } from "./ui/toolbar.js";
import { initNavigator } from "./ui/navigator.js";
import { initJumpbar } from "./ui/jumpbar.js";
import { initEditor } from "./ui/editor.js";
import { initInspector } from "./ui/inspector.js";
import { initDebug } from "./ui/debug.js";
import { initSheets, openSheet } from "./ui/sheets.js";
import { initQuick } from "./ui/quick.js";
import { initToasts } from "./ui/toasts.js";
import { startTicker } from "./ui/ticker.js";

function boot() {
  initDispatch();
  initShell();
  initToasts();
  initToolbar();
  initNavigator();
  initJumpbar();
  initEditor();
  initInspector();
  initDebug();
  initSheets();
  initQuick();
  startTicker();

  onAuthRequired((message) => {
    if (state.ui.sheet?.name === "auth") return;
    openSheet("auth", { message: state.auth === "required" && message && !/Missing/.test(message) ? message : "" });
  });
  hooks.sheet = openSheet;

  invalidate("all");
  void start();
}

if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", boot);
else boot();
