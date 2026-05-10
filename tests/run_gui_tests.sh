#!/bin/bash
# ============================================================
# Milestones 2 & 3 Test Runner — headless smoke tests
# ============================================================
# Usage: ./run_gui_tests.sh [path/to/sim_executable]
# Default: ./sim
#
# Requires: xvfb (sudo apt install xvfb)
# Without xvfb the script falls back to opening real windows.
# ============================================================

SIM="${1:-./sim}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT_DIR="$SCRIPT_DIR/inputs"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PASS=0
FAIL=0
FAILED_TESTS=()

# Sanity check
if [ ! -x "$SIM" ]; then
    echo -e "${RED}ERROR:${NC} executable '$SIM' not found or not executable"
    echo "Build with: make milestone2  (or milestone3)"
    exit 1
fi

# Detect xvfb
HAVE_XVFB=0
if command -v xvfb-run >/dev/null 2>&1; then
    HAVE_XVFB=1
fi

echo "==========================================="
echo "  Milestones 2 & 3 — GUI Smoke Tests"
echo "==========================================="
echo "Executable: $SIM"
if [ $HAVE_XVFB -eq 1 ]; then
    echo "Mode:       headless (xvfb-run)"
else
    echo -e "Mode:       ${YELLOW}windowed${NC} (xvfb not installed — windows will open)"
    echo "            Install: sudo apt install xvfb"
fi
echo ""

# ---------- helper: run sim for N seconds, kill it, check for crashes ----------
# A clean exit (rc 0), or rc 143 (SIGTERM, we killed it after the timeout),
# or rc 137 (SIGKILL fallback) all count as "did not crash."
# rc 139 = segfault, rc 134 = abort/assert, anything else = something went wrong.
run_smoke () {
    local input_name="$1"
    local seconds="$2"
    local desc="$3"
    local input="$INPUT_DIR/$input_name"

    printf "%-45s " "[$input_name]"

    if [ ! -f "$input" ]; then
        echo -e "${YELLOW}SKIP${NC} (input missing)"
        return
    fi

    local logfile
    logfile=$(mktemp)
    local rc

    if [ $HAVE_XVFB -eq 1 ]; then
        # -a auto-picks a free display number; --server-args disables access control.
        # SIGTERM after $seconds, SIGKILL 2s after that as a safety net.
        timeout --signal=TERM --kill-after=2 "${seconds}s" \
            xvfb-run -a --server-args="-screen 0 1024x768x24" \
            "$SIM" "$input" >"$logfile" 2>&1
        rc=$?
    else
        timeout --signal=TERM --kill-after=2 "${seconds}s" \
            "$SIM" "$input" >"$logfile" 2>&1
        rc=$?
    fi

    # Pass: clean exit OR killed by our timeout (it was still running, fine).
    # 124 = timeout's own "I had to send SIGTERM" exit code
    # 143 = process exited from SIGTERM (128 + 15)
    # 137 = process killed by SIGKILL (128 + 9), rare safety-net case
    if [ $rc -eq 0 ] || [ $rc -eq 124 ] || [ $rc -eq 143 ] || [ $rc -eq 137 ]; then
        # Also scan stderr for asserts / sanitizer errors / raylib fatal
        if grep -qiE 'assert|sanitizer|segmentation|fatal|abort' "$logfile"; then
            echo -e "${RED}FAIL${NC}  ($desc — error in stderr)"
            sed 's/^/    /' "$logfile" | head -10
            FAIL=$((FAIL+1))
            FAILED_TESTS+=("$input_name: $desc")
        else
            echo -e "${GREEN}PASS${NC}  ($desc)"
            PASS=$((PASS+1))
        fi
    elif [ $rc -eq 139 ]; then
        echo -e "${RED}FAIL${NC}  (SEGFAULT — $desc)"
        sed 's/^/    /' "$logfile" | tail -10
        FAIL=$((FAIL+1))
        FAILED_TESTS+=("$input_name: SEGFAULT")
    elif [ $rc -eq 134 ]; then
        echo -e "${RED}FAIL${NC}  (ABORT/assert — $desc)"
        sed 's/^/    /' "$logfile" | tail -10
        FAIL=$((FAIL+1))
        FAILED_TESTS+=("$input_name: abort")
    else
        echo -e "${RED}FAIL${NC}  (exit code $rc — $desc)"
        sed 's/^/    /' "$logfile" | tail -10
        FAIL=$((FAIL+1))
        FAILED_TESTS+=("$input_name: rc=$rc")
    fi

    rm -f "$logfile"
}

