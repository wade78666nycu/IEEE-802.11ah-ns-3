#include "assign-src-dst-pair.h"
#include "recv-packet.h"
#include "send-packet.h"

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

#include <fstream>
#include <iomanip>
#include <iostream>
#include <cmath>
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Lab");

void
export_node_position(const NodeContainer& node_container)
{
	std::string file_name{"rand_node_position.txt"};
	std::ofstream export_file(file_name);
	if (export_file.is_open())
	{
		const unsigned int container_size = node_container.GetN();
		export_file << "total nodes: " << container_size << "\n";
		for (unsigned int i = 0; i < container_size; ++i)
		{
			Ptr<Node> node = node_container.Get(i);
			Vector position = node->GetObject<MobilityModel>()->GetPosition();
			export_file << "node " << std::setw(2) << std::to_string(i) << ": " << std::setw(4) << position.x << "  "
						<< std::setw(4) << position.y << "\n";
		}
		export_file.close();
	}
	return;
}

void
export_node_power(const NodeContainer& node_container)
{
	auto write_node_power = [&](const std::string& file_name) {
		std::ofstream export_file(file_name);
		if (!export_file.is_open())
		{
			return;
		}

		auto get_tx_power = [](Ptr<YansWifiPhy> wifi_phy) {
			double tx_power = round(wifi_phy->GetTxPowerStart() * 100) / 100;
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
				Ptr<WifiNetDevice> wifi_device = DynamicCast<WifiNetDevice>(node->GetDevice(device_idx));
				Ptr<YansWifiPhy> wifi_phy = DynamicCast<YansWifiPhy>(wifi_device->GetPhy());
				export_file << std::setw(5) << get_tx_power(wifi_phy) << "  ";
			}
			export_file << "\n";
		}
		export_file << "output power unit: dBm\n";
		export_file.close();
	};

	// Prefer explicit rand-exp output name, but keep legacy filename for existing scripts.
	write_node_power("rand_node_power.txt");
	return;
}

void
export_etx_matrix(const NodeContainer& node_container)
{
	const unsigned int num_nodes = node_container.GetN();
	if (num_nodes == 0)
	{
		return;
	}

	Ptr<Ipv4> ipv4 = node_container.Get(0)->GetObject<Ipv4>();
	const uint32_t channel_count = ipv4 ? (ipv4->GetNInterfaces() > 0 ? ipv4->GetNInterfaces() - 1 : 0) : 0;
	if (channel_count == 0)
	{
		return;
	}

	auto write_matrix = [&](const std::string& file_name, const uint32_t ifIndex) {
		std::ofstream export_file(file_name);
		if (!export_file.is_open())
		{
			return;
		}

		// Write header row (receiver node IDs)
		export_file << "Sender/Receiver";
		for (unsigned int j = 0; j < num_nodes; ++j)
		{
			export_file << "," << j;
		}
		export_file << "\n";

		// Write ETX matrix (rows = senders, columns = receivers)
		for (unsigned int i = 0; i < num_nodes; ++i)
		{
			export_file << i;  // sender node ID
			Ptr<Node> sender_node = node_container.Get(i);

			// Find Hello_beacon_App in this node
			Ptr<Hello_beacon_App> hello_app = nullptr;
			for (unsigned int app_idx = 0; app_idx < sender_node->GetNApplications(); ++app_idx)
			{
				hello_app = DynamicCast<Hello_beacon_App>(sender_node->GetApplication(app_idx));
				if (hello_app != nullptr)
				{
					break;
				}
			}

			// For each receiver node
			for (unsigned int j = 0; j < num_nodes; ++j)
			{
				export_file << ",";
				if (hello_app != nullptr && i != j)
				{
					double etx = hello_app->get_etx(j, ifIndex);
					if (std::isinf(etx))
					{
						export_file << "INF";
					}
					else
					{
						export_file << std::fixed << std::setprecision(2) << etx;
					}
				}
				// else: empty cell if sender == receiver or app not found
			}
			export_file << "\n";
		}
		export_file.close();
	};

	for (uint32_t ifIndex = 1; ifIndex <= channel_count; ++ifIndex)
	{
		write_matrix("rand_etx_ch" + std::to_string(ifIndex) + ".csv", ifIndex);
	}
	return;
}

