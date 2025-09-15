#pragma once

#include "ns3/core-module.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/node-container.h"

using namespace ns3;

void set_mobility_grid(NodeContainer& node_container,
                       const double node_distance,
                       const unsigned int grid_width);

void set_mobility_random_disc(NodeContainer& node_container, const double x, const double y);
/* disk topology
const double disc_center_x = 1200.0;
const double disc_center_y = 1200.0;
set_mobility_random_disc(node_container, disc_center_x, disc_center_y);
*/

void set_mobility_rectangle(NodeContainer& node_container,
                            const double min_x,
                            const double min_y,
                            const double max_x,
                            const double max_y);
/* rectangle topology
 * const double min_x = 0.0, min_y = 0.0;
 * const double max_x = 1000.0, max_y = 1000.0;
 * set_mobility_rectangle(node_container, min_x, min_y, max_x, max_y);
 */

void set_mobility_hexagon(NodeContainer& node_container,
                          const unsigned int total_nodes,
                          const double unit_distance,
                          const unsigned int grid_width,
                          const double start_x,
                          const double start_y);
/* hexagon topology
const double start_x = 0.0;
const double start_y = 0.0;
set_mobility_hexagon(node_container, num_nodes, node_distance, grid_width, start_x, start_y);
*/

// set mobility defined in node_position.txt
void set_mobility_file(NodeContainer& node_container);