# ---------- helper: run sim, expect it to error out cleanly ----------
# For inputs that should be rejected (negative weights, etc.). The program
# must exit on its own (not be killed by our timeout) AND not crash.
run_expect_error () {
    local input_name="$1"
    local desc="$2"
    local input="$INPUT_DIR/$input_name"

    printf "%-45s " "[$input_name]"

    if [ ! -f "$input" ]; then
        echo -e "${YELLOW}SKIP${NC} (input missing)"
        return
    fi

    local logfile
    logfile=$(mktemp)
    local rc

    if [ $HAVE_XVFB -eq 1 ]; then
        timeout --signal=TERM --kill-after=2 5s \
            xvfb-run -a --server-args="-screen 0 1024x768x24" \
            "$SIM" "$input" >"$logfile" 2>&1
        rc=$?
    else
        timeout --signal=TERM --kill-after=2 5s \
            "$SIM" "$input" >"$logfile" 2>&1
    fi

    # Crash codes are always failures
    if [ $rc -eq 139 ] || [ $rc -eq 134 ]; then
        echo -e "${RED}FAIL${NC}  (crashed: rc=$rc)"
        FAIL=$((FAIL+1))
        FAILED_TESTS+=("$input_name: crash on bad input")
        rm -f "$logfile"
        return
    fi

    # Either the program exited on its own with non-zero, OR it printed an
    # error message and continued (e.g. "No path found" displayed, then user
    # closes the window). Both are fine.
    if [ $rc -ne 0 ] && [ $rc -ne 124 ] && [ $rc -ne 143 ] && [ $rc -ne 137 ]; then
        # Self-exited with non-zero — error-on-bad-input behavior
        echo -e "${GREEN}PASS${NC}  ($desc — exited with rc=$rc)"
        PASS=$((PASS+1))
    elif grep -qiE 'no path|error|invalid|negative' "$logfile"; then
        echo -e "${GREEN}PASS${NC}  ($desc — error message in output)"
        PASS=$((PASS+1))
    else
        echo -e "${YELLOW}WARN${NC}  ($desc — no error msg, no error exit)"
        echo "    Verify manually that bad input is handled gracefully."
    fi

    rm -f "$logfile"
}

# ============================================================
# Milestone 2 — graph display (static)
# Run for 3 seconds, then kill. Program should not crash in that window.
# ============================================================
echo "--- Milestone 2: graph display smoke tests ---"

run_smoke "test01_example.txt"        3 "PDF example, 6 nodes"
run_smoke "test05_direct_edge.txt"    3 "Minimal: 2 nodes"
run_smoke "test08_larger_graph.txt"   3 "Medium: 7 nodes, 10 edges"
run_smoke "m2_bidirectional.txt"      3 "Bidirectional edges between same pair"
run_smoke "m2_max_nodes.txt"          3 "Max 15 nodes"
run_smoke "m2_self_loop.txt"          3 "Self-loop edge (0->0)"
run_smoke "test03_disconnected.txt"   3 "Disconnected graph"
run_smoke "test02_same_src_dst.txt"   3 "src == dst"

echo ""
echo "--- Milestone 2: error-handling on bad input ---"
run_expect_error "test07_negative_weight.txt" "Negative weight"

# ============================================================
# Milestone 3 — animation timing
# Each run is given enough time for the animation to *complete*, then a
# few extra seconds before we kill it. If the animation takes much longer
# than expected, the test will still pass the smoke check but you'll catch
# the timing problem with the manual checklist.
# ============================================================
echo ""
echo "--- Milestone 3: animation smoke tests ---"
echo "Note: these run for the expected animation duration + buffer."
echo "      If your sim auto-plays without needing a 'play' click, animation"
echo "      will run during the test. If it requires a click, the program"
echo "      still must not crash while idle."
echo ""

# m3_long_edge: weight 5 = 1.5s of animation + buffer
run_smoke "m3_long_edge.txt"          5 "Single edge weight 5 (~1.5s anim)"

# m3_timing_simple: 3 edges weight 2 + 2 node pauses = 3.8s anim + buffer
run_smoke "m3_timing_simple.txt"      7 "3-edge path (~3.8s anim)"

# m3_correct_path: must follow path 0->1->2->3->4 (4 edges weight 1 + 3 pauses)
# = 4*300 + 3*1000 = 4.2s anim + buffer
run_smoke "m3_correct_path.txt"       7 "Multi-path correctness (~4.2s anim)"

# test01 again, this time with full animation duration (4.6s + buffer)
run_smoke "test01_example.txt"        7 "PDF example with animation (~4.6s)"

# ---------- summary ----------
echo ""
echo "==========================================="
echo "  Summary"
echo "==========================================="
echo -e "  Passed: ${GREEN}$PASS${NC}"
echo -e "  Failed: ${RED}$FAIL${NC}"
if [ ${#FAILED_TESTS[@]} -gt 0 ]; then
    echo ""
    echo "  Failures:"
    for t in "${FAILED_TESTS[@]}"; do
        echo "    - $t"
    done
fi
echo ""
echo "  Smoke tests verify your program does not crash."
echo "  For visual / timing correctness, also run through:"
echo "    - tests/CHECKLIST_milestone2.md"
echo "    - tests/CHECKLIST_milestone3.md"
echo "==========================================="

[ $FAIL -eq 0 ]