// const_num calculation: https://en.wikipedia.org/wiki/Free-space_path_loss
double
calculate_refence_loss(const double freq, const double ref_distance)
{
	// freq is in MHz and ref_distance is in meters
	const double const_num{-27.55};
	return 20 * log10(ref_distance) + 20 * log10(freq) + const_num;
}

void
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
	// NOTE!! EnergyDetectionThreshold is the cca threshold!!
	// wifiPhy.Set("EnergyDetectionThreshold", DoubleValue());
	// wifiPhy.Set("CcaMode1Threshold", DoubleValue(-90.0));
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
	return;
}

void
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
	return;
}

void
biconn_func(NodeContainer& node_container,
			const float max_power,
			const float ref_loss,
			const float path_loss_exponent,
			const double cca_sensitivity)
{
	// NS_LOG_UNCOND("Remember to check if the physical parameters are set correctly!");
	struct edge
	{
		std::pair<unsigned int, unsigned int> node_pair;
		float node_distance;
		float least_power;
	};

	auto get_max_distance = [ref_loss, cca_sensitivity, path_loss_exponent](const float tx_power) -> float {
		const float factor = (tx_power - cca_sensitivity - ref_loss) / (10.0 * path_loss_exponent);
		return pow(10, factor);
	};

	auto get_least_power = [ref_loss, cca_sensitivity, path_loss_exponent](const float node_distance) -> float {
		// const float min_tx_power_dBm = 2.0;
		// plus 0.25 to cancel rounding error when exporting node power
		const float least_power = cca_sensitivity + ref_loss + 10 * path_loss_exponent * log10(node_distance) + 0.25;
		// return least_power > min_tx_power_dBm ? least_power : min_tx_power_dBm;
		return least_power;
	};

	// get edges (node pairs)
	const float max_distance = get_max_distance(max_power);
	const unsigned int total_node = node_container.GetN();
	std::vector<edge> edge_vec;
	edge_vec.reserve(total_node * (total_node - 1) / 2);

	for (unsigned int i = 0; i < total_node; ++i)
	{
		Ptr<MobilityModel> position1 = node_container.Get(i)->GetObject<MobilityModel>();
		for (unsigned int j = i + 1; j < total_node; ++j)
		{
			Ptr<MobilityModel> position2 = node_container.Get(j)->GetObject<MobilityModel>();
			const float node_distance = position1->GetDistanceFrom(position2);
			NS_ASSERT_MSG(node_distance != 0.0, "node_distance cannot be zero");
			if (node_distance <= max_distance)
			{
				const float least_power = get_least_power(node_distance);
				edge e{std::pair<unsigned int, unsigned int>(i, j), node_distance, least_power};
				edge_vec.emplace_back(e);
			}
		}
	}

	std::vector<unsigned int> cluster(total_node);
	for (unsigned int i = 0; i < total_node; ++i)
	{
		cluster[i] = i;
	}

	// sort edge in non-decreasing order
	std::sort(edge_vec.begin(), edge_vec.end(), [](edge e1, edge e2) { return e1.node_distance < e2.node_distance; });

	// caculate tx_power [dBm] (connect algorithm)
	std::vector<float> tx_power_vec(total_node, 0.0); // dBm
	std::unordered_set<unsigned int> cluster_set;
	for (edge e : edge_vec)
	{
		unsigned int n1 = e.node_pair.first;
		unsigned int n2 = e.node_pair.second;
		if (cluster[n1] != cluster[n2])
		{
			cluster_set.emplace(n1);
			cluster_set.emplace(n2);
			unsigned int modify_cluster_num = cluster[n2];
			unsigned int cluster_num = cluster[n1];
			for (unsigned int i = 0; i < total_node; ++i)
			{
				if (cluster[i] == modify_cluster_num)
				{
					// cluster[i] = cluster[n1];
					cluster[i] = cluster_num;
				}
			}
			/*
			std::cout << "n1-n2: " << std::setw(2) << n1 << "-" << std::setw(2) << n2
					  << " least_power: " << e.least_power << " cluster: " << cluster[n1] << " "
					  << cluster[n2] << "\n";
			*/
			tx_power_vec[n1] = std::max(tx_power_vec[n1], e.least_power);
			tx_power_vec[n2] = std::max(tx_power_vec[n2], e.least_power);
			if (cluster_set.size() == total_node)
			{
				break;
			}
		}
	}
	edge_vec.clear();
	cluster.clear();
	cluster_set.clear();

	// assign tx_power to each nodes
	const unsigned int net_card_num = node_container.Get(0)->GetNDevices() - 1;
	for (unsigned int i = 0; i < total_node; ++i)
	{
		Ptr<Node> node = node_container.Get(i);
		for (unsigned int j = 0; j < net_card_num; ++j)
		{
			Ptr<WifiNetDevice> wifi_device = DynamicCast<WifiNetDevice>(node->GetDevice(j));
			Ptr<YansWifiPhy> wifi_phy = DynamicCast<YansWifiPhy>(wifi_device->GetPhy());
			wifi_phy->SetTxPowerStart(tx_power_vec[i]);
			wifi_phy->SetTxPowerEnd(tx_power_vec[i]);
		}
	}
	tx_power_vec.clear();
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
	mobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator", "X", PointerValue(x), "Y", PointerValue(y));
	mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
	mobility.Install(node_container);
}

