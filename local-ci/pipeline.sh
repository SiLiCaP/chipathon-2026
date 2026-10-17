#!/usr/bin/env bash
# Runs inside the tt-local-ci container (started by local-ci/run).
# Mirrors the jobs and steps of .github/workflows/{test,docs,gds}.yaml and the
# TinyTapeout/tt-gds-action composite actions they call.
#
# Layout of a run:  $RUN_DIR/jobs/<job>/{status,started,finished,steps.tsv,steps/NN.log,summary.md,artifacts/<name>/}

set -uo pipefail

LOCAL_CI_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(pwd)"
TT="$REPO/tt/tt_tool.py"
PY_TT=/opt/venv-tt/bin
PY_TEST=/opt/venv-test/bin
PY_PRECHECK=/opt/venv-precheck/bin
export PATH="$PY_TT:$PATH"

# PDK versions pinned by tt-gds-action/precheck/action.yml
declare -A PRECHECK_PDK_VERSION=(
    [sky130A]=0536d02d875c8f67dd7cca3902ac457e62f20005
    [gf180mcuD]=e8daeda73ca8f5814dbc0b11d1d05802251a3750
    [ihp-sg13g2]=c4b8b4e5e7a05f375cca3815d51b3a37721fbf5c
)

# The PDK the gds workflow builds for (`pdk:` input of tt-gds-action)
PDK="$(sed -n 's/^\s*pdk:\s*\([A-Za-z0-9_-]*\).*/\1/p' .github/workflows/gds.yaml | head -1)"
PDK="${PDK:-sky130A}"
case "$PDK" in
    gf180mcuD) TT_ARGS="--gf" ;;
    ihp-sg13g2) TT_ARGS="--ihp" ;;
    *) TT_ARGS="" ;;
esac
pdk_family() { case "$1" in sky130*) echo sky130 ;; gf180mcu*) echo gf180mcu ;; *) echo "$1" ;; esac; }

report() { "$PY_TT/python" "$LOCAL_CI_DIR/report.py" build "$OUT" "$RUN_DIR" >/dev/null 2>&1 || true; }

c_bold=$'\033[1m'; c_red=$'\033[31m'; c_green=$'\033[32m'; c_dim=$'\033[2m'; c_off=$'\033[0m'

# --- Job / step bookkeeping ---------------------------------------------------
JOB="" JOB_DIR="" JOB_FAILED=0 STEP_NO=0

job_begin() {  # job_begin <id> <workflow> <display name>
    JOB="$1" JOB_DIR="$RUN_DIR/jobs/$1" JOB_FAILED=0 STEP_NO=0
    mkdir -p "$JOB_DIR/steps" "$JOB_DIR/artifacts"
    echo "$2" >"$JOB_DIR/workflow"
    echo "$3" >"$JOB_DIR/name"
    echo running >"$JOB_DIR/status"
    date +%s >"$JOB_DIR/started"
    : >"$JOB_DIR/steps.tsv"
    : >"$JOB_DIR/summary.md"
    export GITHUB_STEP_SUMMARY="$JOB_DIR/summary.md"
    printf '\n%s━━━ %s / %s ━━━%s\n' "$c_bold" "$2" "$3" "$c_off"
    report
}

job_end() {
    if [ "$JOB_FAILED" = 0 ]; then echo success >"$JOB_DIR/status"; else echo failure >"$JOB_DIR/status"; fi
    date +%s >"$JOB_DIR/finished"
    report
}

job_skip() {  # job_skip <id> <workflow> <display name> <reason>
    mkdir -p "$RUN_DIR/jobs/$1"
    echo "$2" >"$RUN_DIR/jobs/$1/workflow"
    echo "$3" >"$RUN_DIR/jobs/$1/name"
    echo skipped >"$RUN_DIR/jobs/$1/status"
    echo "$4" >"$RUN_DIR/jobs/$1/reason"
    : >"$RUN_DIR/jobs/$1/steps.tsv"
    printf '\n%s━━━ %s / %s: skipped (%s)%s\n' "$c_dim" "$2" "$3" "$4" "$c_off"
    report
}

