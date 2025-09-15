#include "forward-packet.h"
#include "set-node-position.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/yans-wifi-helper.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("Lab");

static const unsigned int MAX_DEVICE_NUM{5};

void
Wifiphy_Setting(YansWifiPhyHelper& wifiPhy, const double TxPower)
{
    wifiPhy.Set("TxPowerStart", DoubleValue(TxPower));
    wifiPhy.Set("TxPowerEnd", DoubleValue(TxPower));
    wifiPhy.Set("TxPowerLevels", UintegerValue(1));
    wifiPhy.Set("S1g1MfieldEnabled", BooleanValue(false));
    wifiPhy.Set("LdpcEnabled", BooleanValue(false));
    wifiPhy.Set("EnergyDetectionThreshold", DoubleValue(-96.0));
    wifiPhy.Set("CcaMode1Threshold", DoubleValue(-99.0));
    wifiPhy.Set("RxGain", DoubleValue(1.0));
    wifiPhy.Set("TxGain", DoubleValue(1.0));
    wifiPhy.Set("RxNoiseFigure", DoubleValue(6.8));
    wifiPhy.Set("Frequency", UintegerValue(920));
    return;
}

// const_num calculation: https://en.wikipedia.org/wiki/Free-space_path_loss
double
calculate_refence_loss(const double freq, const double ref_distance)
{
    // freq is in MHz and ref_distance is in meters
    const double const_num = -27.55;
    return 20 * log10(ref_distance) + 20 * log10(freq) + const_num;
}

void
WifiChannel_Setting(YansWifiChannelHelper& wifiChannel, const double path_loss_exponent, 
        const double ref_loss, const double ref_distance)
{
    // ReferenceLoss 40 is for operating frequency 2.4GHz
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent",
                                   DoubleValue(path_loss_exponent),
                                   "ReferenceLoss",
                                   DoubleValue(ref_loss),
                                   "ReferenceDistance",
                                   DoubleValue(ref_distance));
    return;
}

void
export_node_position(const NodeContainer& node_container)
{
    std::string file_name = "node_position.txt";
    std::ofstream export_file(file_name);
    if (export_file.is_open())
    {
        const unsigned int container_size = node_container.GetN();
        export_file << "total nodes: " << container_size << "\n";
        for (unsigned int i = 0; i < container_size; ++i)
        {
            Ptr<Node> node = node_container.Get(i);
            Vector position = node->GetObject<MobilityModel>()->GetPosition();
            export_file << "node " << std::setw(2) << std::to_string(i) << ": " << std::setw(4)
                        << position.x << "  " << std::setw(4) << position.y << "\n";
        }
        export_file.close();
    }
    return;
}

double
DbmToW(double dBm)
{
    return std::pow(10.0, 0.1 * (dBm - 30.0));
}

void
export_node_power(const NodeContainer& node_container, std::string power_unit)
{
    std::string file_name = "node_power.txt";
    std::ofstream export_file(file_name);
    if (export_file.is_open())
    {
        auto get_tx_power = [power_unit](Ptr<YansWifiPhy> wifi_phy) {
            double tx_power = round(wifi_phy->GetTxPowerEnd() * 100) / 100;
            if (power_unit == "milliWatts")
            {
                return round(DbmToW(tx_power) * 1000);
            }
            return tx_power;
        };

        const unsigned int container_size = node_container.GetN();
        export_file << "total nodes: " << container_size << "\n";
        for (unsigned int i = 0; i < container_size; ++i)
        {
            Ptr<Node> node = node_container.Get(i);
            const unsigned int total_devices = node->GetNDevices() - 1;
            export_file << "node " << std::setw(2) << std::to_string(i) << ": ";

            for (unsigned int device_idx = 0; device_idx < total_devices; ++device_idx)
            {
                Ptr<WifiNetDevice> wifi_device =
                    DynamicCast<WifiNetDevice>(node->GetDevice(device_idx));
                Ptr<YansWifiPhy> wifi_phy = DynamicCast<YansWifiPhy>(wifi_device->GetPhy());
                export_file << std::setw(5) << get_tx_power(wifi_phy) << "  ";
            }
            export_file << "\n";
        }
        export_file << "output power unit: [" << power_unit << "]\n";
        export_file.close();
    }
    return;
}

