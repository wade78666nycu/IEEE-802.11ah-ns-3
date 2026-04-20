#ifndef SCENARIO_CORE_H
#define SCENARIO_CORE_H

#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/dsr-module.h"
#include "ns3/hello-beacon.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/network-module.h"
#include "ns3/yans-wifi-helper.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace ns3;

struct ScenarioConfig
{
    unsigned int num_nodes{25};
    unsigned int device_num{3};
    unsigned int send_packet_num{500};
    unsigned int gradpc_type{2};
    unsigned int rx_noise_figure{7};
    int rand_seed{2};

    double cca_threshold{-92.0};
    double default_tx_power{15.0};
    double channel_selection_ett_tolerance{1.1};
    unsigned int rreq_wait_time_ms{200};

    std::string routing_method{"aodv"};
    bool export_node_info{true};
    bool enable_hello{true};
    bool enable_power_control{true};
    bool prefer_low_power_channel{false};
    bool reduce_default_power{false};
    bool show_log{true};
    double hello_power_reduction_db{2.0}; // dBm reduction for Hello beacons vs data (0 = disabled)

    std::string scenario_name{"exp"};
};

struct ScenarioHooks
{
    std::function<void(NodeContainer&, const ScenarioConfig&)> setup_mobility;
    std::function<void(const ScenarioConfig&, std::vector<unsigned int>&, std::vector<unsigned int>&)>
        select_src_dst_pairs;
    // Optional: called after IP assignment; use to install interferer applications on nodes.
    std::function<void(NodeContainer&, const ScenarioConfig&)> setup_interferers;
    // Optional: called after channel creation; use to install jammer nodes sharing the channel.
    // Signature: void(NodeContainer& nodes, std::vector<Ptr<YansWifiChannel>>& channels, const ScenarioConfig&)
    std::function<void(NodeContainer&, std::vector<Ptr<YansWifiChannel>>&, const ScenarioConfig&)> setup_jammer;
};

inline double
calculate_refence_loss(const double freq, const double ref_distance)
{
    const double const_num{-27.55};
    return 20 * log10(ref_distance) + 20 * log10(freq) + const_num;
}

inline void
Wifiphy_Setting(YansWifiPhyHelper& wifiPhy,
                const int freq,
                const double tx_power_start,
                const double tx_power_end,
                const unsigned int tx_power_levels,
                const unsigned int rx_noise_figure,
                const double cca_threshold = -92)
{
    wifiPhy.Set("ShortGuardEnabled", BooleanValue(false));
    wifiPhy.Set("ChannelWidth", UintegerValue(1));
    wifiPhy.Set("EnergyDetectionThreshold", DoubleValue(cca_threshold));
    wifiPhy.Set("CcaMode1Threshold", DoubleValue(cca_threshold - 1));
    wifiPhy.Set("TxGain", DoubleValue(0.0));
    wifiPhy.Set("RxGain", DoubleValue(0.0));
    wifiPhy.Set("TxPowerStart", DoubleValue(tx_power_start));
    wifiPhy.Set("TxPowerEnd", DoubleValue(tx_power_end));
    wifiPhy.Set("TxPowerLevels", UintegerValue(tx_power_levels));
    wifiPhy.Set("RxNoiseFigure", DoubleValue(rx_noise_figure));
    wifiPhy.Set("LdpcEnabled", BooleanValue(false));
    wifiPhy.Set("S1g1MfieldEnabled", BooleanValue(true));
    wifiPhy.Set("Frequency", UintegerValue(freq));
}

inline void
WifiChannel_Setting(YansWifiChannelHelper& wifiChannel, const double path_loss_exponent, const double ref_loss)
{
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent",
                                   DoubleValue(path_loss_exponent),
                                   "ReferenceLoss",
                                   DoubleValue(ref_loss),
                                   "ReferenceDistance",
                                   DoubleValue(1.0));
}

