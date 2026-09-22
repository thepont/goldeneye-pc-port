#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${GE_BDD_BINARY:-${ROOT_DIR}/build-pc/ge007.x86_64}"

if [[ ! -x "${BINARY}" ]]; then
    echo "SKIP: PC BDD binary is missing: ${BINARY}"
    exit 77
fi

BDD_DISPLAY="${GE_BDD_DISPLAY:-${DISPLAY:-}}"
if [[ -z "${BDD_DISPLAY}" ]]; then
    if [[ -S /tmp/.X11-unix/X1 ]]; then
        BDD_DISPLAY=":1"
    else
        echo "SKIP: PC BDD requires an X11/XWayland display"
        exit 77
    fi
fi

BDD_RUNTIME="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
if [[ ! -d "${BDD_RUNTIME}" ]]; then
    echo "SKIP: PC BDD runtime directory is missing: ${BDD_RUNTIME}"
    exit 77
fi

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ge007-bdd.XXXXXX")"
trap 'rm -rf "${TMP_DIR}"' EXIT

export DISPLAY="${BDD_DISPLAY}"
export XDG_RUNTIME_DIR="${BDD_RUNTIME}"
export SDL_VIDEODRIVER=x11

fail_case() {
    local name="$1"
    local log="$2"
    echo "FAIL: ${name}"
    sed -n '1,220p' "${log}"
    exit 1
}

assert_distinct_positions() {
    local name="$1"
    local log="$2"

    if ! awk '
        /STAGE_PLAYER: player=0/ { first = $3 }
        /STAGE_PLAYER: player=1/ { second = $3 }
        END {
            if (first == "" || second == "" || first == second) {
                exit 1
            }
        }
    ' "${log}"; then
        echo "FAIL: ${name} did not prove distinct player positions"
        rg -n 'STAGE_PLAYER' "${log}" || true
        exit 1
    fi
}

assert_split_render_viewports() {
    local name="$1"
    local log="$2"
    local top_rect
    local bottom_rect

    top_rect="$( (rg -F 'GE_VIEWPORT_TRACE:' "${log}" || true) \
        | (rg -F 'logical=0,10,320,109' || true) \
        | sed -n '1p' | sed -E 's/.* gl=//')"
    bottom_rect="$( (rg -F 'GE_VIEWPORT_TRACE:' "${log}" || true) \
        | (rg -F 'logical=0,121,320,109' || true) \
        | sed -n '1p' | sed -E 's/.* gl=//')"

    if [[ -z "${top_rect}" || -z "${bottom_rect}" ]]; then
        echo "FAIL: ${name} did not expose both renderer viewport rectangles"
        rg -n 'GE_VIEWPORT_TRACE' "${log}" || true
        exit 1
    fi

    local top_x top_y top_w top_h
    local bottom_x bottom_y bottom_w bottom_h
    IFS=',' read -r top_x top_y top_w top_h <<<"${top_rect}"
    IFS=',' read -r bottom_x bottom_y bottom_w bottom_h <<<"${bottom_rect}"

    if ! [[ "${top_x}" =~ ^-?[0-9]+$ && "${top_y}" =~ ^-?[0-9]+$ &&
        "${top_w}" =~ ^[0-9]+$ && "${top_h}" =~ ^[0-9]+$ &&
        "${bottom_x}" =~ ^-?[0-9]+$ && "${bottom_y}" =~ ^-?[0-9]+$ &&
        "${bottom_w}" =~ ^[0-9]+$ && "${bottom_h}" =~ ^[0-9]+$ ]]; then
        echo "FAIL: ${name} emitted malformed renderer viewport rectangles"
        printf 'top=%s bottom=%s\n' "${top_rect}" "${bottom_rect}"
        exit 1
    fi

    local top_end=$((top_y + top_h))
    local bottom_end=$((bottom_y + bottom_h))
    if (( top_w <= 0 || top_h <= 0 || bottom_w <= 0 || bottom_h <= 0 ||
        (top_y < bottom_end && bottom_y < top_end) )); then
        echo "FAIL: ${name} rendered the two players into overlapping rectangles"
        printf 'top=%s bottom=%s\n' "${top_rect}" "${bottom_rect}"
        exit 1
    fi
}

