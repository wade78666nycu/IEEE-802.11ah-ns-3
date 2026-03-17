#include "recv-packet.h"
#include "send-packet.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/dsr-module.h"
#include "ns3/aodv-module.h"
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

static void
TraceWifiTxRate(std::string context,
                const Ptr<const Packet> packet,
                uint16_t channelFreqMhz,
                uint16_t channelNumber,
                uint32_t rate,
                bool isShortPreamble,
                WifiTxVector txvector)
{
	// Filter small control frames; focus on data transmissions.
	if (packet->GetSize() < 200)
	{
		return;
	}

	const double mbps = static_cast<double>(txvector.GetMode().GetDataRate()) / 1e6;
	std::cout << "[tx-rate] " << context << " ch=" << channelNumber
			  << " mode=" << txvector.GetMode().GetUniqueName()
			  << " rate=" << std::fixed << std::setprecision(3) << mbps << "Mbps"
			  << " size=" << packet->GetSize() << "B"
			  << " preamble=" << (isShortPreamble ? "short" : "long")
			  << std::endl;
}

void
export_node_position(const NodeContainer& node_container)
{
	std::string file_name = "grid_node_position.txt";
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
	std::string file_name = "grid_node_power.txt";
	std::ofstream export_file(file_name);
	if (export_file.is_open())
	{
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
	}
	return;
}

void
export_ett_matrix(const NodeContainer& node_container)
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

		// Write ETT matrix (rows = senders, columns = receivers)
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
					double ett = hello_app->get_ett(j, ifIndex);
					if (std::isinf(ett))
					{
						export_file << "INF";
					}
					else
					{
						export_file << std::fixed << std::setprecision(2) << ett;
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
		write_matrix("grid_etx_ch" + std::to_string(ifIndex) + ".csv", ifIndex);
	}
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
set_mobility_grid(NodeContainer& node_container, const double width, const double length, const unsigned int grid_width)
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
								  DoubleValue(width),
								  "DeltaY",
								  DoubleValue(-1 * length),
								  "GridWidth",
								  UintegerValue(grid_width),
								  "LayoutType",
								  StringValue("RowFirst"));
	mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
	mobility.Install(node_container);
	return;
}

NS_LOG_COMPONENT_DEFINE("Lab");