void
print_vec(const std::vector<unsigned int>& vec)
{
	for (int i : vec)
	{
		std::cout << std::setw(2) << i << ' ';
	}
	std::cout << '\n';
}

void
rand_src_dst_pairs(std::vector<unsigned int>& src_node_vec,
				   std::vector<unsigned int>& dst_node_vec,
				   const NodeContainer& node_container,
				   const int rand_seed)
{
	SeedManager::SetSeed(rand_seed);
	long unsigned int total_flows{8};
	const int n = node_container.GetN();
	double src_range_threshold{250.0};

	double dst_range_min{200.0};
	double dst_range_max{700.0};
	Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
	// uv->GetValue(min, max): min is included, max is excluded

	std::vector<std::vector<unsigned int>> valid_src_node(n);
	std::vector<std::vector<unsigned int>> valid_dst_node(n);
	for (int i{}; i < n; ++i)
	{
		Ptr<MobilityModel> position1 = node_container.Get(i)->GetObject<MobilityModel>();
		for (int j{}; j < n; ++j)
		{
			if (i == j)
				continue;

			Ptr<MobilityModel> position2 = node_container.Get(j)->GetObject<MobilityModel>();
			const float node_distance = position1->GetDistanceFrom(position2);
			if (node_distance <= src_range_threshold)
			{
				valid_src_node[i].emplace_back(j);
			}

			if (node_distance >= dst_range_min && node_distance <= dst_range_max)
			{
				valid_dst_node[i].emplace_back(j);
			}
		}
	}

	int try_counter{};
	std::unordered_set<unsigned int> node_set;
	while (try_counter < n && src_node_vec.size() < total_flows)
	{
		unsigned int node_id = uv->GetValue(0, n);
		if (node_set.count(node_id))
		{
			++try_counter;
			continue;
		}

		bool find_near_src{false};
		for (unsigned int id : valid_src_node[node_id])
		{
			if (node_set.count(id) == 0)
			{
				src_node_vec.emplace_back(id);
				node_set.emplace(id);
				find_near_src = true;
				break;
			}
		}
		if (find_near_src == false)
			continue;
		src_node_vec.emplace_back(node_id);
		node_set.emplace(node_id);
		++try_counter;
	}

	for (unsigned int src_id : src_node_vec)
	{
		int valid_size = valid_dst_node[src_id].size();
		for (int i{}; i < valid_size; ++i)
		{
			int rand_idx = uv->GetValue(0, valid_dst_node[src_id].size());
			int dst_node = valid_dst_node[src_id][rand_idx];
			if (node_set.count(dst_node) == 0)
			{
				node_set.emplace(dst_node);
				dst_node_vec.emplace_back(dst_node);
				break;
			}
			std::swap(valid_dst_node[src_id].back(), valid_dst_node[src_id][rand_idx]);
			valid_dst_node[src_id].pop_back();
		}
	}
	NS_ASSERT_MSG(src_node_vec.size() == dst_node_vec.size(), "src_node_vec and dst_node_vec have unmatch elements.");
}

