#!/bin/bash
# ============================================================
# Milestone 1 Test Runner - Dijkstra
# ============================================================
# Usage: ./run_tests.sh [path/to/dijkstra_executable]
# Default: ./dijkstra
# ============================================================

DIJKSTRA="${1:-./dijkstra}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT_DIR="$SCRIPT_DIR/inputs"
EXPECTED_DIR="$SCRIPT_DIR/expected"

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
if [ ! -x "$DIJKSTRA" ]; then
    echo -e "${RED}ERROR:${NC} executable '$DIJKSTRA' not found or not executable"
    echo "Build with: make milestone1"
    echo "Then run:   $0 [path/to/dijkstra]"
    exit 1
fi

echo "==========================================="
echo "  Milestone 1 — Dijkstra Test Suite"
echo "==========================================="
echo "Executable: $DIJKSTRA"
echo ""

# ---------- normal pass/fail tests ----------
# Each entry: "test_id|description"
TESTS=(
    "test01_example|Example from PDF (6 nodes, 0->5)"
    "test02_same_src_dst|Source equals destination (0->0)"
    "test03_disconnected|Disconnected graph (no path)"
    "test04_directed_no_reverse|Directed: no reverse edge exists"
    "test05_direct_edge|Single direct edge (0->1)"
    "test06_multiple_paths|Multiple paths, must pick shortest"
    "test08_larger_graph|7-node graph (0->6)"
)

run_diff_test () {
    local id="$1"
    local desc="$2"
    local input="$INPUT_DIR/$id.txt"
    local expected="$EXPECTED_DIR/$id.txt"

    printf "%-45s " "[$id]"

    if [ ! -f "$input" ]; then
        echo -e "${YELLOW}SKIP${NC} (missing input)"
        return
    fi
    if [ ! -f "$expected" ]; then
        echo -e "${YELLOW}SKIP${NC} (missing expected)"
        return
    fi

    # Run with 5-second timeout to catch infinite loops
    actual=$(timeout 5s "$DIJKSTRA" "$input" 2>&1)
    rc=$?

    if [ $rc -eq 124 ]; then
        echo -e "${RED}FAIL${NC} (timeout — possible infinite loop)"
        FAIL=$((FAIL+1))
        FAILED_TESTS+=("$id: $desc")
        return
    fi

    expected_content=$(cat "$expected")

    # Normalize trailing whitespace on each line + drop blank trailing lines
    actual_norm=$(echo "$actual" | sed 's/[[:space:]]*$//' | sed -e :a -e '/^$/{$d;N;ba' -e '}')
    expected_norm=$(echo "$expected_content" | sed 's/[[:space:]]*$//' | sed -e :a -e '/^$/{$d;N;ba' -e '}')

    if [ "$actual_norm" = "$expected_norm" ]; then
        echo -e "${GREEN}PASS${NC}  ($desc)"
        PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${NC}  ($desc)"
        echo -e "  ${BLUE}expected:${NC}"
        echo "$expected_content" | sed 's/^/    /'
        echo -e "  ${BLUE}actual:${NC}"
        echo "$actual" | sed 's/^/    /'
        FAIL=$((FAIL+1))
        FAILED_TESTS+=("$id: $desc")
    fi
}

for t in "${TESTS[@]}"; do
    IFS='|' read -r id desc <<< "$t"
    run_diff_test "$id" "$desc"
done

# ---------- negative-input test (must error, not crash, not produce a valid path) ----------
echo ""
echo "--- Error-handling tests ---"

run_error_test () {
    local id="$1"
    local desc="$2"
    local input="$INPUT_DIR/$id.txt"

    printf "%-45s " "[$id]"

    actual=$(timeout 5s "$DIJKSTRA" "$input" 2>&1)
    rc=$?

    if [ $rc -eq 124 ]; then
        echo -e "${RED}FAIL${NC} (timeout)"
        FAIL=$((FAIL+1))
        FAILED_TESTS+=("$id: $desc")
        return
    fi
    if [ $rc -eq 139 ] || [ $rc -eq 134 ]; then
        echo -e "${RED}FAIL${NC} (segfault/abort, rc=$rc)"
        FAIL=$((FAIL+1))
        FAILED_TESTS+=("$id: $desc")
        return
    fi

    # Must NOT print a path like "0 -> 1" or a valid weight-only output.
    # We expect an error message — anything containing "error", "invalid",
    # "negative", "illegal" (case-insensitive) will be accepted.
    if echo "$actual" | grep -qiE 'error|invalid|negative|illegal|bad'; then
        echo -e "${GREEN}PASS${NC}  ($desc — error reported)"
        PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${NC}  ($desc — expected an error message)"
        echo -e "  ${BLUE}actual:${NC}"
        echo "$actual" | sed 's/^/    /'
        FAIL=$((FAIL+1))
        FAILED_TESTS+=("$id: $desc")
    fi
}

run_error_test "test07_negative_weight" "Negative weight rejected"

# ---------- memory-leak test (optional — only if valgrind installed) ----------
echo ""
echo "--- Memory leak check (valgrind, optional) ---"
if command -v valgrind >/dev/null 2>&1; then
    printf "%-45s " "[valgrind on test01]"
    leak_out=$(valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=definite \
        "$DIJKSTRA" "$INPUT_DIR/test01_example.txt" 2>&1 >/dev/null)
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}PASS${NC}  (no definite leaks)"
        PASS=$((PASS+1))
    else
        echo -e "${RED}FAIL${NC}  (leaks or memory errors detected)"
        echo "$leak_out" | tail -20 | sed 's/^/    /'
        FAIL=$((FAIL+1))
        FAILED_TESTS+=("valgrind: memory errors")
    fi
else
    echo "valgrind not installed — skipping. Install: sudo apt install valgrind"
fi

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
echo "==========================================="

[ $FAIL -eq 0 ]
