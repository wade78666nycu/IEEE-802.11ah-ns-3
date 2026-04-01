
#include "../common-exp/shared-recv-packet.h"
#include "../common-exp/shared-send-packet.h"
#include "../common-exp/shared-trace.h"

#include "ns3/core-module.h"

NS_LOG_COMPONENT_DEFINE("Lab");

#include "../common-exp/scenario-core.h"

#include <cmath>

using namespace ns3;

static void
set_mobility_grid(NodeContainer& node_container, const ScenarioConfig& cfg)
{
    const unsigned int node_per_row = static_cast<unsigned int>(std::sqrt(cfg.num_nodes));
    NS_ASSERT_MSG(node_per_row > 0, "num_nodes must be > 0 for grid topology");

    const double grid_len = 2400.0;
    const double node_distance = grid_len / node_per_row;

    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX",
                                  DoubleValue(0.0),
                                  "MinY",
                                  DoubleValue(0.0),
                                  "DeltaX",
                                  DoubleValue(node_distance),
                                  "DeltaY",
                                  DoubleValue(-node_distance),
                                  "GridWidth",
                                  UintegerValue(node_per_row),
                                  "LayoutType",
                                  StringValue("RowFirst"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(node_container);
}

static void
select_grid_pairs(const ScenarioConfig&, std::vector<unsigned int>& src_node_vec, std::vector<unsigned int>& dst_node_vec)
{
    src_node_vec = {5, 6, 7,8,9};
    dst_node_vec = {15,16,17,18,19};
}

int
main(int argc, char* argv[])
{
    ScenarioConfig cfg;
    cfg.scenario_name = "grid";
    cfg.num_nodes = 25;
    cfg.rand_seed = 2;

    CommandLine cmd;
    cmd.AddValue("num_nodes", "total number of nodes", cfg.num_nodes);
    cmd.AddValue("device_num", "number of devices on each node", cfg.device_num);
    cmd.AddValue("send_packet_num", "numbers of packets to send", cfg.send_packet_num);
    cmd.AddValue("gradpc_type", "method of gradpc to use", cfg.gradpc_type);
    cmd.AddValue("export_node_info", "export node's position and power", cfg.export_node_info);
    cmd.AddValue("tx_power", "tx power", cfg.default_tx_power);
    cmd.AddValue("rx_noise_figure", "background Noise Figure", cfg.rx_noise_figure);
    cmd.AddValue("routing_method", "routing method used", cfg.routing_method);
    cmd.AddValue("hello_warmup", "warmup time (seconds) used for hello/ETT before data phase", cfg.hello_warmup_seconds);
    cmd.AddValue("enable_hello", "set true to enable hello beacon (ETX measurement)", cfg.enable_hello);
    cmd.AddValue("enable_power_control", "set false to disable tx power control (keep same power across channels)", cfg.enable_power_control);
    cmd.AddValue("prefer_low_power_channel",
                 "set false to disable low-power preference when ETT values are close",
                 cfg.prefer_low_power_channel);
    cmd.AddValue("channel_selection_ett_tolerance",
                 "ETT tolerance multiplier for preferring lower-power channels, e.g. 1.1, 1.2, 1.5",
                 cfg.channel_selection_ett_tolerance);
    cmd.AddValue("show_log", "show log", cfg.show_log);
    cmd.Parse(argc, argv);

    ScenarioHooks hooks;
    hooks.setup_mobility = set_mobility_grid;
    hooks.select_src_dst_pairs = select_grid_pairs;

    return RunScenario(cfg, hooks);
}