void
set_next_hop_map(std::unordered_map<next_hop_key, std::string>& hop_map,
                 std::unordered_set<unsigned int>& node_set,
                 std::vector<next_hop_key>& src_key)
{
    unsigned int dst_node_id;

    // 49 -> 45 -> 29 -> 32 -> 35
    std::vector<unsigned int> node_vec = {49, 45, 29, 32, 35};
    node_set.insert(node_vec.begin(), node_vec.end());
    node_vec.clear();
    dst_node_id = 35;
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.1.50"), "10.1.1.46");
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.1.46"), "10.1.2.30");
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.2.30"), "10.1.3.33");
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.3.33"), "10.1.1.36");
    src_key.emplace_back(next_hop_key(dst_node_id, "10.1.1.50"));

    // 30 -> 31 -> 27
    node_vec = {30, 31, 27};
    node_set.insert(node_vec.begin(), node_vec.end());
    node_vec.clear();
    dst_node_id = 27;
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.2.31"), "10.1.2.32");
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.2.32"), "10.1.3.28");
    src_key.emplace_back(next_hop_key(dst_node_id, "10.1.2.31"));

    // 40 -> 41 -> 37
    node_vec = {40, 41, 37};
    node_set.insert(node_vec.begin(), node_vec.end());
    node_vec.clear();
    dst_node_id = 37;
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.3.41"), "10.1.3.42");
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.3.42"), "10.1.1.38");
    src_key.emplace_back(next_hop_key(dst_node_id, "10.1.3.41"));

    // 1 -> 3 -> 5 -> 8
    node_vec = {1, 3, 5, 8};
    node_set.insert(node_vec.begin(), node_vec.end());
    node_vec.clear();
    dst_node_id = 8;
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.2.2"), "10.1.2.4");
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.2.4"), "10.1.3.6");
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.3.6"), "10.1.1.9");
    src_key.emplace_back(next_hop_key(dst_node_id, "10.1.2.2"));

    // 16 -> 17 -> 13
    node_vec = {16, 17, 13};
    node_set.insert(node_vec.begin(), node_vec.end());
    node_vec.clear();
    dst_node_id = 13;
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.3.17"), "10.1.3.18");
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.3.18"), "10.1.1.14");
    src_key.emplace_back(next_hop_key(dst_node_id, "10.1.3.17"));

    // 12 -> 15
    node_vec = {12, 15};
    node_set.insert(node_vec.begin(), node_vec.end());
    node_vec.clear();
    dst_node_id = 15;
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.1.13"), "10.1.1.16");
    src_key.emplace_back(next_hop_key(dst_node_id, "10.1.1.13"));

    // 23 -> 22
    node_vec = {23, 22};
    node_set.insert(node_vec.begin(), node_vec.end());
    node_vec.clear();
    dst_node_id = 22;
    hop_map.emplace(next_hop_key(dst_node_id, "10.1.1.24"), "10.1.1.23");
    src_key.emplace_back(next_hop_key(dst_node_id, "10.1.1.24"));
}

