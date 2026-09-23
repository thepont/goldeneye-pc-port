#!/usr/bin/env bash
set -u

binary="${1:?usage: run_wayland_smoke.sh /path/to/ge007.x86_64}"

if [[ "${SDL_VIDEODRIVER:-wayland}" != "wayland" ]]; then
    echo "SKIP: SDL_VIDEODRIVER is not wayland"
    exit 77
fi
if [[ -z "${WAYLAND_DISPLAY:-}" || -z "${XDG_RUNTIME_DIR:-}" ]]; then
    echo "SKIP: Wayland environment is unavailable"
    exit 77
fi
if [[ ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo "SKIP: Wayland socket is unavailable"
    exit 77
fi

log_file="$(mktemp "${TMPDIR:-/tmp}/ge007-wayland-smoke.XXXXXX.log")"
cleanup() {
    rm -f "${log_file}"
}
trap cleanup EXIT

status=0
timeout --signal=TERM --kill-after=2s 8s \
    env SDL_VIDEODRIVER=wayland \
        SDL_AUDIODRIVER=dummy \
        GE_RENDER_MODE=0 \
        GE_START_HIDDEN=1 \
        "${binary}" >"${log_file}" 2>&1 || status=$?

# A launch that reaches only frames 1, 2, or 3 is still inside the fragile
# startup/presentation window. The binary may be terminated by timeout after
# proving that it reached the fourth frame.
if ! rg -q 'frame [4-9][0-9]* rendered' "${log_file}"; then
    echo "FAIL: Wayland launch did not render beyond the startup window (status=${status})"
    cat "${log_file}"
    exit 1
fi

echo "PASS: Wayland launch rendered beyond frame 3"
exit 0