assert_sight_drawn_while_aiming() {
    local name="$1"
    local log="$2"

    if ! rg -q -e 'GE_SIGHT_TRACE:.*aiming=1.*drawn=1' "${log}"; then
        echo "FAIL: ${name} did not draw the reticle while the aim button was held"
        rg -n 'GE_SIGHT_TRACE|GE_INPUTLOG cont0' "${log}" || true
        exit 1
    fi
}

run_case() {
    local name="$1"
    shift
    local -a patterns=()

    while [[ "$#" -gt 0 && "$1" != "--" ]]; do
        patterns+=("$1")
        shift
    done
    if [[ "$#" -eq 0 ]]; then
        echo "FAIL: ${name} has no command"
        exit 1
    fi
    shift

    local log="${TMP_DIR}/${name}.log"
    set +e
    env "$@" >"${log}" 2>&1
    local status=$?
    set -e

    if [[ "${status}" -ne 0 ]]; then
        echo "FAIL: ${name} exited ${status}"
        sed -n '1,220p' "${log}"
        exit 1
    fi

    if rg -n -e 'FATAL|Crashed|SIGSEGV|AddressSanitizer|stack smashing' "${log}"; then
        fail_case "${name} reported a crash" "${log}"
    fi

    local pattern
    for pattern in "${patterns[@]}"; do
        if ! rg -F -q -- "${pattern}" "${log}"; then
            echo "FAIL: ${name} missing: ${pattern}"
            sed -n '1,220p' "${log}"
            exit 1
        fi
    done

    echo "PASS: ${name}"
}

common_timeout=(timeout 60s "${BINARY}")
# Rendering probes intentionally stay alive long enough to submit real display
# lists. A timeout is an expected harness boundary here; assertions below must
# still prove that both renderer rectangles were observed and non-overlapping.
render_probe=(bash -c 'timeout -k 2s 8s "$@"; status=$?; if [[ "${status}" -eq 124 || "${status}" -eq 137 ]]; then exit 0; fi; exit "${status}"' ge007-render "${BINARY}")
menu_render_probe=(bash -c 'timeout -k 2s 35s "$@"; status=$?; if [[ "${status}" -eq 124 || "${status}" -eq 137 ]]; then exit 0; fi; exit "${status}"' ge007-menu-render "${BINARY}")
coop_menu_script='60:SDOWN;120:SNONE,A;240:A;400:A;560:A;720:A;880:A;1040:A;1200:A;1360:A;1520:A'
aim_hold_script='0:R;6:R;12:R;18:R;24:R;30:R;36:R;42:R;48:R;54:R;60:R;66:R;72:R;78:R;84:R;90:R;96:R;102:R;108:R;114:R;120:R;126:R;132:R;138:R;144:R;150:R;156:R;162:R;168:R;174:R;180:R;186:R;192:R;198:R;204:R;210:R;216:R;222:R;228:R;234:R;240:R'
run_case solo_launch \
    'STAGE_SMOKE: mode=0 stage=33 players=1 viewports=1' \
    'STAGE_PLAYER: player=0' \
    -- \
    GE_FAKE_CONTROLLERS=1 \
    GE_STAGE_SMOKE=1 \
    GE_STAGE_SMOKE_EXIT=1 \
    "${common_timeout[@]}" -level_33

run_case multiplayer_launch \
    'STAGE_SMOKE: mode=1' \
    'STAGE_SMOKE: mode=1 stage=38 players=2 viewports=2' \
    'VIEWPORT_SMOKE: player=0 rect=0,10,320,109 players=2' \
    'VIEWPORT_SMOKE: player=1 rect=0,121,320,109 players=2' \
    -- \
    GE_FAKE_CONTROLLERS=2 \
    GE_RESUME_MODE=multi \
    GE_STAGE_SMOKE=1 \
    GE_VIEWPORT_SMOKE=1 \
    GE_VIEWPORT_TRACE=1 \
    "${render_probe[@]}" -level_38