int
main(int argc, char* argv[])
{
    //std::string phy_mode("OfdmRate3MbpsBW5MHz");
    std::string phy_mode("OfdmRate300KbpsBW1MHz");
    unsigned int num_nodes{50};
    unsigned int device_num{3};
    unsigned int send_packet_num = 400;
    bool use_power_ctl = false;
    // IEEE 802.11ah operates in unlicensed sub-GHz frequency bands 
    // (863–868 MHz in Europe, 755–787 MHz in China and 902–928 MHz in North-America)
    double freq = 920; // [Mhz] operating frequency
    // double freq = 59.91; // [Mhz] operating frequency

    SeedManager::SetSeed(10); // random seed manager

    // enable logging
    LogComponentEnable("ForwardPktApp", LOG_LEVEL_INFO);
    // LogComponentEnable("ForwardPktApp", LOG_LEVEL_DEBUG);
    LogComponentEnable("Lab", LOG_LEVEL_INFO);

    CommandLine cmd;
    cmd.AddValue("phy_mode", "Wifi Phy mode", phy_mode);
    cmd.AddValue("num_nodes", "total number of nodes", num_nodes);
    cmd.AddValue("device_num", "number of devices on each node", device_num);
    cmd.AddValue("send_packet_num",
                 "number of packets to send for each source node",
                 send_packet_num);
    cmd.AddValue("power_ctl", "use power control", use_power_ctl);
    cmd.Parse(argc, argv);
    // TODO: check if there are invalid argc inputs
    if (device_num > MAX_DEVICE_NUM)
        device_num = MAX_DEVICE_NUM;

    // no matter turning on/off RTS/CTS, dsr with multi-channel yields the better result under
    // single-hop topology turning off RTS/CTS, difference between dsr with multi-channel and single
    // channel is larger const std::string rts_limit{"9999"};
    // const std::string rts_limit{"500"};
    // Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue(rts_limit));

    NodeContainer node_container;
    node_container.Create(num_nodes);

    //* paper topology
    set_mobility_file(node_container);
    //*/

    /* grid topology
    const unsigned int grid_width = sqrt(num_nodes);
    set_mobility_grid(node_container, node_distance, grid_width);
    //*/

    WifiHelper wifi;
    wifi.SetStandard(WIFI_PHY_STANDARD_80211ah);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue(phy_mode),
                                 "ControlMode",
                                 StringValue(phy_mode));
    // device_vec stores devices that uses different radios(channels)
    std::vector<NetDeviceContainer> device_vec(device_num);
    // default tx_power 20.0dBm = 100mW
    std::vector<double> TxPower = {18.0, 18.0, 18.0};
    if (use_power_ctl)
        TxPower = {15.0, 12.0, 9.18};
    const double ref_distance = 1.0; // meters
    const double ref_loss = calculate_refence_loss(freq, ref_distance); // dB
    const double path_loss_exponent = 2.7;
    NS_LOG_DEBUG("ref_loss: " << ref_loss);
    for (unsigned int i = 0; i < device_num; ++i)
    {
        //Wifiphy_Setting(wifiPhy, TxPower);
        YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();
        Wifiphy_Setting(wifiPhy, TxPower[i]);

        YansWifiChannelHelper wifiChannel; 
        WifiChannel_Setting(wifiChannel, path_loss_exponent, ref_loss, ref_distance);
        wifiPhy.SetChannel(wifiChannel.Create());

        // Add an upper mac and set it to adhoc mode
        //WifiMacHelper wifiMac;
        //wifiMac.SetType("ns3::AdhocWifiMac");
        S1gWifiMacHelper wifiMac = S1gWifiMacHelper();
        wifiMac.SetType("ns3::AdhocWifiMac", "S1gSupported", BooleanValue(true), "HtSupported", BooleanValue(false));
        device_vec.at(i) = wifi.Install(wifiPhy, wifiMac, node_container);

        // if (i == 0)
        // wifiPhy.EnablePcap("lab", device_vec.at(i));
    }

    // need to install the InternetStackHelper before installing dsr
    InternetStackHelper internet;
    internet.Install(node_container);

    // generate ipv4 network of 10.1.1.0, 10.1.2.0, 10.1.3.0, ...
    // and assign these ipv4 address to devices in device_vec
    Ipv4AddressGenerator ipv4_generator;
    ipv4_generator.Init("10.1.1.0", "255.255.255.0");
    std::vector<Ipv4InterfaceContainer> interface_vec;
    for (auto devices : device_vec)
    {
        const Ipv4Mask mask = "255.255.255.0";
        Ipv4AddressHelper ipv4;
        ipv4.SetBase(ipv4_generator.GetNetwork(mask), mask);
        interface_vec.emplace_back(ipv4.Assign(devices));
        ipv4_generator.NextNetwork(mask);
    }

    // Config::Set("/NodeList/*/$ns3::Ipv4L3Protocol/InterfaceList/*/ArpCache/PendingQueueSize",
    // UintegerValue(8));
    // Config::Set("/NodeList/*/$ns3::Ipv4L3Protocol/InterfaceList/*/ArpCache/WaitReplyTimeout",
    // TimeValue(Seconds(3.0)));
    // Config::Set("/NodeList/*/$ns3::Ipv4L3Protocol/InterfaceList/*/ArpCache/MaxRetries",
    // UintegerValue(8));

    std::unordered_map<next_hop_key, std::string> hop_map;
    hop_map.reserve(32);
    std::unordered_set<unsigned int> node_set;
    node_set.reserve(32);
    std::vector<next_hop_key> src_key;
    src_key.reserve(8);
    set_next_hop_map(hop_map, node_set, src_key);

    // Simulation settings
    Time total_simulation_time = Seconds(1000.0); // [second]
    //const DataRate data_rate("1Mbps");          // bps: bits per second
    const DataRate data_rate("300Kbps");
    Time recv_pkt_start_time = Seconds(1);
    Time recv_pkt_end_time = total_simulation_time - Seconds(5);
    Time forward_pkt_end_time = total_simulation_time - Seconds(20);
    double min_forward_pkt_start_time = (recv_pkt_start_time).GetSeconds() + 5;
    double max_forward_pkt_start_time = (recv_pkt_start_time + MicroSeconds(1)).GetSeconds();
    // double max_forward_pkt_start_time = (forward_pkt_end_time - Seconds(10)).GetSeconds();
    //  FIXME: If forward_packet_num is too large (over 300), the program breaks
    const unsigned int max_packet_num_per_round = 100;
    const unsigned int packet_size = 1024; // bytes
    std::vector<unsigned int> src_node_vec{49, 30, 40, 1, 16, 12, 23};
    std::vector<unsigned int> dst_node_vec{35, 27, 37, 8, 13, 15, 22};
    //std::vector<unsigned int> src_node_vec{49};
    //std::vector<unsigned int> dst_node_vec{35};
    const unsigned int src_port_num = 9;
    const unsigned int dst_port_num = 80;
    // some checking before simulation
    NS_ASSERT_MSG(src_node_vec.size() == dst_node_vec.size(),
                  "src_node_vec and dst_node_vec have unmatch elements.");
    NS_ASSERT_MSG(forward_pkt_end_time < total_simulation_time, "forward_pkt_end_time is invalid");
    NS_ASSERT_MSG(recv_pkt_end_time < total_simulation_time, "recv_pkt_end_time is invalid");
    Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
    for (unsigned int node_id : node_set)
    {
        Ptr<ForwardPktApp> forward_pkt_app = CreateObject<ForwardPktApp>();
        forward_pkt_app->Setup(hop_map, src_port_num, dst_port_num);
        forward_pkt_app->Set_max_packet_num_per_round(max_packet_num_per_round);
        forward_pkt_app->SetStartTime(recv_pkt_start_time);
        if (std::any_of(src_node_vec.begin(), src_node_vec.end(), [node_id](unsigned int id) {
                return node_id == id;
            }))
        {
            forward_pkt_app->SetStopTime(recv_pkt_end_time);
            forward_pkt_app->Set_send(packet_size, send_packet_num, data_rate);
            Time start_time =
                Seconds(uv->GetValue(min_forward_pkt_start_time, max_forward_pkt_start_time));
            NS_LOG_INFO("[node " << node_id
                                 << "] forward_pkt_start_time: " << start_time.As(Time::MS));
            forward_pkt_app->is_source = true;
            forward_pkt_app->start_send_pkt_time = start_time;
        }

        if (std::any_of(dst_node_vec.begin(), dst_node_vec.end(), [node_id](unsigned int id) {
                return node_id == id;
            })) {
            forward_pkt_app->is_destination = true;
            forward_pkt_app->SetStopTime(recv_pkt_end_time + Seconds(1.0));
        }

        //if (node_id == 45)
            //forward_pkt_app->print_data = true;
        node_container.Get(node_id)->AddApplication(forward_pkt_app);
    }

    const unsigned int forward_pkt_app_idx = 0;
    NS_ASSERT_MSG(src_key.size() >= src_node_vec.size(), "not enough element in src_key");
    for (unsigned int i = 0; i < src_node_vec.size(); ++i)
    {
        NS_ASSERT_MSG(src_key[i].dst == dst_node_vec[i], "wrong elements in src_key");
        Ptr<Node> src_node = node_container.Get(src_node_vec[i]);
        DynamicCast<ForwardPktApp>(src_node->GetApplication(forward_pkt_app_idx))->start_key =
            src_key[i];
    }

    // print source / destination node pair
    for (unsigned int i = 0; i < src_node_vec.size(); ++i)
    {
        NS_LOG_INFO("source node: " << src_node_vec[i]
                                    << " ---> destination node: " << dst_node_vec[i]);
    }
    //*/

    // Start simulation
    Simulator::Stop(total_simulation_time);
    Simulator::Run();

    // export node_position and node_power to plot lab result
    // export_node_position(node_container);
    // export_node_power(node_container, "milliWatts");

    unsigned int total_pkt_sent = 0;
    unsigned int total_pkt_recv = 0;
    for (unsigned int i = 0; i < dst_node_vec.size(); ++i) {
        Ptr<Node> src_node = node_container.Get(src_node_vec[i]);
        total_pkt_sent += DynamicCast<ForwardPktApp>(src_node->GetApplication(forward_pkt_app_idx))->get_pkt_sent();

        Ptr<Node> dst_node = node_container.Get(dst_node_vec[i]);
        total_pkt_recv += DynamicCast<ForwardPktApp>(dst_node->GetApplication(forward_pkt_app_idx))->get_pkt_recv();
    }

    NS_LOG_INFO("Total packets sent: " << total_pkt_sent);
    NS_LOG_INFO("Total packets received: " << total_pkt_recv);

    Simulator::Destroy();
    return 0;
}
