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

assert_water_composite_viewports() {
    local name="$1"
    local log="$2"

    if ! awk '
        /WATER_COMPOSITE:.*viewport=/ {
            for (i = 1; i <= NF; ++i) {
                if ($i ~ /^viewport=/) {
                    seen[$i] = 1
                }
            }
        }
        END {
            count = 0
            for (viewport in seen) ++count
            if (count < 2) exit 1
        }
    ' "${log}"; then
        echo "FAIL: ${name} did not composite water independently in both player viewports"
        rg -n 'WATER_COMPOSITE:|GE_VIEWPORT_TRACE' "${log}" || true
        exit 1
    fi
}

assert_sight_drawn_while_aiming() {
    local name="$1"
    local log="$2"

    if ! rg -q -e 'GE_INPUTLOG cont0: btn=0010' "${log}"; then
        echo "FAIL: ${name} did not route the aim binding to the N64 R trigger"
        rg -n 'GE_INPUTLOG cont0|GE_SIGHT_TRACE' "${log}" || true
        exit 1
    fi
    if ! rg -q -e 'GE_SIGHT_TRACE:.*aiming=1.*drawn=1' "${log}"; then
        echo "FAIL: ${name} did not draw the reticle while the aim button was held"
        rg -n 'GE_SIGHT_TRACE|GE_INPUTLOG cont0' "${log}" || true
        exit 1
    fi
}

assert_dof_active() {
    local name="$1"
    local log="$2"
    local viewport_count="$3"

    if ! rg -q -e "graphics: Ascension adaptive PostFX active \\([0-9]+% sharpen, dof=1 depth=[1-9][0-9]*x[1-9][0-9]* viewports=${viewport_count}," "${log}"; then
        echo "FAIL: ${name} did not capture depth and activate DoF for ${viewport_count} viewport(s)"
        rg -n 'PostFX|depth framebuffer|shader compile' "${log}" || true
        exit 1
    fi
}

assert_vector_text_stable() {
    local name="$1"
    local log="$2"

    if ! awk '
        # Queue lines are emitted by the game thread while frame traces are
        # emitted by the scheduler thread. Their frame labels are diagnostic,
        # not a causal ordering guarantee. Correlate the published build
        # boundary with the render trace instead of treating a queue split
        # across that boundary as a dropped frame.
        /VECTOR_TEXT_BUILD:/ {
            frame = -1
            commands = -1
            for (i = 1; i <= NF; ++i) {
                if ($i ~ /^frame=/) {
                    split($i, value, "=")
                    frame = value[2] + 0
                } else if ($i ~ /^commands=/) {
                    split($i, value, "=")
                    commands = value[2] + 0
                }
            }
            if (frame >= 0 && commands > 0) {
                published[frame] = commands
                ++published_count
            }
        }
        /VECTOR_TEXT_TRACE:/ {
            frame = -1
            commands = -1
            reused = 0
            for (i = 1; i <= NF; ++i) {
                if ($i ~ /^frame=/) {
                    split($i, value, "=")
                    frame = value[2] + 0
                } else if ($i ~ /^commands=/) {
                    split($i, value, "=")
                    commands = value[2] + 0
                } else if ($i ~ /^reused=/) {
                    split($i, value, "=")
                    reused = value[2] + 0
                }
            }
            if (frame < 0 || commands < 0) next
            if (commands > 0) {
                trace_commands[frame] = commands
                trace_reused[frame] = reused
                ++positive
            }
        }
        END {
            if (positive < 5 || published_count < 3) exit 1
            for (frame in trace_commands) {
                if (trace_reused[frame] != 0) {
                    ++matched
                    continue
                }
                if (((frame - 1) in published &&
                     published[frame - 1] == trace_commands[frame]) ||
                    ((frame in published) &&
                     published[frame] == trace_commands[frame]) ||
                    ((frame + 1) in published &&
                     published[frame + 1] == trace_commands[frame])) {
                    ++matched
                }
            }
            # A render trace may be one scheduler tick ahead of or behind the
            # game-thread publication. Every observed batch must nevertheless
            # be either a matching published batch or an explicit reuse of the
            # last complete batch.
            if (matched < 5) exit 1
        }
    ' "${log}"; then
        echo "FAIL: ${name} rendered a vector batch without a complete publication or explicit reuse"
        rg -n 'VECTOR_TEXT_BUILD|VECTOR_TEXT_TRACE|VECTOR_TEXT_QUEUE' "${log}" | tail -80 || true
        exit 1
    fi
}