inline int
RunScenario(const ScenarioConfig& cfg, const ScenarioHooks& hooks)
{
    NS_ASSERT_MSG(static_cast<bool>(hooks.setup_mobility), "setup_mobility hook is required");
    NS_ASSERT_MSG(static_cast<bool>(hooks.select_src_dst_pairs), "select_src_dst_pairs hook is required");

    SeedManager::SetSeed(cfg.rand_seed);

    LogComponentEnable("Lab", LOG_LEVEL_INFO);
    NS_LOG_INFO("Lab Start");

    NodeContainer node_container;
    node_container.Create(cfg.num_nodes);
    hooks.setup_mobility(node_container, cfg);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_PHY_STANDARD_80211ah);
    wifi.SetRemoteStationManager("ns3::MinstrelWifiManager");

    std::vector<NetDeviceContainer> device_vec(cfg.device_num);

    const double TxPowerStart{cfg.default_tx_power};
    const double TxPowerEnd = TxPowerStart;
    const int power_levels{2};
    const double freq{920.0};
    const double path_loss_exponent{2.5};
    const double ref_distance{1.0};
    const double ref_loss = calculate_refence_loss(freq, ref_distance);

    std::vector<Ptr<YansWifiChannel>> channel_vec(cfg.device_num);
    std::cout << "Reference Loss: " << ref_loss;
    for (unsigned int i = 0; i < cfg.device_num; ++i)
    {
        YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();
        
        /*if(i==2){
            Wifiphy_Setting(wifiPhy,
                        freq,
                        TxPowerStart,
                        TxPowerEnd,
                        power_levels,
                        20,
                        cfg.cca_threshold);
        }*/
        //else{
            Wifiphy_Setting(wifiPhy,
                            freq,
                            TxPowerStart,
                            TxPowerEnd,
                            power_levels,
                            cfg.rx_noise_figure,
                            cfg.cca_threshold);
        //  }
        YansWifiChannelHelper wifiChannel;
        /*if(i==0)path_loss_exponent=2.6;
        else path_loss_exponent=2.5;*/
        WifiChannel_Setting(wifiChannel, path_loss_exponent, ref_loss);
        channel_vec[i] = wifiChannel.Create();
        wifiPhy.SetChannel(channel_vec[i]);

        S1gWifiMacHelper wifiMac = S1gWifiMacHelper();
        wifiMac.SetType("ns3::AdhocWifiMac", "S1gSupported", BooleanValue(true));
        device_vec.at(i) = wifi.Install(wifiPhy, wifiMac, node_container);
    }

    if (hooks.setup_jammer)
    {
        hooks.setup_jammer(node_container, channel_vec, cfg);
    }

    std::vector<unsigned int> src_node_vec;
    std::vector<unsigned int> dst_node_vec;
    hooks.select_src_dst_pairs(cfg, src_node_vec, dst_node_vec);

    SetTraceOutputPrefix(cfg.scenario_name);
    InitializeTracing(cfg.show_log, cfg.show_log);
    if (cfg.show_log)
    {
        Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferTx",
                        MakeCallback(&TraceWifiTxRate));
        Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferTx",
                        MakeCallback(&TraceTxEnergy));
        Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/RemoteStationManager/TxRtsFailed",
                        MakeCallback(&TraceMacTxRtsFailed));
        Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/RemoteStationManager/MacTxDataFailed",
                        MakeCallback(&TraceMacTxDataFailed));
        Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/RemoteStationManager/MacTxFinalRtsFailed",
                        MakeCallback(&TraceMacTxFinalRtsFailed));
        Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/RemoteStationManager/MacTxFinalDataFailed",
                        MakeCallback(&TraceMacTxFinalDataFailed));
    }

    InternetStackHelper internet;
    if (cfg.routing_method == "dsr")
    {
        internet.Install(node_container);
        DsrMainHelper dsrMain;
        DsrHelper dsr;
        dsr.Set("MinLifeTime", TimeValue(Seconds(10.0)));
        dsrMain.Install(dsr, node_container);
    }
    else if (cfg.routing_method == "aodv")
    {
        AodvHelper aodv_helper;
        aodv_helper.Set("EnableHello", BooleanValue(!cfg.enable_hello));
        aodv_helper.Set("RreqRateLimit", UintegerValue(12));
        aodv_helper.Set("ActiveRouteTimeout", TimeValue(Seconds(10.0)));
        aodv_helper.Set("UseEttRouting", BooleanValue(cfg.enable_hello));
        aodv_helper.Set("PreferLowPowerChannel", BooleanValue(cfg.prefer_low_power_channel));
        aodv_helper.Set("ChannelSelectionEttTolerance", DoubleValue(cfg.channel_selection_ett_tolerance));
        aodv_helper.Set("RreqWaitTime", TimeValue(MilliSeconds(cfg.rreq_wait_time_ms)));
        aodv_helper.Set("EnableIntermediateRrep", BooleanValue(!cfg.enable_hello));
        if (cfg.enable_hello && cfg.enable_power_control)
        {
            aodv_helper.Set("GradPCAppIdx", IntegerValue(1));
        }
        if (cfg.enable_hello)
        {
            aodv_helper.Set("ProactiveRediscoveryInterval", TimeValue(Seconds(0.0)));
        }

        InternetStackHelper internetAodv;
        internetAodv.SetRoutingHelper(aodv_helper);
        internetAodv.Install(node_container);

        if (cfg.show_log)
        {
            LogComponentEnable("AodvRoutingProtocol", LogLevel(LOG_LEVEL_DEBUG | LOG_PREFIX_TIME | LOG_PREFIX_NODE));
        }
    }
    else
    {
        NS_ASSERT_MSG(false, "routing method not found!");
    }

    Ipv4AddressGenerator ipv4_generator;
    ipv4_generator.Init("10.1.1.0", "255.255.255.0");
    for (auto devices : device_vec)
    {
        const Ipv4Mask mask = "255.255.255.0";
        Ipv4AddressHelper ipv4;
        ipv4.SetBase(ipv4_generator.GetNetwork(mask), mask);
        ipv4.Assign(devices);
        ipv4_generator.NextNetwork(mask);
    }

    const unsigned int max_packet_num_per_round{50};
    const unsigned int packet_size{256};

    const unsigned int src_port_num{9};
    const unsigned int dst_port_num{80};
    NS_ASSERT_MSG(src_node_vec.size() == dst_node_vec.size(), "src_node_vec and dst_node_vec have unmatch elements.");
    NS_ASSERT_MSG(!src_node_vec.empty(), "src_node_vec is empty.");
    NS_ASSERT_MSG(!dst_node_vec.empty(), "dst_node_vec is empty.");
    NS_ASSERT_MSG(*max_element(src_node_vec.begin(), src_node_vec.end()) < cfg.num_nodes,
                  "invalid source node id in src_node_vec.");
    NS_ASSERT_MSG(*max_element(dst_node_vec.begin(), dst_node_vec.end()) < cfg.num_nodes,
                  "invalid destination node id in dst_node_vec.");

    Time total_simulation_time = Seconds(120);
    const DataRate data_rate("30Kbps");
    Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
    Time hello_beacon_start_time = Seconds(0);
    Time hello_beacon_stop_time = Seconds(0);
    Time grad_pc_start_time = Seconds(0);
    Time grad_pc_stop_time = Seconds(0);
    Time data_phase_start_time = Seconds(0.01);

    if (cfg.enable_hello)
    {
        if (cfg.show_log)
        {
            std::cout << "[" << cfg.scenario_name << "] " << cfg.routing_method << " hello=on power_control="
                      << (cfg.enable_power_control ? "on" : "off")
                      << " prefer_low_power=" << (cfg.prefer_low_power_channel ? "on" : "off")
                      << " ett_tol="
                      << (cfg.prefer_low_power_channel ? cfg.channel_selection_ett_tolerance : 1.0) << "\n";
        }

        hello_beacon_start_time = Seconds(1.0);
        hello_beacon_stop_time = total_simulation_time - Seconds(1.0);

        if (cfg.enable_power_control)
        {
            // Phase 1: Hello at full power (1s-9s)
            // Phase 2: GradPC adjusts power (10s-11s)
            // Phase 3: Hello at new power levels for ETT re-measurement (12s-20s)
            // Data transmission starts at 21s
            grad_pc_start_time = Seconds(10.0);
            grad_pc_stop_time = Seconds(11.0);
            data_phase_start_time = Seconds(21.0);
        }
        else
        {
            // Hello warmup (1s-9s), data starts at 10s
            data_phase_start_time = Seconds(10.0);
        }

        Time hello_interval = MilliSeconds(80.0);
        const float extra_tx_distance{80.0};

        std::vector<short> gradPC_func_vec;
        if (cfg.enable_power_control)
        {
            GradPC_App::GradPC_type func_type;
            if (cfg.gradpc_type == 1)
            {
                func_type = GradPC_App::GradPC_type::torque;
                if (cfg.show_log)
                {
                    std::cout << "[" << cfg.scenario_name << "] power_control=GradPC torque\n";
                }
            }
            else if (cfg.gradpc_type == 2)
            {
                func_type = GradPC_App::GradPC_type::proportional;
                if (cfg.show_log)
                {
                    std::cout << "[" << cfg.scenario_name << "] power_control=GradPC proportional\n";
                }
            }
            else if (cfg.gradpc_type == 3)
            {
                func_type = GradPC_App::GradPC_type::logarithmic;
                if (cfg.show_log)
                {
                    std::cout << "[" << cfg.scenario_name << "] power_control=GradPC logarithmic\n";
                }
            }
            else
            {
                NS_ASSERT_MSG(false, "Not a valid gradpc_type option");
            }

            for (unsigned int i{1}; i <= cfg.device_num; ++i)
            {
                gradPC_func_vec.emplace_back(static_cast<short>(func_type));
            }
        }

        for (unsigned int i{}; i < cfg.num_nodes; ++i)
        {
            Ptr<Node> node = node_container.Get(i);

            Ptr<Hello_beacon_App> hello_beacon_app = CreateObject<Hello_beacon_App>();
            hello_beacon_app->set_data_rate(data_rate);
            hello_beacon_app->set_hello_interval(hello_interval);
            const unsigned int max_backoff_slot = 64;
            const unsigned int min_backoff_slot = 0;
            hello_beacon_app->set_backoff_limit(max_backoff_slot, min_backoff_slot);
            hello_beacon_app->set_backoff_slot_time(Time("10ms"));
            hello_beacon_app->set_max_packet_count(12);
            hello_beacon_app->run_interval = Seconds(0);   // disable auto-cycling
            hello_beacon_app->wait_interval = Seconds(0);
            hello_beacon_app->SetStartTime(hello_beacon_start_time);  // 1s
            hello_beacon_app->SetStopTime(hello_beacon_stop_time);
            node->AddApplication(hello_beacon_app);

            // Explicit phase scheduling: stop first warmup at 9s
            Simulator::Schedule(Seconds(9.0), &Hello_beacon_App::StopApplication,
                                PeekPointer(hello_beacon_app));
            // Restart hello at 12s (after GradPC at 10s-11s) for ETT re-measurement
            if(cfg.enable_power_control){
                Simulator::Schedule(Seconds(12.0), &Hello_beacon_App::StartApplication,
                                PeekPointer(hello_beacon_app));
                Simulator::Schedule(Seconds(20.0), &Hello_beacon_App::StopApplication,
                                PeekPointer(hello_beacon_app));
            }
            
            // Data-phase hello cycles: 3s on, 7s off (20-23s, 30-33s, ...)
            /*for (double t = 30.0; t < total_simulation_time.GetSeconds() - 1.0; t += 20.0)
            {
                Simulator::Schedule(Seconds(t), &Hello_beacon_App::StartApplication,
                                    PeekPointer(hello_beacon_app));
                Simulator::Schedule(Seconds(t + 8.0), &Hello_beacon_App::StopApplication,
                                    PeekPointer(hello_beacon_app));
            }*/

            if (cfg.enable_power_control)
            {
                Ptr<GradPC_App> grad_pc_app = CreateObject<GradPC_App>();
                grad_pc_app->set_gradPC_func(gradPC_func_vec);
                grad_pc_app->set_extra_tx_distance(extra_tx_distance);
                if (cfg.reduce_default_power)
                {
                    grad_pc_app->set_reduce_default_power();
                }
                if (cfg.hello_power_reduction_db > 0.0)
                {
                    grad_pc_app->set_hello_power_reduction(cfg.hello_power_reduction_db);
                }
                grad_pc_app->SetStartTime(grad_pc_start_time);
                grad_pc_app->SetStopTime(grad_pc_stop_time);
                node->AddApplication(grad_pc_app);
            }
        }
    }
    else if (cfg.show_log)
    {
        std::cout << "[" << cfg.scenario_name << "] hello=off -> traditional AODV mode, devices="
                  << cfg.device_num << "\n";
    }

    // Interferer hook is called AFTER hello/GradPC apps so application indices stay fixed.
    if (hooks.setup_interferers)
    {
        hooks.setup_interferers(node_container, cfg);
    }

    Time recv_pkt_start_time = cfg.enable_hello ? data_phase_start_time : Seconds(1);
    Time recv_pkt_end_time = total_simulation_time - Seconds(1);
    Time send_pkt_end_time = total_simulation_time - Seconds(2);
    double min_send_pkt_start_time = recv_pkt_start_time.GetSeconds();
    double max_send_pkt_start_time = (recv_pkt_start_time + MilliSeconds(2000.0)).GetSeconds();

    for (unsigned int i{0}; i < src_node_vec.size(); ++i)
    {
        Ptr<Node> src_node = node_container.Get(src_node_vec[i]);
        Ptr<Ipv4> src_ipv4 = src_node->GetObject<Ipv4>();
        Ptr<Node> dst_node = node_container.Get(dst_node_vec[i]);
        Ptr<Ipv4> dst_ipv4 = dst_node->GetObject<Ipv4>();

        Ptr<SendPacketApp> send_pkt_app = CreateObject<SendPacketApp>();
        send_pkt_app->Setup(dst_ipv4, src_port_num, dst_port_num, packet_size, cfg.send_packet_num, data_rate);
        send_pkt_app->Set_max_packet_num_per_round(max_packet_num_per_round);
        send_pkt_app->m_multi_channel = true;

        Time send_pkt_start_time = Seconds(uv->GetValue(min_send_pkt_start_time, max_send_pkt_start_time));
        send_pkt_app->SetStartTime(send_pkt_start_time);
        send_pkt_app->SetStopTime(send_pkt_end_time);
        src_node->AddApplication(send_pkt_app);
    }

    for (uint32_t dst_node_id : dst_node_vec)
    {
        Ptr<Node> dst_node = node_container.Get(dst_node_id);
        Ptr<RecvPacketApp> recv_pkt_app = CreateObject<RecvPacketApp>(dst_port_num);
        recv_pkt_app->SetStartTime(recv_pkt_start_time);
        recv_pkt_app->SetStopTime(recv_pkt_end_time);
        dst_node->AddApplication(recv_pkt_app);
    }

    for (unsigned int i{}; i < src_node_vec.size(); ++i)
    {
        NS_LOG_INFO("source node: " << std::setw(3) << src_node_vec[i] << " ---> destination node: "
                                   << std::setw(3) << dst_node_vec[i]);
    }

    std::ofstream aodv_log_file("output_file/" + std::string(cfg.scenario_name)+"/aodv_" + cfg.scenario_name + "_path.log");
    std::streambuf* const orig_clog_buf = std::clog.rdbuf(aodv_log_file.rdbuf());

    Simulator::Stop(total_simulation_time);
    Simulator::Run();

    if (cfg.export_node_info)
    {
        ExportNodePosition(node_container);
        ExportNodePower(node_container);
        ExportEnergyConsumption(node_container);
    }
    ExportEttMatrix(node_container);

    unsigned long total_recv_packets{};
    uint64_t total_delay_ns{};
    uint64_t max_delay_ns{};
    uint64_t global_first_recv_ns = UINT64_MAX;
    uint64_t global_last_recv_ns = 0;
    for (unsigned long i{}; i < src_node_vec.size(); ++i)
    {
        unsigned int dst_node_id = dst_node_vec[i];
        Ptr<Node> dst_node = node_container.Get(dst_node_id);
        const int recv_pkt_app_id = dst_node->GetNApplications() - 1;
        auto recv_pkt_app = DynamicCast<RecvPacketApp>(dst_node->GetApplication(recv_pkt_app_id));
        total_recv_packets += recv_pkt_app->get_total_recv_packet();
        total_delay_ns += recv_pkt_app->get_total_delay_ns();
        max_delay_ns = std::max(max_delay_ns, recv_pkt_app->get_max_delay_ns());
        if (recv_pkt_app->get_total_recv_packet() > 0)
        {
            global_first_recv_ns = std::min(global_first_recv_ns, recv_pkt_app->get_first_recv_ns());
            global_last_recv_ns = std::max(global_last_recv_ns, recv_pkt_app->get_last_recv_ns());
        }
    }

    const unsigned long total_sent_packets = src_node_vec.size() * cfg.send_packet_num;
    const double pdr_percent = total_sent_packets ? (static_cast<double>(total_recv_packets) / total_sent_packets) * 100.0
                                                  : 0.0;
    /*const double data_transfer_duration_s =
        (total_recv_packets > 0 && global_last_recv_ns > global_first_recv_ns)
            ? static_cast<double>(global_last_recv_ns - global_first_recv_ns) / 1e9
            : total_simulation_time.GetSeconds();*/
    const double throughput_bps = static_cast<double>(total_recv_packets * packet_size * 8) /
                                  total_simulation_time.GetSeconds();
    const double avg_delay_ms = total_recv_packets ? (static_cast<double>(total_delay_ns) / total_recv_packets) / 1e6
                                                   : 0.0;
    const double max_delay_ms = static_cast<double>(max_delay_ns) / 1e6;

    CloseTraceFiles();
    Simulator::Destroy();
    std::clog.rdbuf(orig_clog_buf);

    ExportMacRetrySummary();

    std::ofstream run_summary_file("output_file/" + std::string(cfg.scenario_name) + "/" + cfg.scenario_name +
                                   "_run_summary.txt");
    if (run_summary_file.is_open())
    {
        run_summary_file << std::fixed << std::setprecision(6);
        run_summary_file << "scenario=" << cfg.scenario_name << "\n";
        run_summary_file << "routing_method=" << cfg.routing_method << "\n";
        run_summary_file << "enable_hello=" << (cfg.enable_hello ? "true" : "false") << "\n";
        run_summary_file << "enable_power_control=" << (cfg.enable_power_control ? "true" : "false") << "\n";
        run_summary_file << "prefer_low_power_channel=" << (cfg.prefer_low_power_channel ? "true" : "false")
                         << "\n";
        run_summary_file << "channel_selection_ett_tolerance=" << cfg.channel_selection_ett_tolerance << "\n";
        run_summary_file << "total_sent_packets=" << total_sent_packets << "\n";
        run_summary_file << "total_received_packets=" << total_recv_packets << "\n";
        run_summary_file << "pdr_percent=" << pdr_percent << "\n";
        run_summary_file << "throughput_bps=" << throughput_bps << "\n";
        run_summary_file << "avg_delay_ms=" << avg_delay_ms << "\n";
        run_summary_file << "max_delay_ms=" << max_delay_ms << "\n";
        //run_summary_file << "data_transfer_duration_s=" << data_transfer_duration_s << "\n";
        run_summary_file << "total_simulation_time_s=" << total_simulation_time.GetSeconds() << "\n";
    }

    NS_LOG_INFO("Total received packets / Total send packet : " << total_recv_packets << "/" << total_sent_packets);
    NS_LOG_INFO("PDR " << total_recv_packets << "/" << total_sent_packets << " = " << std::fixed
                       << std::setprecision(2) << pdr_percent << "%");
    NS_LOG_INFO("Throughput: " << std::fixed << std::setprecision(2)
                               << throughput_bps /1000
                               << " Kbps");
    NS_LOG_INFO("Average delay: " << std::fixed << std::setprecision(3) << avg_delay_ms << " ms");
    NS_LOG_INFO("Max delay: " << std::fixed << std::setprecision(3) << max_delay_ms << " ms");

    return 0;
}

#endif // SCENARIO_CORE_H