assert_distinct_positions multiplayer_launch "${TMP_DIR}/multiplayer_launch.log"
assert_split_render_viewports multiplayer_launch "${TMP_DIR}/multiplayer_launch.log"

run_case aim_reticle \
    'STAGE_SMOKE: mode=0 stage=33 players=1 viewports=1' \
    -- \
    GE_FAKE_CONTROLLERS=1 \
    GE_RESUME_MODE=solo \
    GE_INPUTSCRIPT="${aim_hold_script}" \
    GE_SIGHT_TRACE=1 \
    GE_STAGE_SMOKE=1 \
    "${render_probe[@]}" -level_33
assert_sight_drawn_while_aiming aim_reticle "${TMP_DIR}/aim_reticle.log"

run_case coop_menu \
    'MENU_MP_CHAR_SELECT' \
    'MENU_MISSION_SELECT' \
    'MENU_RUN_STAGE' \
    'STAGE_SMOKE: mode=3 stage=33 players=2 viewports=2' \
    'COOP_SPAWN_PADS: player=0 count=2' \
    'COOP_SPAWN_PADS: player=1 count=2' \
    'VIEWPORT_SMOKE: player=0 rect=0,10,320,109 players=2' \
    'VIEWPORT_SMOKE: player=1 rect=0,121,320,109 players=2' \
    -- \
    GE_FAKE_CONTROLLERS=2 \
    GE_STARTMENU=6 \
    GE_D243=1 \
    GE_INPUTSCRIPT="${coop_menu_script}" \
    GE_STAGE_SMOKE=1 \
    GE_COOP_SMOKE=1 \
    GE_COOP_SPAWN_LOG=1 \
    GE_VIEWPORT_SMOKE=1 \
    GE_VIEWPORT_TRACE=1 \
    "${menu_render_probe[@]}"
assert_distinct_positions coop_menu "${TMP_DIR}/coop_menu.log"
assert_split_render_viewports coop_menu "${TMP_DIR}/coop_menu.log"

run_case resume_solo \
    'STAGE_SMOKE: mode=0 stage=33 players=1 viewports=1' \
    'STAGE_PLAYER: player=0' \
    -- \
    GE_FAKE_CONTROLLERS=1 \
    GE_RESUME_MODE=solo \
    GE_STAGE_SMOKE=1 \
    GE_STAGE_SMOKE_EXIT=1 \
    "${common_timeout[@]}" -level_33

run_case resume_coop_two \
    'STAGE_SMOKE: mode=3 stage=33 players=2 viewports=2' \
    'VIEWPORT_SMOKE: player=0 rect=0,10,320,109 players=2' \
    'VIEWPORT_SMOKE: player=1 rect=0,121,320,109 players=2' \
    -- \
    GE_FAKE_CONTROLLERS=2 \
    GE_RESUME_MODE=coop \
    GE_STAGE_SMOKE=1 \
    GE_COOP_SPAWN_LOG=1 \
    GE_VIEWPORT_SMOKE=1 \
    GE_VIEWPORT_TRACE=1 \
    "${render_probe[@]}" -level_33
assert_distinct_positions resume_coop_two "${TMP_DIR}/resume_coop_two.log"
assert_split_render_viewports resume_coop_two "${TMP_DIR}/resume_coop_two.log"

run_case resume_coop_as_solo \
    'STAGE_SMOKE: mode=0 stage=33 players=1 viewports=1' \
    'STAGE_PLAYER: player=0' \
    -- \
    GE_FAKE_CONTROLLERS=1 \
    GE_RESUME_MODE=coop \
    GE_STAGE_SMOKE=1 \
    GE_STAGE_SMOKE_EXIT=1 \
    "${common_timeout[@]}" -level_33

echo "ALL PC BDD SCENARIOS PASSED"