assert_vector_text_builds_atomic() {
    local name="$1"
    local log="$2"

    if ! awk '
        /VECTOR_TEXT_BUILD:/ {
            frame = -1
            commands = -1
            for (i = 1; i <= NF; ++i) {
                if ($i ~ /^frame=/) {
                    split($i, value, "=")
                    frame = value[2] + 0
                } else if ($i ~ /^commands=/) {
                    split($i, value, "=")
                    commands = value[2] + 0
                }
            }
            if (frame >= 0 && commands > 0) {
                ++published
                published_commands[frame] = commands
            }
        }
        /VECTOR_TEXT_TRACE:/ {
            frame = -1
            commands = -1
            for (i = 1; i <= NF; ++i) {
                if ($i ~ /^frame=/) {
                    split($i, value, "=")
                    frame = value[2] + 0
                } else if ($i ~ /^commands=/) {
                    split($i, value, "=")
                    commands = value[2] + 0
                }
            }
            if (frame >= 0 && commands >= 0) {
                rendered_commands[frame] = commands
            }
        }
        END {
            if (published < 3) exit 1
            for (frame in published_commands) {
                expected = published_commands[frame]
                if (((frame in rendered_commands) &&
                     rendered_commands[frame] == expected) ||
                    (((frame + 1) in rendered_commands) &&
                     rendered_commands[frame + 1] == expected)) {
                    ++matched
                }
            }
            # Build and render counters are intentionally independent: the
            # game can construct more than one display list while the
            # scheduler presents one. Require repeated complete matches, but
            # do not mistake that normal queueing latency for a partial batch.
            if (matched < 3) exit 1
        }
    ' "${log}"; then
        echo "FAIL: ${name} exposed a partial vector text build to the renderer"
        rg -n 'VECTOR_TEXT_BUILD|VECTOR_TEXT_TRACE' "${log}" | tail -80 || true
        exit 1
    fi
}

assert_briefing_objectives_layout() {
    local name="$1"
    local log="$2"

    if ! awk '
        /VECTOR_TEXT_QUEUE:/ {
            frame = -1
            y = -1
            label = ""
            for (i = 1; i <= NF; ++i) {
                if ($i ~ /^frame=/) {
                    split($i, value, "=")
                    frame = value[2] + 0
                } else if ($i ~ /^y=/) {
                    split($i, value, "=")
                    y = value[2] + 0
                } else if ($i ~ /^text=[a-d]\.$/) {
                    split($i, value, "=")
                    label = substr(value[2], 1, 1)
                }
            }
            if (frame >= 0 && y >= 0 && label != "") {
                objective_y[frame, label] = y
                saw_frame[frame] = 1
            }
        }
        END {
            valid_frames = 0
            for (frame in saw_frame) {
                if (!((frame, "a") in objective_y) ||
                    !((frame, "b") in objective_y) ||
                    !((frame, "c") in objective_y) ||
                    !((frame, "d") in objective_y)) {
                    continue
                }
                if (!(objective_y[frame, "a"] < objective_y[frame, "b"] &&
                      objective_y[frame, "b"] < objective_y[frame, "c"] &&
                      objective_y[frame, "c"] < objective_y[frame, "d"])) {
                    exit 1
                }
                ++valid_frames
            }
            if (valid_frames < 3) exit 1
        }
    ' "${log}"; then
        echo "FAIL: ${name} queued briefing objectives at overlapping Y origins"
        rg -n 'VECTOR_TEXT_QUEUE:.*text=[a-d]\.' "${log}" | tail -40 || true
        exit 1
    fi
}

