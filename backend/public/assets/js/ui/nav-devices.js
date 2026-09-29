// Devices navigator: Xcode's project tree. Simulators > one blue folder per runtime > devices, plus the
// experimental (simulated) VM root when the backend enables it. Source: GET /devices.

import { html } from "../core/util.js";
import { icon } from "../core/icons.js";
import { state, prefs, isExpanded } from "../core/state.js";
import { deviceGroups, deviceIcon, deviceModel, deviceRuntime, deviceState, isBooted, vmDevices } from "../core/models.js";
import { treeHtml } from "./tree.js";
import { skeleton, emptyState, errorState, deviceDot } from "./nav-common.js";

function matches(device, q) {
  if (!q) return true;
  const hay = `${device.name} ${deviceRuntime(device)} ${deviceModel(device)} ${device.simulatorUdid || ""} ${deviceState(device).label}`.toLowerCase();
  return q.split(/\s+/).every((part) => hay.includes(part));
}

function deviceRow(device, level) {
  const st = deviceState(device);
  const tip = [`${device.name}`, `${deviceRuntime(device)}, ${deviceModel(device)}`, st.label, device.lastError ? `Error: ${device.lastError}` : ""].filter(Boolean).join("\n");
  return {
    nav: "devices",
    key: `dev:${device.id}`,
    level,
    sel: { kind: "device", id: device.id },
    open: { kind: "device", id: device.id },
    ctx: "device",
    icon: icon(deviceIcon(device), "ic-16"),
    label: html`${device.name}${device.ephemeral ? html` <span class="tag" title="Created automatically for a run">auto</span>` : ""}`,
    trailing: deviceDot(device),
    title: tip
  };
}

export function devicesView() {
  if (!state.loaded.devices) return { html: skeleton(9), count: 0 };
  if (state.errors.devices && !state.devices.length) return { html: errorState("simulators", state.errors.devices), count: 0 };

  const q = state.ui.filter.devices.trim().toLowerCase();
  const filtering = !!q || prefs.onlyBooted;
  const keep = (d) => (!prefs.onlyBooted || isBooted(d)) && matches(d, q);
  const sims = state.devices.filter((d) => d.type !== "vm");
  const vmEnabled = !!state.caps?.vm?.enabled;

  if (!sims.length && !vmEnabled) {
    return {
      html: emptyState({ iconName: "iphone", title: "No simulators yet", text: "Create a simulator to run tests on it, or just press Run and MobileLab creates one on demand.", action: "new-simulator", actionLabel: "Create Simulator" }),
      count: 0
    };
  }

  const rows = [];
  let shown = 0;
  const rootOpen = filtering || isExpanded("devices", "root", true);
  rows.push({ nav: "devices", key: "root", level: 0, expandable: true, expanded: rootOpen, icon: icon("mark", "ic-16"), label: "Simulators", cls: "root" });
  if (rootOpen) {
    for (const group of deviceGroups()) {
      const kids = group.devices.filter(keep);
      if (!kids.length) continue;
      const key = `rt:${group.key}`;
      const open = filtering || isExpanded("devices", key, true);
      rows.push({ nav: "devices", key, level: 1, expandable: true, expanded: open, icon: icon("folder.fill", "ic-16"), label: group.name });
      if (open) for (const d of kids) { rows.push(deviceRow(d, 2)); shown += 1; }
      else shown += kids.length;
    }
    if (!sims.length) rows.push({ nav: "devices", key: "none", level: 1, disabled: true, dim: true, label: "No simulators yet" });
  }
  if (vmEnabled) {
    const vms = vmDevices().filter(keep);
    const open = filtering || isExpanded("devices", "vms", true);
    rows.push({ nav: "devices", key: "vms", level: 0, expandable: true, expanded: open, icon: icon("folder.fill", "ic-16"), label: "Experimental VMs (simulated)", cls: "root", title: "Simulated VMs never start a real VM and cannot run tests." });
    if (open) {
      for (const vm of vms) { rows.push(deviceRow(vm, 1)); shown += 1; }
      if (!vms.length) rows.push({ nav: "devices", key: "novm", level: 1, disabled: true, dim: true, label: filtering ? "No matching VMs" : "No VMs" });
    }
  }

  if (filtering && !shown) {
    return { html: emptyState({ iconName: "magnifyingglass", title: "No matching devices", text: q ? `Nothing matches “${state.ui.filter.devices.trim()}”.` : "No simulator is booted." }), count: 0 };
  }
  return { html: treeHtml(rows, { nav: "devices", label: "Devices" }), count: shown };
}