int
main(int argc, char* argv[])
{
	std::string phy_mode("OfdmRate300KbpsBW1MHz");
	// std::string phy_mode("OfdmRate650KbpsBW2MHz");
	LogComponentEnable("Lab", LOG_LEVEL_INFO);
	// LogComponentEnable("Lab", LOG_LEVEL_DEBUG);
	// LogComponentEnable("SendPacketApp", LOG_LEVEL_INFO);
	LogComponentEnable("RecvPacketApp", LOG_LEVEL_INFO);

	unsigned int num_nodes{30};
	unsigned int device_num{3};
	unsigned int send_packet_num{1};
	unsigned int gradpc_type{2};
	unsigned int rx_noise_figure{7};
	int rand_seed{4};
	double cca_threshold{-92.};
	double default_tx_power{20.0};
	float gradpc_log_k{5.0};
	float gradpc_delta{0.35};
	std::string routing_method{"aodv"};
	bool export_node_info{false};
	bool enable_hello{true};
	bool enable_power_control{false};
	bool use_biconn{false};
	bool reduce_default_power{true};
	bool show_log{true};

	CommandLine cmd;
	cmd.AddValue("num_nodes", "total number of nodes", num_nodes);
	cmd.AddValue("device_num", "total device numbers on each node", device_num);
	cmd.AddValue("send_packet_num", "numbers of packets to send", send_packet_num);
	cmd.AddValue("gradpc_type", "method of gradpc to use", gradpc_type);
	cmd.AddValue("seed", "random seed number", rand_seed);
	cmd.AddValue("tx_power", "tx power", default_tx_power);
	cmd.AddValue("gradpc_log_k", "value of gradpc's log k, suggest value is between 0.75 ~ 5.5", gradpc_log_k);
	cmd.AddValue("gradpc_delta", "value of gardpc's delta, value must be between 0 ~ 1 (exclusive)", gradpc_delta);
	cmd.AddValue("rx_noise_figure", "background Noise Figure", rx_noise_figure);
	cmd.AddValue("routing_method", "routing method used", routing_method);
	cmd.AddValue("export_node_info", "export node's position and power", export_node_info);
	cmd.AddValue("enable_hello", "set true to enable hello beacon (ETX measurement)", enable_hello);
	cmd.AddValue("enable_power_control", "set false to disable tx power control (keep same power across channels)", enable_power_control);

	cmd.AddValue("use_biconn", "set true to enable biconn power control", use_biconn);
	cmd.AddValue("reduce_default", "set to true if channel 1's power can be reduced", reduce_default_power);
	cmd.AddValue("show_log", "show log", show_log);
	cmd.Parse(argc, argv);


	NS_LOG_INFO("rand seed: " << rand_seed);
	SeedManager::SetSeed(rand_seed);

	NodeContainer node_container;
	node_container.Create(num_nodes);

	// random rectangel topology
	double min_x{-1200}, max_x{1200};
	double min_y{-1200}, max_y{1200};
	set_mobility_rectangle(node_container, min_x, min_y, max_x, max_y);

	WifiHelper wifi;
	wifi.SetStandard(WIFI_PHY_STANDARD_80211ah);
	wifi.SetRemoteStationManager("ns3::GradPCWifiManager",
								 // wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
								 "DataMode",
								 StringValue(phy_mode),
								 "ControlMode",
								 StringValue(phy_mode));
	// device_vec stores devices that uses different radios(channels)
	std::vector<NetDeviceContainer> device_vec(device_num);

	// TxPowerEnd is the maximum power, TxPowerStart is the minimum power
	const double TxPowerStart{default_tx_power};
	const double TxPowerEnd = TxPowerStart; // default tx_power 20.0dBm = 100mW
	const int power_levels{2};
	double freq{920.0}; // [Mhz] operating frequency
	const double path_loss_exponent{2.5};
	const double ref_distance{1.0};
	const double ref_loss = calculate_refence_loss(freq, ref_distance); // dB
	for (unsigned int i = 0; i < device_num; ++i)
	{
		YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();
		Wifiphy_Setting(wifiPhy, freq, TxPowerStart, TxPowerEnd, power_levels,rx_noise_figure, cca_threshold);

		YansWifiChannelHelper wifiChannel;
		WifiChannel_Setting(wifiChannel, path_loss_exponent, ref_loss);
		wifiPhy.SetChannel(wifiChannel.Create());

		// Add an upper mac and set it to adhoc mode
		S1gWifiMacHelper wifiMac = S1gWifiMacHelper();
		wifiMac.SetType("ns3::AdhocWifiMac", "S1gSupported", BooleanValue(true));
		device_vec.at(i) = wifi.Install(wifiPhy, wifiMac, node_container);

		// if (i == 0)
		// wifiPhy.EnablePcap("lab", device_vec.at(i));
	}

	std::vector<unsigned int> src_node_vec;
	std::vector<unsigned int> dst_node_vec;
	// rand_src_dst_pairs(src_node_vec, dst_node_vec, node_container, rand_seed);
	assign_src_dst_pair(rand_seed, num_nodes, src_node_vec, dst_node_vec);
	// print_vec(src_node_vec);
	// print_vec(dst_node_vec);

	// need to install the InternetStackHelper before installing dsr
	InternetStackHelper internet;
	    if (routing_method == "dsr")
	    {
		    internet.Install(node_container);
		    // install dsr
		    DsrMainHelper dsrMain;
		    DsrHelper dsr;
		// dsr.Set("RreqRetries", UintegerValue(8));
		dsr.Set("MinLifeTime", TimeValue(Seconds(10.0)));
		// dsr.Set("NonPropRequestTimeout", TimeValue(MilliSeconds(64.0))); // default is 30ms
		// dsr.Set("LinkAckTimeout", TimeValue(MilliSeconds(120.0)));		 // default is 100ms
		// dsr.Set("DiscoveryHopLimit", UintegerValue(8));
		// dsr.Set("MaxSendBuffLen", UintegerValue(128)); // default is 64
		dsrMain.Install(dsr, node_container);
	}
	    else if (routing_method == "aodv")
	    {
		    // settings of aodv
		    AodvHelper aodv_helper;
		    aodv_helper.Set("EnableHello", BooleanValue(false));
			// aodv_helper.Set("RreqRateLimit", UintegerValue(12)); // default is 10
			aodv_helper.Set("ActiveRouteTimeout", TimeValue(Seconds(10.0)));
			if (enable_hello && enable_power_control)
				aodv_helper.Set("GradPCAppIdx", IntegerValue(1));

		// install the InternetStackHelper and aodv routing
		InternetStackHelper internet;
		internet.SetRoutingHelper(aodv_helper); // has effect on the next Install ()
		internet.Install(node_container);
		if (show_log)
		{
			LogComponentEnable("AodvRoutingProtocol", LOG_LEVEL_ALL);
		}
	}
	else
	{
		std::cerr << "routing method not found!\n";
	}

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

	const unsigned int max_packet_num_per_round {300};
	const unsigned int packet_size{1024}; // bytes

	const unsigned int src_port_num {9};
	const unsigned int dst_port_num {80};
	// some checking before simulation
	NS_ASSERT_MSG(src_node_vec.size() == dst_node_vec.size(), "src_node_vec and dst_node_vec have unmatch elements.");
	NS_ASSERT_MSG(*max_element(src_node_vec.begin(), src_node_vec.end()) < num_nodes,
				  "invalid source node id in src_node_vec.");
	NS_ASSERT_MSG(*max_element(dst_node_vec.begin(), dst_node_vec.end()) < num_nodes,
				  "invalid destination node id in dst_node_vec.");

	// Simulation settings
	Time total_simulation_time = Seconds(60); // [second]
	const DataRate data_rate("300Kbps");	  // bps: bits per second
	Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();

		Time hello_beacon_start_time = Seconds(0);
		Time hello_beacon_stop_time = Seconds(0);
		Time grad_pc_start_time = Seconds(0);
		Time grad_pc_stop_time = Seconds(0);
			if (enable_hello)
			{
				if (show_log)
				{
					std::cout << "[rand-exp] "<< routing_method << " hello=on power_control=" << (enable_power_control ? "on" : "off") << "\n";
				}
				hello_beacon_start_time = Seconds(1.0);						// [second]
				hello_beacon_stop_time = Seconds(20.0);						// [second]
				if (enable_power_control)
				{
					grad_pc_start_time = hello_beacon_stop_time + Seconds(1.0); // [second]
					grad_pc_stop_time = grad_pc_start_time + Seconds(1.0);		// [second]
				}
				Time control_stop_time = enable_power_control ? grad_pc_stop_time : hello_beacon_stop_time;
				total_simulation_time += control_stop_time + Seconds(1);

			Time hello_interval = MilliSeconds(180.0); // milliseconds
			float extra_tx_distance{80.0};
			if (use_biconn)
				extra_tx_distance = 0.0;
			std::vector<short> gradPC_func_vec;
			if (enable_power_control)
			{
				GradPC_App::GradPC_type func_type;
				if (gradpc_type == 1)
				{
					func_type = GradPC_App::GradPC_type::torque;
					if (show_log) std::cout << "[rand-exp] power_control=GradPC torque\n";
				}
				else if (gradpc_type == 2)
				{
					func_type = GradPC_App::GradPC_type::proportional;
					if (show_log) std::cout << "[rand-exp] power_control=GradPC proportional\n";
				}
				else if (gradpc_type == 3)
				{
					func_type = GradPC_App::GradPC_type::logarithmic;
					if (show_log) std::cout << "[rand-exp] power_control=GradPC logarithmic\n";
				}
				else
				{
					NS_ASSERT_MSG(false, "Not a valid gradpc_type option");
				}
				for (unsigned int i{1}; i <= device_num; ++i)
				{
					gradPC_func_vec.emplace_back(static_cast<short>(func_type));
				}
			}

			for (unsigned int i{}; i < num_nodes; ++i)
			{
				Ptr<Node> node = node_container.Get(i);

			// Install Hello_beacon_App on each node
			Ptr<Hello_beacon_App> hello_beacon_app = CreateObject<Hello_beacon_App>();
			hello_beacon_app->set_data_rate(data_rate);
			hello_beacon_app->set_hello_interval(hello_interval);
			const unsigned int max_backoff_slot = 32;
			const unsigned int min_backoff_slot = 0;
			hello_beacon_app->set_backoff_limit(max_backoff_slot, min_backoff_slot);
			hello_beacon_app->set_backoff_slot_time(Time("4ms"));
			hello_beacon_app->set_max_packet_count(30);
			// hello_beacon_app->enable_print_neighbor = true;

			hello_beacon_app->SetStartTime(hello_beacon_start_time);
			hello_beacon_app->SetStopTime(hello_beacon_stop_time);
				node->AddApplication(hello_beacon_app);

				if (enable_power_control)
				{
					// Install GradPC_App on each node
					Ptr<GradPC_App> grad_pc_app = CreateObject<GradPC_App>();
					grad_pc_app->set_gradPC_func(gradPC_func_vec);
					// grad_pc_app->m_print_neighbor_list = true;
					grad_pc_app->gradPC_proportional_delta = 0.35;
					grad_pc_app->set_extra_tx_distance(extra_tx_distance);
					if (reduce_default_power)
						grad_pc_app->set_reduce_default_power();
					if (gradpc_type == 2)
					{
						grad_pc_app->gradPC_proportional_delta = gradpc_delta;
					}
					else if (gradpc_type == 3)
					{
						grad_pc_app->set_gradpc_log_k(gradpc_log_k);
					}
					grad_pc_app->SetStartTime(grad_pc_start_time);
					grad_pc_app->SetStopTime(grad_pc_stop_time);
					node->AddApplication(grad_pc_app);
				}
			}
		}
	else if (use_biconn)
	{
		NS_LOG_INFO("BICONN");
		biconn_func(node_container, TxPowerStart, ref_loss, path_loss_exponent, cca_threshold);
	}
		else
		{
			if (show_log)
			{
				std::cout << "[rand-exp] hello=off (default), devices=" << device_num << "\n";
			}
		}

		Time recv_pkt_start_time =
			(enable_hello) ? ((enable_power_control ? grad_pc_stop_time : hello_beacon_stop_time) + Seconds(1))
						 : Seconds(0.01);
	Time recv_pkt_end_time = total_simulation_time - Seconds(1);
	Time send_pkt_end_time = total_simulation_time - Seconds(2);
	double min_send_pkt_start_time = (recv_pkt_start_time).GetSeconds();
	double max_send_pkt_start_time = (recv_pkt_start_time + MilliSeconds(5.0)).GetSeconds();
	for (unsigned int i {0}; i < src_node_vec.size(); ++i)
	{
		Ptr<Node> src_node = node_container.Get(src_node_vec[i]);
		Ptr<Ipv4> src_ipv4 = src_node->GetObject<Ipv4>();
		Ptr<Node> dst_node = node_container.Get(dst_node_vec[i]);
		Ptr<Ipv4> dst_ipv4 = dst_node->GetObject<Ipv4>();

		// Install SendPacketApp on each source node
		Ptr<SendPacketApp> send_pkt_app = CreateObject<SendPacketApp>();
		send_pkt_app->Setup(dst_ipv4, src_port_num, dst_port_num, packet_size, send_packet_num, data_rate);
		send_pkt_app->Set_max_packet_num_per_round(max_packet_num_per_round);
		send_pkt_app->m_multi_channel = true;
		Time send_pkt_start_time = Seconds(uv->GetValue(min_send_pkt_start_time, max_send_pkt_start_time));
		// NS_LOG_INFO("[node " << src_node->GetId()
		//           << "] send_pkt_start_time: " << send_pkt_start_time.As(Time::MS));
		send_pkt_app->SetStartTime(send_pkt_start_time);
		send_pkt_app->SetStopTime(send_pkt_end_time);
		src_node->AddApplication(send_pkt_app);
	}

	for (const uint32_t dst_node_id : dst_node_vec)
	{
		Ptr<Node> dst_node = node_container.Get(dst_node_id);
		// Install RecvPacketApp on each destination node
		Ptr<RecvPacketApp> recv_pkt_app = CreateObject<RecvPacketApp>(dst_port_num);
		recv_pkt_app->SetStartTime(recv_pkt_start_time);
		recv_pkt_app->SetStopTime(recv_pkt_end_time);
		recv_pkt_app->set_count_delay();
		dst_node->AddApplication(recv_pkt_app);
	}
	
	// print source / destination node pair
	for (unsigned int i{}; i < src_node_vec.size(); ++i)
	{
		NS_LOG_INFO("source node: " << std::setw(3) << src_node_vec[i] << " ---> destination node: " << std::setw(3)
									<< dst_node_vec[i]);
	}

	// Start simulation
	Simulator::Stop(total_simulation_time);
	Simulator::Run();

	// export node_position and node_power to plot lab result
	if (export_node_info)
	{
		export_node_position(node_container);
		export_node_power(node_container);
	}
	
	// Export ETX matrix
	export_etx_matrix(node_container);

	unsigned long total_recv_packets{};
	float avg_delay{0.0};
	for (unsigned long i{}; i < src_node_vec.size(); ++i)
	{
		unsigned int src_node_id = src_node_vec[i];
		unsigned int dst_node_id = dst_node_vec[i];
		Ptr<Node> dst_node = node_container.Get(dst_node_id);
		// FIXME: recv_pkt_app is the last application may need to change if more applications are added!!
		const int recv_pkt_app_id = dst_node->GetNApplications() - 1;
		auto recv_pkt_app = DynamicCast<RecvPacketApp>(dst_node->GetApplication(recv_pkt_app_id));
		total_recv_packets += recv_pkt_app->get_total_recv_packet();
		avg_delay += recv_pkt_app->get_total_delay_time(src_node_id);
	}
	avg_delay /= total_recv_packets;

	Simulator::Destroy();

	//NS_LOG_INFO("Total received packets: " << total_recv_packets);
	// NS_LOG_INFO("Avg delay: " << avg_delay);
	std::cerr << "Total received packets: " << total_recv_packets << '\n';
	//std::cerr << "Avg delay: " << avg_delay << '\n'; // << " seconds\n";
	return 0;
}