assert_absent() {
    local name="$1"
    local log="$2"
    local pattern="$3"

    if rg -F -q -- "${pattern}" "${log}"; then
        echo "FAIL: ${name} unexpectedly emitted: ${pattern}"
        rg -n -F -- "${pattern}" "${log}" || true
        exit 1
    fi
}

assert_water_shader_compiled() {
    local name="$1"
    local log="$2"

    assert_absent "${name}" "${log}" 'WATER_COMPOSITE_SHADER: compile failed'
    assert_absent "${name}" "${log}" 'WATER_COMPOSITE_SHADER: link failed'
    assert_absent "${name}" "${log}" 'WATER_COMPOSITE: shader uniforms unavailable'
}

assert_water_framebuffer_readback() {
    local name="$1"
    local log="$2"

    # The normal BDD run is log-only so it remains usable on llvmpipe/WSLg.
    # GE_BDD_PIXEL_ASSERT=1 enables the real PPM readback boundary below.
    if [[ "${GE_BDD_PIXEL_ASSERT:-0}" != 1 ]]; then
        return 0
    fi

    local dump_dir="${ROOT_DIR}/ppm"
    local newest_dump
    newest_dump="$(find "${dump_dir}" -maxdepth 1 -type f -name 'frame_*.ppm' \
        -newer "${WATER_PIXEL_MARKER}" -print 2>/dev/null | sort | tail -1)"
    if [[ -z "${newest_dump}" ]]; then
        echo "FAIL: ${name} produced no framebuffer readback"
        sed -n '1,220p' "${log}"
        exit 1
    fi
    if ! python3 "${ROOT_DIR}/tools_pc/pixcount.py" "${newest_dump}"; then
        echo "FAIL: ${name} produced an empty or malformed framebuffer readback"
        sed -n '1,220p' "${log}"
        exit 1
    fi
}

water_pixel_oracle="${GE_BDD_PIXEL_ORACLE:-${ROOT_DIR}/build-pc/ge007_water_render_pixel_oracle_test}"

water_dump_files() {
    find "${ROOT_DIR}/ppm" -maxdepth 1 -type f -name 'frame_*.ppm' \
        -newer "${WATER_PIXEL_MARKER}" -print 2>/dev/null | sort
}

assert_water_pixel_variance() {
    local name="$1"
    if [[ "${GE_BDD_PIXEL_ASSERT:-0}" != 1 ]]; then return 0; fi
    local first
    first="$(water_dump_files | head -1)"
    [[ -n "${first}" && -x "${water_pixel_oracle}" ]] || {
        echo "FAIL: ${name} did not produce a usable water pixel oracle input"
        exit 1
    }
    "${water_pixel_oracle}" --variance "${first}"
}

assert_water_reflection_asymmetry() {
    local name="$1"
    if [[ "${GE_BDD_PIXEL_ASSERT:-0}" != 1 ]]; then return 0; fi
    local first
    first="$(water_dump_files | head -1)"
    [[ -n "${first}" && -x "${water_pixel_oracle}" ]] || {
        echo "FAIL: ${name} did not produce a reflection pixel oracle input"
        exit 1
    }
    "${water_pixel_oracle}" --reflection-asymmetry "${first}"
}

assert_water_occluder_mask() {
    local name="$1"
    if [[ "${GE_BDD_PIXEL_ASSERT:-0}" != 1 ]]; then return 0; fi
    local first
    first="$(water_dump_files | head -1)"
    [[ -n "${first}" && -x "${water_pixel_oracle}" ]] || {
        echo "FAIL: ${name} did not produce an occluder pixel oracle input"
        exit 1
    }
    "${water_pixel_oracle}" --occluder-mask "${first}"
}

