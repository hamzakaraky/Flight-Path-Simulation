# Milestone 3 — Animation Checklist

Run: `./sim tests/inputs/test01_example.txt`

Expected path for `test01_example.txt`: **0 → 2 → 5**, total weight **12**
(2 + 10).

---

## Required (per the PDF)

### Static display
- [ ] Graph from milestone 2 is still drawn in the background during animation
- [ ] Source node is clearly identifiable
- [ ] Destination node is clearly identifiable
- [ ] (Optional but nice) The Dijkstra path is highlighted

### Entity at source
- [ ] An icon / colored circle appears at the source node
- [ ] It is visually distinct from the static graph nodes

### Play / Stop button
- [ ] A button labeled play / stop (or pause) is visible on screen
- [ ] Clicking it starts the animation
- [ ] Clicking it again pauses the animation
- [ ] Clicking it again resumes
- [ ] Button label or icon updates to reflect current state

### Movement on edges
- [ ] Entity moves along the Dijkstra path edges (not in straight lines
      across the canvas, not through non-path nodes)
- [ ] An edge of weight W is traversed in **W discrete hops**
- [ ] **Each hop takes 300ms** — verify by timing a long edge.
      Test with `test01`: edge 2→5 has weight 10, so traversing it should
      take ≈ 10 × 300ms = **3.0 seconds**.
- [ ] Heavier edges take proportionally longer than lighter ones

### Behavior at intermediate nodes
- [ ] Entity **pauses for 1 full second** at each intermediate node
- [ ] No pause at the source node when starting
- [ ] No pause at the destination after arrival (just the arrival message)

### Total timing for `test01_example.txt` (path 0 → 2 → 5)
- [ ] Edge 0→2 (weight 2): 2 × 300ms = 600ms
- [ ] Pause at node 2: 1000ms
- [ ] Edge 2→5 (weight 10): 10 × 300ms = 3000ms
- [ ] **Total ≈ 4.6 seconds** from press play to arrival

### Arrival
- [ ] Message appears on screen when entity reaches destination
      (e.g. "Arrived" / "Reached destination")
- [ ] Message is clearly visible, not hidden behind a node

## Edge cases to spot-check

- [ ] `test02_same_src_dst.txt` (0→0): no movement needed,
      arrival message shows immediately (or refuse politely)
- [ ] `test03_disconnected.txt`: animation does not start;
      "No path found" or similar shown
- [ ] `test05_direct_edge.txt` (single edge weight 7): one continuous
      traversal of 7 × 300ms = 2.1s, no intermediate pause

## Stability
- [ ] Animation is smooth (no visible jumps or stutters)
- [ ] Pausing mid-edge then resuming continues from the same hop
- [ ] No memory growth over repeated play/pause cycles

## Submission requirements (per spec — first graded submission)

- [ ] `make milestone3` builds successfully from clean state
- [ ] `make clean` removes all build artifacts
- [ ] README documents build & run for milestones 1, 2, **and** 3
- [ ] Tag `milestone3` pushed to GitHub
- [ ] Short video of the animation recorded
- [ ] Sample input file included
- [ ] Submitted on Moodle: GitHub link + video + sample input

## Common pitfalls

- Confusing "300ms per hop" with "300ms per edge" — a weight-5 edge takes
  5 × 300 = 1500ms, not 300ms.
- Forgetting the 1-second node pause (or applying it to source/destination).
- Animation timing tied to frame rate instead of wall-clock — always use
  `GetFrameTime()` or compare against a stored timestamp.
- Entity teleporting between hops instead of interpolating — interpolation
  is not strictly required by the spec ("hops"), but jitter-free hops are.
