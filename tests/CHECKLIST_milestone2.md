# Milestone 2 — GUI Display Checklist

Run: `./sim tests/inputs/test01_example.txt`

Use multiple input files from `tests/inputs/` to verify behavior is general,
not hardcoded for one graph.

---

## Required (per the PDF)

- [ ] Window opens without errors and stays open
- [ ] Program does not crash when closed (X button / ESC)
- [ ] All N nodes are visible on screen
- [ ] Each node shows its ID number (or station name)
- [ ] All M edges are visible on screen
- [ ] **Edges are drawn as ARROWS** showing direction (not plain lines)
- [ ] Each edge displays its weight as a label
- [ ] Nodes do not overlap each other
- [ ] Edge labels do not overlap nodes (must be readable)
- [ ] When two opposing edges exist (e.g. 0→1 and 1→0), both are visible
      and distinguishable

## Multiple input files

Try each of these and confirm the graph is rendered correctly:

- [ ] `test01_example.txt`           — 6 nodes, 8 edges
- [ ] `test05_direct_edge.txt`       — minimal (2 nodes)
- [ ] `test06_multiple_paths.txt`    — 5 nodes, parallel paths
- [ ] `test08_larger_graph.txt`      — 7 nodes, 10 edges
- [ ] A graph with 15 nodes (max per spec) — generate one if needed

## Stability

- [ ] Window resizes gracefully (or has a fixed reasonable size)
- [ ] No flicker, no rendering artifacts
- [ ] FPS is stable (raylib default 60 is fine)

## Submission requirements

- [ ] `make milestone2` builds successfully from clean state
- [ ] README mentions how to build & run milestone 2
- [ ] All 4 group members have commits in GitHub
- [ ] Tag `milestone2` exists on the submitted commit

## Notes

The PDF says *"מיקום הצמתים יחושב לבחירת הקבוצה"* — node placement is your
choice. Common options:
  - Circular layout (easy, looks decent for small N)
  - Grid layout
  - Force-directed (looks best, harder to implement)
  - Hardcoded coordinates from the input file (simplest)

For up to 15 nodes, a circular layout is usually clean enough.
