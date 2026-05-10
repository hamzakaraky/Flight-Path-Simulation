# Milestone 3 — Animation Timing Reference

The animation timing is fully deterministic per the spec:
- Each hop on an edge: **300ms**
- Pause at each intermediate node: **1000ms** (1 second)
- No pause at source or destination
- An edge of weight W = W hops

So given a Dijkstra path, you can compute the exact expected duration.

Formula:
```
duration = (sum of edge weights on path) * 300ms
         + (number of intermediate nodes) * 1000ms
```

Number of intermediate nodes = (path length in nodes) - 2.

## Reference times for each test input

Time these with a stopwatch from "press play" until "arrived" message.
Tolerance: ±200ms is fine. More than ±500ms off → bug.

| Input file               | Path              | Edge weights | Intermediate nodes | Expected time |
|--------------------------|-------------------|--------------|--------------------|---------------|
| test01_example.txt       | 0 → 2 → 5         | 2 + 10 = 12  | 1                  | 12·300 + 1·1000 = **4600ms** |
| test05_direct_edge.txt   | 0 → 1             | 7            | 0                  | 7·300 + 0 = **2100ms** |
| test06_multiple_paths.txt| 0 → 1 → 2 → 3 → 4 | 1+1+1+1 = 4  | 3                  | 4·300 + 3·1000 = **4200ms** |
| test08_larger_graph.txt  | 0 → 1 → 4 → 6     | 2 + 11 + 2 = 15 | 2               | 15·300 + 2·1000 = **6500ms** |
| m3_long_edge.txt         | 0 → 1             | 5            | 0                  | 5·300 + 0 = **1500ms** |
| m3_timing_simple.txt     | 0 → 1 → 2 → 3     | 2+2+2 = 6    | 2                  | 6·300 + 2·1000 = **3800ms** |
| m3_correct_path.txt      | 0 → 1 → 2 → 3 → 4 | 1+1+1+1 = 4  | 3                  | 4·300 + 3·1000 = **4200ms** |

**Critical correctness check** with `m3_correct_path.txt`:
There is also a direct edge `0 → 4` with weight 10, but Dijkstra must prefer
the longer 4-edge path with total weight 4. If your animation goes straight
across (0 → 4 directly), your Dijkstra is wrong, not your animation.
Watching this case is the cheapest way to catch a path-selection bug.

## Common timing bugs

- **Total time = (W) × 300ms instead of (W) × 300 + pauses** → forgot the
  intermediate-node pause.
- **Total time = (W) × 300ms × frame_rate / 60** → animation tied to FPS
  instead of wall-clock. Use `GetFrameTime()` to accumulate elapsed time.
- **Pause at source or destination** → off by 1 or 2 seconds. The spec is
  clear: pauses are only at *intermediate* nodes.
- **Edge of weight W animated as 1 hop instead of W hops** → animation
  finishes way too fast and looks the same regardless of weight.
