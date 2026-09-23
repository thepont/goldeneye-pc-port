#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "usage: run_headless_gl.sh Xvfb command [args...]" >&2
    exit 2
fi

xvfb_binary="$1"
shift
if [[ ! -x "${xvfb_binary}" ]]; then
    echo "SKIP: Xvfb is unavailable: ${xvfb_binary}" >&2
    exit 77
fi

task_tmp_root="${TMPDIR:-/tmp}"
export TMPDIR="${task_tmp_root}"
task_tmp_dir="$(mktemp -d "${task_tmp_root}/ge007-xvfb.XXXXXX")"
error_file="${task_tmp_dir}/xvfb.log"
xvfb_pid=""

cleanup()
{
    if [[ -n "${xvfb_pid}" ]] && kill -0 "${xvfb_pid}" 2>/dev/null; then
        kill "${xvfb_pid}" 2>/dev/null || true
        wait "${xvfb_pid}" 2>/dev/null || true
    fi
    rm -rf -- "${task_tmp_dir}"
}
trap cleanup EXIT

display_number=""
ready=0
# Keep the test display in a private high-number range. Desktop X/Wayland
# bridges commonly occupy :0/:1, and stale lock files there are not ours to
# remove. The preflight check plus retry handles another test choosing the
# same high display concurrently.
display_seed=$((10000 + ($$ % 10000)))
for attempt in {0..99}; do
    candidate=$((display_seed + attempt))
    if [[ -e "/tmp/.X${candidate}-lock" ||
          -S "/tmp/.X11-unix/X${candidate}" ]]; then
        continue
    fi
    "${xvfb_binary}" ":${candidate}" -screen 0 64x64x24 -ac \
        -nolisten tcp >>"${error_file}" 2>&1 &
    xvfb_pid=$!
    for _ in {1..40}; do
        if [[ -S "/tmp/.X11-unix/X${candidate}" ]] &&
           kill -0 "${xvfb_pid}" 2>/dev/null; then
            ready=1
            display_number="${candidate}"
            break
        fi
        if ! kill -0 "${xvfb_pid}" 2>/dev/null; then
            break
        fi
        sleep 0.05
    done
    if (( ready )); then
        break
    fi
    kill "${xvfb_pid}" 2>/dev/null || true
    wait "${xvfb_pid}" 2>/dev/null || true
    xvfb_pid=""
done

if (( ! ready )); then
    echo "SKIP: Xvfb could not provide a display" >&2
    sed -n '1,80p' "${error_file}" >&2 || true
    exit 77
fi

export DISPLAY=":${display_number}"
"$@"
