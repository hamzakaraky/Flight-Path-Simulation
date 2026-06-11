# Flight Path Simulation - OS Project

## Project Overview
This project simulates a global flight network where airports are nodes and flight paths are edges. 
The system calculates the most efficient route using Dijkstra's algorithm.

## Team Members
- mahmoud malhi - Dijkstra algorithm and shortest path testing
- hamza karaky - Raylib GUI and graph drawing
- ibrahem hroub - OS integration and file parsing
- qais hijazi - Testing, documentation, and GitHub management

## Technical Stack
* Language: C
* Graphics: Raylib (UCRT64 build from MSYS2)
* Platform: Windows (MSYS2 UCRT64)

## Prerequisites (Windows / MSYS2 UCRT64)

Install Raylib via the MSYS2 UCRT64 shell:
```bash
pacman -S mingw-w64-ucrt-x86_64-raylib
```

The Makefile uses `/c/msys64/mingw64/bin/gcc` with headers and libs from
`/c/msys64/ucrt64/include` and `/c/msys64/ucrt64/lib`. Adjust `CC_WIN`,
`IFLAGS`, and `LFLAGS` in the Makefile if your MSYS2 is installed elsewhere.

## Milestone 1 — Dijkstra CLI

Build:
```bash
make milestone1
```
Run:
```bash
./dijkstra <input_file>
```
Run automated tests:
```bash
bash tests/run_tests.sh ./dijkstra
```

## Milestone 2 — Graph GUI

Build:
```bash
make milestone2
```
Run:
```bash
./sim.exe <input_file>
```

## Milestone 3 — Animated Traveler

Build:
```bash
make milestone3
```
Run:
```bash
./sim.exe <input_file>
```

## Milestone 4 — Multi-Traveler with fork()

Each traveler runs in its own child process. The parent animates all travelers
simultaneously using pre-computed paths.

Build:
```bash
make milestone4
```
Run:
```bash
./sim.exe multi.txt
```

## Milestone 5 — IPC via pipes

Child processes send real-time position/state updates to the parent over
non-blocking pipes. The GUI reflects live child process state.

Build:
```bash
make milestone5
```
Run:
```bash
./sim.exe multi.txt
```

## Milestone 6 — Semaphore-based node locking

Adds one process-shared semaphore per node (via mmap + sem_init pshared=1).
Travelers must acquire the destination node's semaphore before entering it,
preventing simultaneous occupation.

Build:
```bash
make milestone6
```
Run:
```bash
./sim.exe sync_test.txt    # stress test: 3 travelers competing for node 2
./sim.exe multi.txt        # 3 travelers, 7-node graph
```

## Input Format

### Single traveler (legacy)
```
<nodes> <edges>
src dst weight
...
src dst
```

### Multiple travelers
```
<nodes> <edges>
src dst weight
...
<traveler_count>
src dst
src dst
```
Or with explicit header:
```
#travelers <count>
src dst
```

Console output format: `[PID=XXXX] arrived at node N | next node: M`
