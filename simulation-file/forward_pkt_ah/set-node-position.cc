// set node's topology
#include "set-node-position.h"

void
set_mobility_grid(NodeContainer& node_container,
                  const double node_distance,
                  const unsigned int grid_width)
{
    // assign node's position and movement
    MobilityHelper mobility;
    // (MinX, MinY) is the starting point of node0
    // GridWidth means how many nodes per row or column
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX",
                                  DoubleValue(0.0),
                                  "MinY",
                                  DoubleValue(0.0),
                                  "DeltaX",
                                  DoubleValue(node_distance),
                                  "DeltaY",
                                  DoubleValue(-1 * node_distance),
                                  "GridWidth",
                                  UintegerValue(grid_width),
                                  "LayoutType",
                                  StringValue("RowFirst"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(node_container);
    return;
}

void
set_mobility_random_disc(NodeContainer& node_container, const double x, const double y)
{
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::RandomDiscPositionAllocator",
                                  "X",
                                  DoubleValue(x),
                                  "Y",
                                  DoubleValue(y),
                                  "Rho",
                                  StringValue("ns3::UniformRandomVariable[Min=0|Max=1200]"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(node_container);
    return;
}

void
set_mobility_rectangle(NodeContainer& node_container,
                       const double min_x,
                       const double min_y,
                       const double max_x,
                       const double max_y)
{
    Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
    x->SetAttribute("Min", DoubleValue(min_x));
    x->SetAttribute("Max", DoubleValue(max_x));

    Ptr<UniformRandomVariable> y = CreateObject<UniformRandomVariable>();
    y->SetAttribute("Min", DoubleValue(min_y));
    y->SetAttribute("Max", DoubleValue(max_y));

    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
                                  "X",
                                  PointerValue(x),
                                  "Y",
                                  PointerValue(y));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(node_container);
}

void
set_mobility_hexagon(NodeContainer& node_container,
                     const unsigned int total_nodes,
                     const double unit_distance,
                     const unsigned int grid_width,
                     const double start_x,
                     const double start_y)
{
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    const double xydfactor = std::sqrt(0.75);
    double yd = xydfactor * unit_distance;
    for (unsigned int row = 0, n = 0; n < total_nodes; ++row)
    {
        double x = start_x - (row % 2) * 0.5 * unit_distance;
        double y = row * yd;
        for (unsigned int col = 0; col < grid_width + (row % 2) && n < total_nodes; ++col)
        {
            positionAlloc->Add(Vector(x, y, 0));
            x += unit_distance;
            ++n;
        }
    }
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(node_container);
}

void
set_mobility_file(NodeContainer& node_container)
{
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0, 0, 0));
    positionAlloc->Add(Vector(492, 20, 0));
    positionAlloc->Add(Vector(405, 242, 0));
    positionAlloc->Add(Vector(432, 565, 0));
    positionAlloc->Add(Vector(230, 666, 0));
    positionAlloc->Add(Vector(459, 841, 0));
    positionAlloc->Add(Vector(1037, 215, 0));
    positionAlloc->Add(Vector(533, 1077, 0));
    positionAlloc->Add(Vector(492, 1272, 0));
    positionAlloc->Add(Vector(936, 1211, 0));
    positionAlloc->Add(Vector(1084, 1480, 0));
    positionAlloc->Add(Vector(1246, 1366, 0));
    positionAlloc->Add(Vector(1488, 707, 0));
    positionAlloc->Add(Vector(196, -1695, 0));
    positionAlloc->Add(Vector(1697, 834, 0));
    positionAlloc->Add(Vector(1865, 619, 0));
    positionAlloc->Add(Vector(22, -800, 0));
    positionAlloc->Add(Vector(75, -1372, 0));
    positionAlloc->Add(Vector(-12, -1641, 0));
    positionAlloc->Add(Vector(1757, 982, 0));
    positionAlloc->Add(Vector(903, -706, 0));
    positionAlloc->Add(Vector(1542, -538, 0));
    positionAlloc->Add(Vector(1872, -383, 0));
    positionAlloc->Add(Vector(1522, -861, 0));
    positionAlloc->Add(Vector(1172, -1372, 0));
    positionAlloc->Add(Vector(1703, -1325, 0));
    positionAlloc->Add(Vector(-604, -242, 0));
    positionAlloc->Add(Vector(-665, -605, 0));
    positionAlloc->Add(Vector(-1290, -94, 0));
    positionAlloc->Add(Vector(-1297, -383, 0));
    positionAlloc->Add(Vector(-1707, -451, 0));
    positionAlloc->Add(Vector(-1169, -700, 0));
    positionAlloc->Add(Vector(-1432, -874, 0));
    positionAlloc->Add(Vector(-1681, -1359, 0));
    positionAlloc->Add(Vector(-1485, -1588, 0));
    positionAlloc->Add(Vector(-1176, -1258, 0));
    positionAlloc->Add(Vector(-416, 363, 0));
    positionAlloc->Add(Vector(-53, 713, 0));
    positionAlloc->Add(Vector(-214, 1204, 0));
    positionAlloc->Add(Vector(-510, 1494, 0));
    positionAlloc->Add(Vector(-638, 942, 0));
    positionAlloc->Add(Vector(-523, 814, 0));
    positionAlloc->Add(Vector(-786, 34, 0));
    positionAlloc->Add(Vector(-1068, 81, 0));
    positionAlloc->Add(Vector(-1109, 411, 0));
    positionAlloc->Add(Vector(-1526, 303, 0));
    positionAlloc->Add(Vector(-1559, 565, 0));
    positionAlloc->Add(Vector(-1351, 760, 0));
    positionAlloc->Add(Vector(-1102, 1157, 0));
    positionAlloc->Add(Vector(-1472, 1184, 0));
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(node_container);
}
