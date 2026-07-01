
#include "../common-exp/shared-recv-packet.h"
#include "../common-exp/shared-send-packet.h"
#include "../common-exp/shared-trace.h"

#include "ns3/core-module.h"

NS_LOG_COMPONENT_DEFINE("Lab");

#include "../common-exp/scenario-core.h"
#include "../common-exp/shared-interferer.h"

#include <cmath>

using namespace ns3;

// ─────────────────────────────────────────────────────────────────────────────
// "Parallel short links" spatial-reuse benchmark.
//
// Layout: pairs_per_row x pairs_per_row independent src->dst pairs arranged on a
// coarse grid. Within each pair the dst sits `link_distance` away from the src
// (short -> always single hop at any power). Pairs are separated by
// `pair_spacing` on the coarse grid.
//
// The crossover between full-power and GradPC is controlled purely by geometry:
//   - full power (15 dBm)  -> carrier-sense range ~1000 m  -> if pair_spacing
//     < ~1000 m, ALL pairs share one contention domain and CSMA serializes them.
//   - GradPC (reduced)     -> carrier-sense range ~200 m   -> if pair_spacing
//     > ~200 m, pairs decouple and transmit concurrently (spatial reuse).
//
// So choose pair_spacing inside the window (~200 m, ~1000 m), e.g. 400 m, and
// raise data_rate until airtime is the bottleneck: GradPC should then overtake
// full power on aggregate throughput. Sweep pair_spacing / data_rate to map the
// crossover.
//
// Node indexing: node 2k = src of pair k, node 2k+1 = dst of pair k.
// ─────────────────────────────────────────────────────────────────────────────

int
main(int argc, char* argv[])
{
    ScenarioConfig cfg;
    cfg.scenario_name = "grid";
    cfg.rand_seed = 6;

    unsigned int pairs_per_row = 3;   // total pairs = pairs_per_row^2
    double pair_spacing = 100.0;      // distance between adjacent pairs (m)
    double link_distance = 60.0;      // src->dst distance within a pair (m)

    // Saturation defaults: continuous CBR (no inter-round pause) and enough
    // packets to keep every flow backlogged for the whole ~58 s data window.
    cfg.data_rate_str = "500Kbps";
    cfg.send_packet_num = 5000;
    cfg.max_packet_num_per_round = 1000000;

    CommandLine cmd;
    cmd.AddValue("scenario_name", "output directory / file prefix", cfg.scenario_name);
    cmd.AddValue("pairs_per_row", "pairs per row; total pairs = pairs_per_row^2", pairs_per_row);
    cmd.AddValue("pair_spacing", "distance between adjacent pairs (m)", pair_spacing);
    cmd.AddValue("link_distance", "src->dst distance within a pair (m)", link_distance);
    cmd.AddValue("device_num", "number of devices on each node", cfg.device_num);
    cmd.AddValue("send_packet_num", "numbers of packets to send", cfg.send_packet_num);
    cmd.AddValue("max_packet_per_round",
                 "packets sent back-to-back before a pause; large = continuous CBR (saturation)",
                 cfg.max_packet_num_per_round);
    cmd.AddValue("gradpc_type", "method of gradpc to use", cfg.gradpc_type);
    cmd.AddValue("seed", "random seed number", cfg.rand_seed);
    cmd.AddValue("export_node_info", "export node's position and power", cfg.export_node_info);
    cmd.AddValue("tx_power", "tx power", cfg.default_tx_power);
    cmd.AddValue("reduce_default", "set true so GradPC can also lower channel 0 (default power)", cfg.reduce_default_power);
    cmd.AddValue("extra_tx_distance", "extra reach (m) GradPC adds on top of neighbor distance; smaller = more aggressive", cfg.extra_tx_distance);
    cmd.AddValue("rx_noise_figure", "background Noise Figure", cfg.rx_noise_figure);
    cmd.AddValue("routing_method", "routing method used", cfg.routing_method);
    cmd.AddValue("enable_hello", "set true to enable hello beacon (ETX measurement)", cfg.enable_hello);
    cmd.AddValue("enable_power_control", "set false to disable tx power control (keep same power across channels)", cfg.enable_power_control);
    cmd.AddValue("prefer_low_power_channel",
                 "set false to disable low-power preference when ETT values are close",
                 cfg.prefer_low_power_channel);
    cmd.AddValue("data_rate", "data rate per flow, e.g. 200Kbps or 1Mbps", cfg.data_rate_str);
    cmd.AddValue("show_log", "show log", cfg.show_log);
    cmd.Parse(argc, argv);

    NS_ASSERT_MSG(pairs_per_row > 0, "pairs_per_row must be > 0");
    const unsigned int num_pairs = pairs_per_row * pairs_per_row;
    cfg.num_nodes = 2 * num_pairs;

    ScenarioHooks hooks;

    // Place node 2k (src) and node 2k+1 (dst) for each pair k on the coarse grid.
    hooks.setup_mobility = [=](NodeContainer& nodes, const ScenarioConfig&) {
        Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
        for (unsigned int k = 0; k < num_pairs; ++k)
        {
            const unsigned int r = k / pairs_per_row;
            const unsigned int c = k % pairs_per_row;
            const double sx = c * pair_spacing;
            const double sy = r * pair_spacing;
            pos->Add(Vector(sx, sy, 0.0));                  // src  (node 2k)
            pos->Add(Vector(sx + link_distance, sy, 0.0));  // dst  (node 2k+1)
        }
        MobilityHelper mobility;
        mobility.SetPositionAllocator(pos);
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(nodes);
    };

    hooks.select_src_dst_pairs = [=](const ScenarioConfig&,
                                     std::vector<unsigned int>& src_node_vec,
                                     std::vector<unsigned int>& dst_node_vec) {
        src_node_vec.clear();
        dst_node_vec.clear();
        for (unsigned int k = 0; k < num_pairs; ++k)
        {
            src_node_vec.push_back(2 * k);
            dst_node_vec.push_back(2 * k + 1);
        }
    };

    return RunScenario(cfg, hooks);
}
