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
./sim <input_file>
```

## Milestone 3 — Animated Traveler

Build:
```bash
make milestone3
```
Run:
```bash
./sim <input_file>
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
./sim multi.txt
```

## Milestone 5 — IPC via pipes

* **IPC Mechanism**: Non-blocking anonymous pipes. For each traveler, the parent process creates a pipe (`pipe()`) and forks a child process. The child process writes state updates (`IPCMessage` structs containing PID, current/next node, coordinates, state, and events) to its write-end, while the parent reads them from its read-end.
* **Non-blocking Reads**: The parent sets the read-end of each pipe to non-blocking mode (`O_NONBLOCK` via `fcntl`) to keep the GUI rendering smoothly at 60 FPS without waiting for child writes.
* **GUI Updates**: The parent processes child updates in real-time and updates the UI accordingly.

Build:
```bash
make milestone5
```
Run:
```bash
./sim multi.txt
```

## Milestone 6 — Semaphore-based node locking

* **Synchronization Mechanism**: Process-shared POSIX semaphores (`sem_t`).
* **Shared Memory Allocation**: The parent allocates memory for `MAX_NODES` semaphores in a shared memory region using anonymous memory mapping (`mmap` with `MAP_SHARED | MAP_ANONYMOUS`).
* **Semaphore Initialization**: Each semaphore is initialized with `pshared = 1` (via `sem_init`), allowing it to be shared between the parent and child processes.
* **Locking Logic**: Before entering any node (including intermediate and destination nodes), the traveler process calculates a waiting position outside the node border, updates the parent to display the traveler at this waiting position in a flashing `STATE_BLOCKED` state, and calls `sem_wait(&node_sems[node_id])`.
* **Critical Section**: Once the semaphore is acquired, the traveler enters the node, notifies the parent of its arrival, stays inside for exactly 1.0 second (`sleepMs(1000)`), and then releases the lock using `sem_post(&node_sems[node_id])`.
* **Stress Test**: A test input `sync_test.txt` is provided to simulate 3 travelers competing for the same node (node 2) simultaneously, demonstrating mutual exclusion, queueing outside the node, and sequential entry.

Build:
```bash
make milestone6
```
Run:
```bash
./sim sync_test.txt    # stress test: 3 travelers competing for node 2
./sim multi.txt        # 3 travelers, 7-node graph
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
