#!/usr/bin/env bash
set -euo pipefail
native_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$native_source/paths.sh"
cd "$native_base/budokai2-native"
if [ "$#" -ne 1 ]; then
    printf '%s\n' 'Usage: bash tools/native/run-native.sh /path/to/Budokai-2-USA.iso' >&2
    exit 2
fi
native_iso=$1
native_runner=$native_base/budokai2-native/budokai2-native
native_sysroot=$native_base/sysdeps/root
if [ ! -f "$native_iso" ] || [ ! -x "$native_runner" ]; then
    printf '%s\n' 'The original game ISO and a built native runner are required.' >&2
    exit 1
fi
export LD_LIBRARY_PATH="$native_sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LIBGL_ALWAYS_SOFTWARE=1
export XDG_CACHE_HOME=$native_base/cache
export XDG_CONFIG_HOME=$native_base/config
export ALSA_CONFIG_PATH=$native_source/asound.conf
mkdir -p "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME"
native_display_dir=$(mktemp -d /tmp/openkai2-display.XXXXXX)
native_xvfb_pid=
native_cleanup() {
    native_status=$?
    trap - EXIT
    if [ -n "$native_xvfb_pid" ]; then
        kill "$native_xvfb_pid" 2>/dev/null || true
        wait "$native_xvfb_pid" 2>/dev/null || true
    fi
    rm -rf "$native_display_dir"
    exit "$native_status"
}
trap native_cleanup EXIT
trap 'exit 143' TERM
trap 'exit 130' INT
"$native_sysroot/usr/bin/Xvfb" -displayfd 3 -screen 0 1280x720x24 -nolisten tcp \
    3>"$native_display_dir/display" >"$native_display_dir/xvfb.log" 2>&1 &
native_xvfb_pid=$!
for native_attempt in {1..100}; do
    if [ -s "$native_display_dir/display" ]; then break; fi
    if ! kill -0 "$native_xvfb_pid" 2>/dev/null; then
        cat "$native_display_dir/xvfb.log" >&2
        exit 1
    fi
    sleep 0.1
done
if [ ! -s "$native_display_dir/display" ]; then
    printf '%s\n' 'Virtual display did not become available.' >&2
    exit 1
fi
export DISPLAY=":$(cat "$native_display_dir/display")"
xdpyinfo -display "$DISPLAY" > /dev/null
"$native_runner" "$native_iso"
