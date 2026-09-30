#!/usr/bin/env bash
# Fake `avdmanager create avd -n NAME -k PACKAGE -d DEVICE`.
AVD_HOME="${ANDROID_AVD_HOME:?ANDROID_AVD_HOME is not set}"
[ "${1:-}" = create ] && [ "${2:-}" = avd ] || { echo "unsupported" >&2; exit 1; }
shift 2
name=""; pkg=""; dev=""
while [ $# -gt 0 ]; do
  case "$1" in -n) name="$2"; shift ;; -k) pkg="$2"; shift ;; -d) dev="$2"; shift ;; esac
  shift
done
read -r _answer || true    # "Do you wish to create a custom hardware profile? [no]"
[ -n "$name" ] && [ -n "$pkg" ] || { echo "Error: missing -n or -k" >&2; exit 1; }
IFS=';' read -r _ platform tag abi <<<"$pkg"
[ -n "$abi" ] || { echo "Error: package path '$pkg' is not valid" >&2; exit 1; }
mkdir -p "$AVD_HOME/$name.avd"
printf 'avd.ini.encoding=UTF-8\npath=%s/%s.avd\ntarget=%s\n' "$AVD_HOME" "$name" "$platform" > "$AVD_HOME/$name.ini"
printf 'AvdId=%s\nabi.type=%s\nimage.sysdir.1=system-images/%s/%s/%s/\ntag.id=%s\nhw.device.name=%s\n' "$name" "$abi" "$platform" "$tag" "$abi" "$tag" "$dev" > "$AVD_HOME/$name.avd/config.ini"
echo "Created AVD $name"
