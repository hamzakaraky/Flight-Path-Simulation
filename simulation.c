#include <stdio.h>
#include <unistd.h>

#include "simulation.h"

/**
 * Runs the movement simulation on the graph based on Dijkstra's path.
 * Requirements:
 * - 1 second wait at each intermediate node.
 * - Movement on edges split into W equal jumps.
 * - Each jump takes exactly 300ms.
 */
void run_simulation(Node path[], int path_size, Edge edges[], int edges_count) {
    printf("Starting simulation animation...\n");

    for (int i = 0; i < path_size; i++) {
        
        // 1. Behavior at Nodes: Wait for 1 full second at each intermediate node
        // This does not include the source (start) or destination (end) nodes.
        if (i > 0 && i < path_size - 1) {
            printf("Waiting at node %d for 1000ms...\n", path[i].id);
            sleep(1); // 1 second delay
        }

        // 2. Behavior on Edges: Movement to the next node in the path
        if (i < path_size - 1) {
            Node current = path[i];
            Node next = path[i+1];
            
            // Find the weight (W) of the edge connecting current and next nodes
            int W = 1; 
            for (int e = 0; e < edges_count; e++) {
                if (edges[e].src == current.id && edges[e].dest == next.id) {
                    W = edges[e].weight;
                    break;
                }
            }

            printf("Moving on edge from %d to %d (Weight W = %d)\n", current.id, next.id, W);
            
            // Split the edge into W equal jumps as per requirements
            for (int j = 1; j <= W; j++) {
                // Calculate position using linear interpolation for animation logic
                float ratio = (float)j / W;
                float currentX = current.x + ratio * (next.x - current.x);
                float currentY = current.y + ratio * (next.y - current.y);

                // Print the current jump and calculated coordinates[cite: 1]
                printf("  Jump %d/%d -> Position: (%.2f, %.2f)\n", j, W, currentX, currentY);
                
                // Each jump takes exactly 300ms[cite: 1]
                // usleep uses microseconds (300ms * 1000 = 300,000us)
                usleep(300 * 1000); 
            }
        }
    }
    
    // 3. Final Step: Display message after reaching the destination[cite: 1]
    printf("Simulation Complete: Destination Reached!\n");
}