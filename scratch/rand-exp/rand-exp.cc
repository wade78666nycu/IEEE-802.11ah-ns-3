#include "assign-src-dst-pair.h"
#include "../common-exp/shared-recv-packet.h"
#include "../common-exp/shared-send-packet.h"
#include "../common-exp/shared-trace.h"

#include "ns3/core-module.h"

NS_LOG_COMPONENT_DEFINE("Lab");

#include "../common-exp/scenario-core.h"

using namespace ns3;

static void
set_mobility_rectangle(NodeContainer& node_container, const ScenarioConfig&)
{
    Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
    x->SetAttribute("Min", DoubleValue(-1200.0));
    x->SetAttribute("Max", DoubleValue(1200.0));

    Ptr<UniformRandomVariable> y = CreateObject<UniformRandomVariable>();
    y->SetAttribute("Min", DoubleValue(-1200.0));
    y->SetAttribute("Max", DoubleValue(1200.0));

    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator", "X", PointerValue(x), "Y", PointerValue(y));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(node_container);
}

static void
select_rand_pairs(const ScenarioConfig& cfg, std::vector<unsigned int>& src_node_vec, std::vector<unsigned int>& dst_node_vec)
{
    assign_src_dst_pair(cfg.rand_seed, cfg.num_nodes, src_node_vec, dst_node_vec);
}

int
main(int argc, char* argv[])
{
    ScenarioConfig cfg;
    cfg.scenario_name = "rand";
    cfg.num_nodes = 50;
    cfg.rand_seed = 10;

    CommandLine cmd;
    cmd.AddValue("num_nodes", "total number of nodes", cfg.num_nodes);
    cmd.AddValue("device_num", "number of devices on each node", cfg.device_num);
    cmd.AddValue("send_packet_num", "numbers of packets to send", cfg.send_packet_num);
    cmd.AddValue("gradpc_type", "method of gradpc to use", cfg.gradpc_type);
    cmd.AddValue("seed", "random seed number", cfg.rand_seed);
    cmd.AddValue("tx_power", "tx power", cfg.default_tx_power);
    cmd.AddValue("rx_noise_figure", "background Noise Figure", cfg.rx_noise_figure);
    cmd.AddValue("routing_method", "routing method used", cfg.routing_method);
    cmd.AddValue("export_node_info", "export node's position and power", cfg.export_node_info);
    cmd.AddValue("enable_hello", "set true to enable hello beacon (ETX measurement)", cfg.enable_hello);
    cmd.AddValue("enable_power_control", "set false to disable tx power control (keep same power across channels)", cfg.enable_power_control);
    cmd.AddValue("prefer_low_power_channel",
                 "set false to disable low-power preference when ETT values are close",
                 cfg.prefer_low_power_channel);
    cmd.AddValue("channel_selection_ett_tolerance",
                 "ETT tolerance multiplier for preferring lower-power channels, e.g. 1.1, 1.2, 1.5",
                 cfg.channel_selection_ett_tolerance);
    cmd.AddValue("reduce_default", "set to true if channel 1's power can be reduced", cfg.reduce_default_power);
    cmd.AddValue("show_log", "show log", cfg.show_log);
    cmd.AddValue("hello_power_reduction",
                 "dBm reduction for Hello beacons vs data power (0 = disabled)",
                 cfg.hello_power_reduction_db);
    cmd.Parse(argc, argv);

    ScenarioHooks hooks;
    hooks.setup_mobility = set_mobility_rectangle;
    hooks.select_src_dst_pairs = select_rand_pairs;

    return RunScenario(cfg, hooks);
}
