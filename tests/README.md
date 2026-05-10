# Test Suite — Operating Systems Project

Tests for milestones 1, 2, and 3 of the graph-traversal simulation project.

## Layout

```
tests/
├── run_tests.sh                  # Automated runner for milestone 1
├── inputs/                       # Test input files (graph + query)
│   ├── test01_example.txt        # The example from the PDF
│   ├── test02_same_src_dst.txt   # 0 -> 0 edge case
│   ├── test03_disconnected.txt   # No path exists
│   ├── test04_directed_no_reverse.txt  # Verifies directed semantics
│   ├── test05_direct_edge.txt    # Single edge
│   ├── test06_multiple_paths.txt # Must pick shortest, not first found
│   ├── test07_negative_weight.txt # Should be rejected
│   └── test08_larger_graph.txt   # 7 nodes, 10 edges
├── expected/                     # Expected outputs (one per test that has fixed output)
├── CHECKLIST_milestone2.md       # Manual visual checklist for GUI
└── CHECKLIST_milestone3.md       # Manual visual checklist for animation
```

## Milestone 1 — automated

From the project root:

```bash
make milestone1
./tests/run_tests.sh ./dijkstra
```

What it covers:
1. Canonical example from the PDF
2. Source equals destination → prints node id + 0
3. Disconnected graph → "No path found"
4. Directed semantics → reverse direction has no path
5. Single direct edge
6. Multiple paths → must select the shortest
7. Negative-weight rejection (must report an error, not crash)
8. Larger 7-node graph
9. Memory leaks (only if `valgrind` is installed — auto-skipped otherwise)

The runner uses a 5-second timeout per test to catch infinite loops, and
checks for segfaults / aborts explicitly.

Output normalization: trailing whitespace and trailing blank lines are
trimmed before comparing, so your code is allowed to print an extra `\n` at
the end.

### What "PASS" means for the negative-weight test

Test 7 expects your program to print *some* error message containing one
of: `error`, `invalid`, `negative`, `illegal`, `bad` (case-insensitive),
**not** to crash, and **not** to produce a normal Dijkstra-looking output.
This matches the spec's wording (negative numbers are invalid input — print
an appropriate error message).

## Milestones 2 & 3 — manual

These involve a GUI and animation, so they cannot be diffed against
expected output. Open the matching checklist file and tick items as you
verify them visually:

- `CHECKLIST_milestone2.md` — graph display
- `CHECKLIST_milestone3.md` — animation, including the timing math you
  should observe (e.g. `test01_example` should take ≈ 4.6 seconds end-to-end).

## Adding your own tests

Add `tests/inputs/myTest.txt` and `tests/expected/myTest.txt`, then
register it in `run_tests.sh` inside the `TESTS=( ... )` array as
`"myTest|description"`.