job_status() { cat "$RUN_DIR/jobs/$1/status" 2>/dev/null || echo missing; }

# step [--always] "<name>" <function> [args...]
# Like a GitHub step: skipped after an earlier failure unless --always
# (`if: always()` / `if: success() || failure()`).
step() {
    local always=0
    [ "$1" = --always ] && { always=1; shift; }
    local name="$1"; shift
    STEP_NO=$((STEP_NO + 1))
    local n; n=$(printf '%02d' "$STEP_NO")
    local logf="$JOB_DIR/steps/$n.log"
    if [ "$JOB_FAILED" = 1 ] && [ "$always" = 0 ]; then
        printf '%s\t%s\tskipped\t\t\n' "$n" "$name" >>"$JOB_DIR/steps.tsv"
        printf '%s  ○ %s (skipped)%s\n' "$c_dim" "$name" "$c_off"
        return
    fi
    local t0; t0=$(date +%s)
    printf '%s\t%s\n' "$n" "$name" >"$JOB_DIR/current"
    report
    printf '%s  ▶ %s%s\n' "$c_bold" "$name" "$c_off"
    ( set -eo pipefail; cd "$REPO"; "$@" ) 2>&1 | tee "$logf" | sed -u 's/^/    /'
    local rc=${PIPESTATUS[0]}
    local status=success
    if [ "$rc" != 0 ]; then
        status=failure
        JOB_FAILED=1
        echo "Error: Process completed with exit code $rc." >>"$logf"
        printf '%s  ✗ %s (exit code %s)%s\n' "$c_red" "$name" "$rc" "$c_off"
    else
        printf '%s  ✓ %s%s\n' "$c_green" "$name" "$c_off"
    fi
    rm -f "$JOB_DIR/current"
    printf '%s\t%s\t%s\t%s\t%s\n' "$n" "$name" "$status" "$t0" "$(date +%s)" >>"$JOB_DIR/steps.tsv"
    report
}

# upload_artifact <name> <path>...  (globs that match nothing are ignored, like actions/upload-artifact)
upload_artifact() {
    local name="$1"; shift
    local dest="$JOB_DIR/artifacts/$name" found=0 p
    rm -rf "$dest" && mkdir -p "$dest"
    shopt -s nullglob
    for p in "$@"; do
        for f in $p; do
            [ -e "$f" ] || continue
            cp -a --parents "$f" "$dest/"
            found=1
        done
    done
    shopt -u nullglob
    if [ "$found" = 0 ]; then
        echo "Warning: No files were found with the provided path: $*. No artifacts will be uploaded."
        rmdir "$dest"
    else
        echo "Artifact '$name' saved: $(du -sh "$dest" | cut -f1)"
    fi
}

junit_summary() { "$PY_TT/python" "$LOCAL_CI_DIR/report.py" junit "$1" >>"$GITHUB_STEP_SUMMARY"; }

ciel_enable() {  # ciel_enable <pdk_root> <pdk> <version>
    echo "Enabling $2 ($3) in $1"
    ciel enable --pdk-root "$1" --pdk-family "$(pdk_family "$2")" "$3"
}

# --- workflow: test -----------------------------------------------------------
test_run() {
    export PATH="$PY_TEST:$PATH"
    cd test
    make clean
    make
    # make will return success even if the tests fail, so check results.xml for failed tests
    python -m cocotb_tools.check_results results.xml
}

core_test_run() {
    [ -d test/core ] || { echo "No test/core in this repo, nothing to run"; return 0; }
    export PATH="$PY_TEST:$PATH"
    make -C test/core all-variants
}

workflow_test() {
    job_begin test test test
    step "Run tests" test_run
    step "Run core tests" core_test_run
    step --always "Test Summary" junit_summary test/results.xml
    step --always "upload waveform and test results" upload_artifact test-results test/tb.fst test/results.xml 'test/output/*'
    job_end
}

# --- workflow: docs -----------------------------------------------------------
docs_check() { "$TT" --check-docs | tee -a "$GITHUB_STEP_SUMMARY"; }

workflow_docs() {
    job_begin docs docs docs
    step "Check info.yaml and docs/info.md" docs_check
    step "Create PDF" "$TT" --create-pdf
    step "Archive PDF" upload_artifact PDF datasheet.pdf
    job_end
}

