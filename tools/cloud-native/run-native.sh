#!/usr/bin/env bash
set -euo pipefail
cd /workspace/.onboarding/budokai2-native
native_elf=${1:-/workspace/.onboarding/game/SLUS_207.79}
native_runner=/workspace/.onboarding/budokai2-native/budokai2-native
native_sysroot=/workspace/.onboarding/sysdeps/root
if [ ! -f "$native_elf" ] || [ ! -x "$native_runner" ]; then
    printf '%s\n' 'Native runner or game ELF is missing; run the saved native setup first.' >&2
    exit 1
fi
export LD_LIBRARY_PATH="$native_sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LIBGL_ALWAYS_SOFTWARE=1
export XDG_CACHE_HOME=/workspace/.onboarding/cache
export XDG_CONFIG_HOME=/workspace/.onboarding/config
export ALSA_CONFIG_PATH=/workspace/.onboarding/budokai2-native/asound.conf
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
"$native_runner" "$native_elf"
