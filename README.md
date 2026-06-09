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
* Graphics: Raylib
* Platform: Linux

## How to Run
1. Build Milestone 1: `make milestone1`
2. Run: `./dijkstra <filename>`

## Milestone 2
Build:
make milestone2

Run:
./sim <filename>

## Milestone 3
Build:
make milestone3

Run:
./sim <filename>

## Milestone 4 / 5 / 6
Build:
make milestone4
make milestone5
make milestone6

Run:
./sim <filename>

The advanced input format uses a `#travelers` section after the graph definition:
```
7 10
0 1 2
...edges...
#travelers 2
0 6
1 4
```
If the `#travelers` marker is omitted and only one source-destination pair is provided after the edges, the program falls back to legacy single-traveler input.