# --- workflow: gds ------------------------------------------------------------
gds_linter() {
    # Same logic as the "Linter output" step of tt-gds-action
    local logs=(runs/wokwi/*-verilator-lint/verilator-lint.log)
    local linter_log=${logs[0]}
    cat "$linter_log" 2>/dev/null || true
    [ -s "$linter_log" ] || return 0
    local count_error count_warn count_other
    count_error=$(grep -Eic "^%Error" "$linter_log" || true)
    count_warn=$(grep -Eic "^%Warning" "$linter_log" || true)
    count_other=$(grep -Ei "^%" "$linter_log" | grep -Evc "%(Warning|Error)" || true)
    local open="" summary="" icon=":green_circle:"
    [ "$count_other" -gt 0 ] && { summary="$count_other message(s)"; icon=":orange_circle:"; }
    [ "$count_warn" -gt 0 ] && { summary="$count_warn warning(s)"; icon=":orange_circle:"; }
    [ "$count_error" -gt 0 ] && { summary="$count_error error(s)"; icon=":red_circle:"; open="open"; }
    [ -n "$summary" ] && summary="[$summary]"
    {
        echo "<details ${open}>"
        echo "<summary><h1>Linter output&nbsp;&nbsp;<h4>${summary} ${icon}</h4></h1></summary>"
        echo "<pre>"
        sed 's/&/\&amp;/g; s/</\&lt;/g; s/^/    /' <"$linter_log"
        echo "</pre>"
        echo "</details>"
    } >>"$GITHUB_STEP_SUMMARY"
}

gds_png() { "$TT" --create-png $TT_ARGS 2>&1 || echo "WARNING: Failed to render PNG preview from GDS; error $?"; }

job_gds() {
    job_begin gds gds gds
    step "Fetch verilog and build config" "$TT" --create-user-config $TT_ARGS
    step "Make GDS with LibreLane" "$TT" --harden $TT_ARGS
    step "Show build files (for debugging)" find runs/wokwi/
    step --always "Linter output" gds_linter
    step "Yosys warnings" bash -c "'$TT' --print-warnings $TT_ARGS >> \"\$GITHUB_STEP_SUMMARY\""
    step "Routing summary" bash -c "'$TT' --print-stats $TT_ARGS >> \"\$GITHUB_STEP_SUMMARY\""
    step "Cell usage summary" bash -c "'$TT' --print-cell-category $TT_ARGS >> \"\$GITHUB_STEP_SUMMARY\""
    step --always "Publish build logs" upload_artifact GDS_logs 'src/*' 'runs/wokwi/*'
    step "Prepare tt_submission artifact" "$TT" --create-tt-submission $TT_ARGS
    step "Publish tt_submission artifact" upload_artifact tt_submission 'src/*' 'docs/*' 'tt_submission/*' info.yaml LICENSE
    step "Render PNG from GDS" gds_png
    step "Upload gds_render (png) artifact" upload_artifact gds_render gds_render.png
    job_end
}

precheck_run() {
    cd tt/precheck
    rm -rf reports/*
    PDK_ROOT="$PRECHECK_PDK_ROOT" PATH="/opt/precheck-tools/bin:$PY_PRECHECK:$PATH" \
        python precheck.py --gds "$REPO"/tt_submission/*.gds* --tech "$PDK"
}

job_precheck() {
    job_begin precheck gds precheck
    step "Install $PDK PDK" ciel_enable "$PRECHECK_PDK_ROOT" "$PDK" "${PRECHECK_PDK_VERSION[$PDK]}"
    step "Run precheck" precheck_run
    step --always "Test Summary" junit_summary tt/precheck/reports/results.xml
    step --always "Capture test results" bash -c 'cat tt/precheck/reports/results.md >> "$GITHUB_STEP_SUMMARY"'
    step --always "Upload reports" upload_artifact precheck_reports 'tt/precheck/reports/*'
    job_end
}

gl_pdk() { ciel_enable "$PDK_ROOT" "$(jq -r .PDK tt_submission/pdk.json)" "$(jq -r .PDK_VERSION tt_submission/pdk.json)"; }

gl_run() {
    export PATH="$PY_TEST:$PATH"
    cp tt_submission/*.v test/gate_level_netlist.v
    cd test
    rm -f tb.vcd tb.fst results.xml
    make clean
    GATES=yes make
    # `make` will return success even if the tests fail, so check for failed tests in results.xml.
    test -f results.xml
    ! grep -E '<(failure|error)' results.xml
}

job_gl_test() {
    job_begin gl_test gds gl_test
    step "Install PDK" gl_pdk
    step "Run tests" gl_run
    step --always "Upload Waveform and Test Results" upload_artifact gatelevel_test_results test/tb.vcd test/tb.fst test/results.xml 'test/output/*'
    step --always "Test Summary" junit_summary test/results.xml
    job_end
}

viewer_pages() {
    local pages="$JOB_DIR/artifacts/github-pages"
    mkdir -p "$pages"
    cp gds_render.png "$pages/" 2>/dev/null || echo "No gds_render.png"
    cp tt_submission/*.gds "$pages/tinytapeout.gds"
    cp tt_submission/*.oas "$pages/tinytapeout.oas"
    ls -l "$pages"
}

viewer_summary() {
    local base="jobs/viewer/artifacts/github-pages"
    local pdk; pdk=$(jq -r .PDK tt_submission/pdk.json)
    cat >>"$GITHUB_STEP_SUMMARY" <<EOF
# 3D Viewer
<a class="model-link" data-viewer="https://gds-viewer.tinytapeout.com/?pdk=$pdk&model=" data-model="$base/tinytapeout.oas" href="#">Open 3D viewer</a>

# Tiny Tapeout Explorer
<a class="model-link" data-viewer="https://gds-explorer.tinytapeout.com/viewer?pdk=$pdk&file=" data-model="$base/tinytapeout.oas" href="#">Open in Tiny Tapeout Explorer</a>

# 2D Preview
<img src="$base/gds_render.png" alt="GDS render" style="max-width:100%">
EOF
}

job_viewer() {
    job_begin viewer gds viewer
    step "Copy OAS And GDSII" viewer_pages
    step "Display preview in workflow summary" viewer_summary
    job_end
}

workflow_gds() {
    job_gds
    if [ "$(job_status gds)" = success ]; then
        job_viewer
        job_precheck
        job_gl_test
    else
        job_skip viewer gds viewer "needs gds, which did not succeed"
        job_skip precheck gds precheck "needs gds, which did not succeed"
        job_skip gl_test gds gl_test "needs gds, which did not succeed"
    fi
}

# --- main ---------------------------------------------------------------------
mkdir -p "$RUN_DIR/jobs"
for wf in "$@"; do
    case "$wf" in
        test) mkdir -p "$RUN_DIR/jobs/test" && echo test >"$RUN_DIR/jobs/test/workflow" && echo queued >"$RUN_DIR/jobs/test/status" ;;
        docs) mkdir -p "$RUN_DIR/jobs/docs" && echo docs >"$RUN_DIR/jobs/docs/workflow" && echo queued >"$RUN_DIR/jobs/docs/status" ;;
        gds) for j in gds viewer precheck gl_test; do mkdir -p "$RUN_DIR/jobs/$j" && echo gds >"$RUN_DIR/jobs/$j/workflow" && echo queued >"$RUN_DIR/jobs/$j/status"; done ;;
    esac
done
report

for wf in "$@"; do "workflow_$wf"; done

date -Iseconds >"$RUN_DIR/finished"
report

rc=0
for d in "$RUN_DIR"/jobs/*/; do
    [ "$(cat "$d/status")" = failure ] && rc=1
done
printf '\n%sJobs:%s\n' "$c_bold" "$c_off"
for d in "$RUN_DIR"/jobs/*/; do
    s=$(cat "$d/status")
    case "$s" in success) c=$c_green ;; failure) c=$c_red ;; *) c=$c_dim ;; esac
    printf '  %s%-9s%s %s / %s\n' "$c" "$s" "$c_off" "$(cat "$d/workflow")" "$(basename "$d")"
done
exit $rc