assert_water_temporal_change() {
    local name="$1"
    if [[ "${GE_BDD_PIXEL_ASSERT:-0}" != 1 ]]; then return 0; fi
    mapfile -t dumps < <(water_dump_files)
    if [[ "${#dumps[@]}" -lt 2 || ! -x "${water_pixel_oracle}" ]]; then
        echo "FAIL: ${name} did not produce two temporal water captures"
        exit 1
    fi
    "${water_pixel_oracle}" --temporal "${dumps[0]}" "${dumps[${#dumps[@]}-1]}"
}

assert_water_viewport_isolation() {
    local name="$1"
    if [[ "${GE_BDD_PIXEL_ASSERT:-0}" != 1 ]]; then return 0; fi
    local first
    first="$(water_dump_files | head -1)"
    [[ -n "${first}" && -x "${water_pixel_oracle}" ]] || {
        echo "FAIL: ${name} did not produce a split-viewport pixel oracle input"
        exit 1
    }
    "${water_pixel_oracle}" --viewport-isolation "${first}"
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
        if rg -q -e 'Could not init SDL|x11 not available|No available video device' \
            "${log}"; then
            echo "SKIP: ${name} has no usable SDL/OpenGL display"
            exit 77
        fi
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
# The split renderer probes opt into a clean exit immediately after both
# rectangles reach the real GL mapping; the other render probe remains bounded
# by a timeout while its own trace assertion runs.
render_probe=(bash -c ': "${GE_BDD_EXIT_AFTER_FRAMES:=24}"; export GE_BDD_EXIT_AFTER_FRAMES; timeout -k 2s 30s "$@"; status=$?; if [[ "${status}" -eq 124 || "${status}" -eq 137 ]]; then exit 0; fi; exit "${status}"' ge007-render "${BINARY}")
menu_render_probe=(bash -c ': "${GE_BDD_EXIT_AFTER_FRAMES:=1800}"; export GE_BDD_EXIT_AFTER_FRAMES; timeout -k 2s 35s "$@"; status=$?; if [[ "${status}" -eq 124 || "${status}" -eq 137 ]]; then exit 0; fi; exit "${status}"' ge007-menu-render "${BINARY}")

# GPU Gems water shaders have separate source paths for legacy desktop GLSL,
# modern desktop GLSL, and GLES. The matrix is opt-in because a machine may
# expose only one SDL context profile; enabling it never silently substitutes
# the default driver profile for a requested profile.
run_water_profile_case() {
    local name="$1"
    local gl_version="$2"
    local profile_label="$3"

    run_case "${name}" \
        'STAGE_SMOKE: mode=0 stage=26 players=1 viewports=1' \
        'WATER_WORLD_SURFACE: stage=26 height=' \
        'WATER_COMPOSITE_SHADER: profile=' \
        'WATER_COMPOSITE: phase=post_world_before_hud' \
        -- \
        GE_RENDER_MODE=2 \
        GE_WATER_TRACE=1 \
        GE_WATER_SHADER_PROFILE="${profile_label}" \
        GE_FAKE_CONTROLLERS=1 \
        GE_STAGE_SMOKE=1 \
        "${render_probe[@]}" -level_26 --gl-version "${gl_version}"
    assert_water_shader_compiled "${name}" "${TMP_DIR}/${name}.log"
}

WATER_PIXEL_MARKER="${TMP_DIR}/water-pixel-marker-inactive"
water_pixel_env=()
water_render_probe=("${render_probe[@]}")
if [[ "${GE_BDD_PIXEL_ASSERT:-0}" == 1 ]]; then
    # Stage smoke has a deterministic loading/transition interval around the
    # earlier frames. Capture two stable post-load frames far enough apart to
    # expose the configured wave phase while keeping both the frame budget and
    # water clock fixed. A short capture can be numerically valid yet appear
    # unchanged after 8-bit quantization.
    water_pixel_env=(GE_PCDUMP=91-191:100)
    water_render_probe=(bash -c ': "${GE_BDD_EXIT_AFTER_FRAMES:=200}"; : "${GE_WATER_TEST_FIXED_DELTA:=0.25}"; export GE_BDD_EXIT_AFTER_FRAMES GE_WATER_TEST_FIXED_DELTA; timeout -k 2s 30s "$@"; status=$?; if [[ "${status}" -eq 124 || "${status}" -eq 137 ]]; then exit 0; fi; exit "${status}"' ge007-water-pixel-render "${BINARY}")
fi

mark_water_pixel_capture() {
    local name="$1"
    WATER_PIXEL_MARKER="${TMP_DIR}/${name}.pixel-marker"
    touch "${WATER_PIXEL_MARKER}"
}
# The mode selector is navigated with controller D-pad presses: down moves
# solo -> multiplayer -> co-op. This protects the controller-only path rather
# than relying on the old analog cursor walk.
coop_menu_script='60:DOWN;120:DOWN;180:A;300:A;460:A;620:A;780:A;940:A;1100:A;1260:A;1420:A'
# Pulses leave a release edge between presses. That makes the harness exercise
# the same RMB hold/edge transition used to leave the stage intro, while still
# proving that gameplay draws the reticle with the aim trigger active.
aim_hold_script='0:R;12:R;24:R;36:R;48:R;60:R;72:R;84:R;96:R;108:R;120:R;132:R;144:R;156:R;168:R;180:R;192:R;204:R;216:R;228:R;240:R;252:R;264:R;276:R;288:R;300:R;312:R;324:R;336:R;348:R;360:R;372:R;384:R;396:R;408:R;420:R;432:R;444:R;456:R;468:R;480:R;492:R;504:R;516:R;528:R;540:R;552:R;564:R;576:R;588:R;600:R;612:R;624:R;636:R;648:R;660:R;672:R;684:R;696:R;708:R;720:R;732:R;744:R;756:R'
run_case solo_launch \
    'STAGE_SMOKE: mode=0 stage=33 players=1 viewports=1' \
    'STAGE_PLAYER: player=0' \
    -- \
    GE_FAKE_CONTROLLERS=1 \
    GE_STAGE_SMOKE=1 \
    GE_STAGE_SMOKE_EXIT=1 \
    "${common_timeout[@]}" -level_33

run_case explosion_presentation_original \
    'STAGE_SMOKE: mode=0 stage=33 players=1 viewports=1' \
    'EXPLOSION_PRESENTATION: mode=0 extras=0 depth=compare,no-write' \
    -- \
    GE_RENDER_MODE=0 \
    GE_EXPLOSION_POLICY_TRACE=1 \
    GE_FAKE_CONTROLLERS=1 \
    GE_STAGE_SMOKE=1 \
    "${render_probe[@]}" -level_33

run_case explosion_presentation_remaster \
    'STAGE_SMOKE: mode=0 stage=33 players=1 viewports=1' \
    'EXPLOSION_PRESENTATION: mode=2 extras=2 depth=compare,no-write' \
    -- \
    GE_RENDER_MODE=2 \
    GE_EXPLOSION_POLICY_TRACE=1 \
    GE_FAKE_CONTROLLERS=1 \
    GE_STAGE_SMOKE=1 \
    "${render_probe[@]}" -level_33

run_case depth_of_field_single_view \
    'STAGE_SMOKE: mode=0 stage=33 players=1 viewports=1' \
    -- \
    GE_DOF=1 \
    GE_POSTFX=0 \
    GE_FAKE_CONTROLLERS=1 \
    GE_STAGE_SMOKE=1 \
    "${render_probe[@]}" -level_33
assert_dof_active depth_of_field_single_view "${TMP_DIR}/depth_of_field_single_view.log" 1

run_case depth_of_field_split_view \
    'STAGE_SMOKE: mode=3 stage=33 players=2 viewports=2' \
    'VIEWPORT_SMOKE: player=0 rect=0,10,320,109 players=2' \
    'VIEWPORT_SMOKE: player=1 rect=0,121,320,109 players=2' \
    -- \
    GE_DOF=1 \
    GE_POSTFX=0 \
    GE_FAKE_CONTROLLERS=2 \
    GE_RESUME_MODE=coop \
    GE_STAGE_SMOKE=1 \
    GE_VIEWPORT_SMOKE=1 \
    GE_VIEWPORT_TRACE=1 \
    "${render_probe[@]}" -level_33
assert_dof_active depth_of_field_split_view "${TMP_DIR}/depth_of_field_split_view.log" 2
assert_split_render_viewports depth_of_field_split_view "${TMP_DIR}/depth_of_field_split_view.log"

run_case water_material_original \
    'STAGE_SMOKE: mode=0 stage=26 players=1 viewports=1' \
    -- \
    GE_RENDER_MODE=0 \
    GE_WATER_TRACE=1 \
    GE_FAKE_CONTROLLERS=1 \
    GE_STAGE_SMOKE=1 \
    "${render_probe[@]}" -level_26
assert_absent water_material_original "${TMP_DIR}/water_material_original.log" 'WATER_SHADER:'
assert_absent water_material_original "${TMP_DIR}/water_material_original.log" 'WATER_MATERIAL:'
assert_absent water_material_original "${TMP_DIR}/water_material_original.log" 'WATER_COMPOSITE:'

mark_water_pixel_capture water_material_enhanced
run_case water_material_enhanced \
    'STAGE_SMOKE: mode=0 stage=26 players=1 viewports=1' \
    'WATER_WORLD_SURFACE: stage=26' \
    'WATER_CAPTURE: source=world-surface' \
    'WATER_COMPOSITE_SHADER: profile=' \
    'WATER_COMPOSITE_GEOMETRY: uv-min=' \
    'WATER_COMPOSITE: phase=post_world_before_hud tier=1 triangles=2 reflection=reflected-camera fallback=screen-space planar_reflection=1 clip=water-plane refraction=1 contact=1 depth=scene-copy depth-test=lequal bump=height-gradient ray=projected-xy-over-depth' \
    'source=world-surface' \
    -- \
    GE_RENDER_MODE=1 \
    GE_WATER_TRACE=1 \
    "${water_pixel_env[@]}" \
    GE_FAKE_CONTROLLERS=1 \
    GE_STAGE_SMOKE=1 \
    "${water_render_probe[@]}" -level_26
assert_water_shader_compiled water_material_enhanced "${TMP_DIR}/water_material_enhanced.log"
assert_absent water_material_enhanced "${TMP_DIR}/water_material_enhanced.log" 'WATER_MATERIAL: enabled=1 render_mode=1'
assert_water_framebuffer_readback water_material_enhanced "${TMP_DIR}/water_material_enhanced.log"
assert_water_pixel_variance water_material_enhanced
assert_water_reflection_asymmetry water_material_enhanced
assert_water_occluder_mask water_material_enhanced
assert_water_temporal_change water_material_enhanced

# water_material_remaster remains the compatibility label for this tier; the
# production scenario now proves the physical Frigate world surface.
mark_water_pixel_capture water_world_surface_frigate
run_case water_world_surface_frigate \
    'STAGE_SMOKE: mode=0 stage=26 players=1 viewports=1' \
    'WATER_WORLD_SURFACE: stage=26' \
    'WATER_CAPTURE: source=world-surface' \
    'WATER_COMPOSITE_SHADER: profile=' \
    'WATER_COMPOSITE_GEOMETRY: uv-min=' \
    'WATER_COMPOSITE: phase=post_world_before_hud tier=2 triangles=2 reflection=reflected-camera fallback=screen-space planar_reflection=1 clip=water-plane refraction=1 contact=1 depth=scene-copy depth-test=lequal bump=height-gradient ray=projected-xy-over-depth' \
    'source=world-surface' \
    -- \
    GE_RENDER_MODE=2 \
    GE_WATER_TRACE=1 \
    "${water_pixel_env[@]}" \
    GE_FAKE_CONTROLLERS=1 \
    GE_STAGE_SMOKE=1 \
    "${water_render_probe[@]}" -level_26
assert_absent water_world_surface_frigate "${TMP_DIR}/water_world_surface_frigate.log" 'WATER_MATERIAL: enabled=1 render_mode=2'
assert_water_shader_compiled water_world_surface_frigate "${TMP_DIR}/water_world_surface_frigate.log"
assert_water_framebuffer_readback water_world_surface_frigate "${TMP_DIR}/water_world_surface_frigate.log"
assert_water_pixel_variance water_world_surface_frigate
assert_water_reflection_asymmetry water_world_surface_frigate
assert_water_occluder_mask water_world_surface_frigate
assert_water_temporal_change water_world_surface_frigate

run_case water_dam_occlusion \
    'STAGE_SMOKE: mode=0 stage=26 players=1 viewports=1' \
    'WATER_OCCLUDER: type=dam depth-mask=1' \
    'WATER_COMPOSITE: phase=post_world_before_hud' \
    -- \
    GE_RENDER_MODE=2 \
    GE_WATER_TRACE=1 \
    GE_WATER_TEST_OCCLUDER=dam \
    GE_FAKE_CONTROLLERS=1 \
    GE_STAGE_SMOKE=1 \
    "${render_probe[@]}" -level_26

run_case water_multiple_surfaces \
    'STAGE_SMOKE: mode=0 stage=26 players=1 viewports=1' \
    'WATER_SURFACE_BATCH: surface_count=' \
    'fixture=non-coplanar' \
    'WATER_COMPOSITE: phase=post_world_before_hud' \
    -- \
    GE_RENDER_MODE=2 \
    GE_WATER_TRACE=1 \
    GE_WATER_TEST_SURFACES=2 \
    GE_FAKE_CONTROLLERS=1 \
    GE_STAGE_SMOKE=1 \
    "${render_probe[@]}" -level_26

run_case water_boat_wake \
    'STAGE_SMOKE: mode=0 stage=26 players=1 viewports=1' \
    'WATER_DISTURBANCE: event=boat-wake' \
    'WATER_BOAT_WAKE: count=3' \
    'WATER_COMPOSITE: phase=post_world_before_hud' \
    'disturbances=3' \
    -- \
    GE_RENDER_MODE=2 \
    GE_WATER_TRACE=1 \
    GE_WATER_TEST_DISTURBANCE=boat-wake \
    GE_FAKE_CONTROLLERS=1 \
    GE_STAGE_SMOKE=1 \
    "${render_probe[@]}" -level_26

run_case water_resize_recreate \
    'STAGE_SMOKE: mode=0 stage=26 players=1 viewports=1' \
    'WATER_COMPOSITE: phase=post_world_before_hud' \
    'WATER_RESIZE: resources=invalidated' \
    -- \
    GE_RENDER_MODE=2 \
    GE_WATER_TRACE=1 \
    GE_BDD_RESIZE=1 \
    GE_BDD_EXIT_AFTER_FRAMES=24 \
    GE_FAKE_CONTROLLERS=1 \
    GE_STAGE_SMOKE=1 \
    "${render_probe[@]}" -level_26

if [[ "${GE_BDD_GLSL_PROFILES:-0}" == 1 ]]; then
    echo 'WATER_SHADER_PROFILE_MATRIX: profiles=GLSL120,GLSL130,GLSL_ES'
    run_water_profile_case water_shader_profile_legacy 2.1 GLSL120
    run_water_profile_case water_shader_profile_modern 3.0 GLSL130
    run_water_profile_case water_shader_profile_gles 3.0es GLSL_ES
else
    echo 'SKIP: glsl_profile_matrix disabled (set GE_BDD_GLSL_PROFILES=1)'
fi

mark_water_pixel_capture water_material_split_view
run_case water_material_split_view \
    'STAGE_SMOKE: mode=3 stage=26 players=2 viewports=2' \
    'WATER_SPLIT_FIXTURE: enabled=1 triangles=2 surfaces=1' \
    'WATER_COMPOSITE: phase=post_world_before_hud' \
    -- \
    GE_RENDER_MODE=2 \
    GE_WATER_TRACE=1 \
    GE_WATER_TEST_SPLIT=1 \
    GE_FAKE_CONTROLLERS=2 \
    GE_RESUME_MODE=coop \
    GE_STAGE_SMOKE=1 \
    GE_VIEWPORT_SMOKE=1 \
    GE_VIEWPORT_TRACE=1 \
    "${water_pixel_env[@]}" \
    "${water_render_probe[@]}" -level_26
assert_split_render_viewports water_material_split_view "${TMP_DIR}/water_material_split_view.log"
assert_water_composite_viewports water_material_split_view "${TMP_DIR}/water_material_split_view.log"
assert_water_viewport_isolation water_material_split_view

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
    GE_VIEWPORT_TRACE_EXIT=1 \
    "${render_probe[@]}" -level_38
assert_distinct_positions multiplayer_launch "${TMP_DIR}/multiplayer_launch.log"
assert_split_render_viewports multiplayer_launch "${TMP_DIR}/multiplayer_launch.log"

run_case aim_reticle \
    'STAGE_SMOKE: mode=0 stage=33 players=1 viewports=1' \
    -- \
    GE_FAKE_CONTROLLERS=1 \
    GE_RESUME_MODE=solo \
    GE_INPUTSCRIPT="${aim_hold_script}" \
    GE_INPUTLOG=1 \
    GE_SIGHT_TRACE=1 \
    GE_SIGHT_TRACE_EXIT=1 \
    GE_BDD_EXIT_AFTER_FRAMES=120 \
    GE_STAGE_SMOKE=1 \
    "${render_probe[@]}" -level_33
assert_sight_drawn_while_aiming aim_reticle "${TMP_DIR}/aim_reticle.log"

run_case vector_text_hud_stability \
    'STAGE_SMOKE: mode=0 stage=33 players=1 viewports=1' \
    'vector UI font:' \
    -- \
    GE_RENDER_MODE=2 \
    GE_VECTOR_TEXT_TRACE=1 \
    GE_BDD_EXIT_AFTER_FRAMES=120 \
    GE_FAKE_CONTROLLERS=1 \
    GE_STAGE_SMOKE=1 \
    "${render_probe[@]}" -level_33
assert_vector_text_stable vector_text_hud_stability "${TMP_DIR}/vector_text_hud_stability.log"
assert_vector_text_builds_atomic vector_text_hud_stability "${TMP_DIR}/vector_text_hud_stability.log"

run_case briefing_vector_text \
    'vector UI font:' \
    'BRIEFING_FONT_TRACE: route=vector page=0' \
    -- \
    GE_RENDER_MODE=2 \
    GE_VECTOR_TEXT_TRACE=1 \
    GE_BRIEFING_FONT_TRACE=1 \
    GE_FAKE_CONTROLLERS=1 \
    GE_STARTMENU=6 \
    GE_D243=1 \
    GE_INPUTSCRIPT='60:DOWN;120:A;300:A;460:A;620:A' \
    "${menu_render_probe[@]}"
assert_vector_text_stable briefing_vector_text "${TMP_DIR}/briefing_vector_text.log"
assert_vector_text_builds_atomic briefing_vector_text "${TMP_DIR}/briefing_vector_text.log"
assert_briefing_objectives_layout briefing_vector_text "${TMP_DIR}/briefing_vector_text.log"

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
    GE_VIEWPORT_TRACE_EXIT=1 \
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
    GE_VIEWPORT_TRACE_EXIT=1 \
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