int
main(int argc, char* argv[])
{
	SeedManager::SetSeed(2);

	LogComponentEnable("Lab", LOG_LEVEL_INFO);
	LogComponentEnable("SendPacketApp", LOG_LEVEL_INFO);
	LogComponentEnable("RecvPacketApp", LOG_LEVEL_INFO);
	NS_LOG_INFO("Lab Start");

	unsigned int num_nodes{25};
	unsigned int device_num{3};
	unsigned int send_packet_num{1};
    unsigned int gradpc_type{2};
	unsigned int rx_noise_figure{7};
	double cca_threshold{-92.};
	double default_tx_power{15};
	bool export_node_info{false};
    bool enable_hello {true};
    bool enable_power_control {false};
    bool reduce_default_power {false};
    bool show_log {true};
    std::string routing_method {"aodv"};

	CommandLine cmd;
	cmd.AddValue("num_nodes", "total number of nodes", num_nodes);
    cmd.AddValue("device_num", "number of devices on each node", device_num);
	cmd.AddValue("send_packet_num", "numbers of packets to send", send_packet_num);
    cmd.AddValue("gradpc_type", "method of gradpc to use", gradpc_type);
	cmd.AddValue("export_node_info", "export node's position and power", export_node_info);
	cmd.AddValue("tx_power", "tx power", default_tx_power);
	cmd.AddValue("rx_noise_figure", "background Noise Figure", rx_noise_figure);
    cmd.AddValue("routing_method", "routing method used", routing_method);
	cmd.AddValue("enable_hello", "set true to enable hello beacon (ETX measurement)", enable_hello);
	cmd.AddValue("enable_power_control", "set false to disable tx power control (keep same power across channels)", enable_power_control);
    cmd.AddValue("show_log", "show log", show_log);
	cmd.Parse(argc, argv);

	NodeContainer node_container;
	node_container.Create(num_nodes);

    // grid topology
	const unsigned int node_per_row = sqrt(num_nodes);
    const float grid_len {2400.0};
    const float node_distance = grid_len / node_per_row;
	set_mobility_grid(node_container, node_distance, node_distance, node_per_row);

	WifiHelper wifi;
	wifi.SetStandard(WIFI_PHY_STANDARD_80211ah);
	// MinstrelWifiManager: adapts MCS based on per-station packet success/retry
	// statistics — no CalculateSnr() needed, compatible with 802.11ah S1G PHY.
	wifi.SetRemoteStationManager("ns3::ArfWifiManager");

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

		//if (i == 0)
		//wifiPhy.EnablePcap("lab", device_vec.at(i));
	}
	if(show_log){
		//Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/MonitorSnifferTx",
					//MakeCallback(&TraceWifiTxRate));
	}
	// need to install the InternetStackHelper before installing dsr
	InternetStackHelper internet;
	if (routing_method == "dsr") {
		internet.Install(node_container);
		// install dsr
		DsrMainHelper dsrMain;
		DsrHelper dsr;
		//dsr.Set("RreqRetries", UintegerValue(8));
		//dsr.Set("MinLifeTime", TimeValue(Seconds(3.0)));
		//dsr.Set("NonPropRequestTimeout", TimeValue(MilliSeconds(64.0))); // default is 30ms
		//dsr.Set("LinkAckTimeout", TimeValue(MilliSeconds(120.0)));		 // default is 100ms
		//dsr.Set("DiscoveryHopLimit", UintegerValue(8));
		//dsr.Set("MaxSendBuffLen", UintegerValue(128)); // default is 64
		dsrMain.Install(dsr, node_container);
	} else if(routing_method == "aodv") {
		// settings of aodv
		AodvHelper aodv_helper;
		aodv_helper.Set("EnableHello", BooleanValue(false));
		aodv_helper.Set("RreqRateLimit", UintegerValue(12)); // default is 10
		aodv_helper.Set("ActiveRouteTimeout", TimeValue(Seconds(10.0)));
		if (enable_hello && enable_power_control) aodv_helper.Set("GradPCAppIdx", IntegerValue(1));
		// install the InternetStackHelper and aodv routing
		InternetStackHelper internet;
		internet.SetRoutingHelper(aodv_helper); // has effect on the next Install ()
		internet.Install(node_container);
		if (show_log) {
			LogComponentEnable("AodvRoutingProtocol", LogLevel(LOG_LEVEL_DEBUG | LOG_PREFIX_TIME | LOG_PREFIX_NODE));
		}
	} else {
		NS_ASSERT_MSG(false, "routing method not found!"); 
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

	const unsigned int max_packet_num_per_round = 300;
	const unsigned int packet_size{1024}; // bytes

    std::vector<unsigned int> src_node_vec;
    std::vector<unsigned int> dst_node_vec;
	/*for (int i{}; i < node_per_row; ++i) {
        src_node_vec.emplace_back(node_per_row + i);
        dst_node_vec.emplace_back(num_nodes - (2 * node_per_row) + i);
    }*/
   src_node_vec.emplace_back(1);
   src_node_vec.emplace_back(0);
   dst_node_vec.emplace_back(23); 
   dst_node_vec.emplace_back(24); 

	const unsigned int src_port_num = 9;
	const unsigned int dst_port_num = 80;
	// some checking before simulation
	NS_ASSERT_MSG(src_node_vec.size() == dst_node_vec.size(), "src_node_vec and dst_node_vec have unmatch elements.");
	NS_ASSERT_MSG(*max_element(src_node_vec.begin(), src_node_vec.end()) < num_nodes,
				  "invalid source node id in src_node_vec.");
	NS_ASSERT_MSG(*max_element(dst_node_vec.begin(), dst_node_vec.end()) < num_nodes,
				  "invalid destination node id in dst_node_vec.");


	// Simulation settings
	Time total_simulation_time = Seconds(60); // [second]
	const DataRate data_rate("300Kbps");		 // bps: bits per second
	Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();

	Time hello_beacon_start_time = Seconds(0);
	Time hello_beacon_stop_time = Seconds(0);
	Time grad_pc_start_time = Seconds(0);
	Time grad_pc_stop_time = Seconds(0);
	if (enable_hello) {
		std::cout << "[grid-exp] "<< routing_method << " hello=on power_control=" << (enable_power_control ? "on" : "off") << "\n";
		hello_beacon_start_time = Seconds(1.0); // [second]
		hello_beacon_stop_time = Seconds(10.0); // [second]
		if (enable_power_control) {
			grad_pc_start_time = Seconds(11.0);	 // [second]g
		grad_pc_stop_time = Seconds(11.5);		 // [second]
	}
	Time control_stop_time = enable_power_control ? grad_pc_stop_time : hello_beacon_stop_time;
	total_simulation_time += control_stop_time + Seconds(1);

	Time hello_interval = MilliSeconds(180.0); // milliseconds
	const float extra_tx_distance{80.0};

	std::vector<short> gradPC_func_vec;
	if (enable_power_control)
	{
		GradPC_App::GradPC_type func_type;
		if (gradpc_type == 1) {
			func_type = GradPC_App::GradPC_type::torque;
			std::cout << "[grid-exp] power_control=GradPC torque\n";
		} else if (gradpc_type == 2) {
			func_type = GradPC_App::GradPC_type::proportional;
			std::cout << "[grid-exp] power_control=GradPC proportional\n";
		} else if (gradpc_type == 3) {
			func_type = GradPC_App::GradPC_type::logarithmic;
			std::cout << "[grid-exp] power_control=GradPC logarithmic\n";
		} else {
			NS_ASSERT_MSG(false, "Not a valid gradpc_type option");
		}
		for (int i {1}; i <= device_num; ++i) {
		gradPC_func_vec.emplace_back(static_cast<short>(func_type));
	}
	}

	for (unsigned int i {}; i < num_nodes; ++i)
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
			//grad_pc_app->m_print_neighbor_list = true;
			grad_pc_app->set_extra_tx_distance(extra_tx_distance);
			//if (change_default_power) grad_pc_app->set_change_default_power();
			grad_pc_app->SetStartTime(grad_pc_start_time);
			grad_pc_app->SetStopTime(grad_pc_stop_time);
			node->AddApplication(grad_pc_app);
		}
	}
	} else {
		std::cout << "[grid-exp] hello=off (default), devices=" << device_num << "\n";
	}
	
	Time recv_pkt_start_time =
		(enable_hello) ? ((enable_power_control ? grad_pc_stop_time : hello_beacon_stop_time) + Seconds(1))
						: Seconds(0.01);
	Time recv_pkt_end_time = total_simulation_time - Seconds(1);
	Time send_pkt_end_time = total_simulation_time - Seconds(2);
	double min_send_pkt_start_time = (recv_pkt_start_time).GetSeconds();
	double max_send_pkt_start_time = (recv_pkt_start_time + MilliSeconds(5.0)).GetSeconds();
	for (unsigned int i = 0; i < src_node_vec.size(); ++i)
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
		//NS_LOG_INFO("[node " << src_node->GetId()
		//			<< "] send_pkt_start_time: " << send_pkt_start_time.As(Time::MS));
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
		dst_node->AddApplication(recv_pkt_app);
	}

	
	for (unsigned int i {}; i < src_node_vec.size(); i++)
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
	
	// Export ETT matrix
	export_ett_matrix(node_container);

	unsigned long total_recv_packets = 0;
	for (int i{0}; i < dst_node_vec.size(); ++i)
	{
        int node_id = dst_node_vec[i];
		Ptr<Node> node = node_container.Get(node_id);
		// recv_pkt_app is the last application
		// may need to change if more applications are added!!
		const int recv_pkt_app_id = node->GetNApplications() - 1;
		total_recv_packets +=
			DynamicCast<RecvPacketApp>(node->GetApplication(recv_pkt_app_id))->get_total_recv_packet();
	}

	Simulator::Destroy();

	NS_LOG_INFO("Total received packets: " << total_recv_packets);
    //NS_LOG_INFO("Lab End");
	return 0;
}
