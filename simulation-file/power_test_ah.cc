#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/core-module.h"
#include "ns3/double.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/network-module.h"
#include "ns3/string.h"
#include "ns3/wifi-module.h"
#include "ns3/wifi-phy.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"

using namespace ns3;

double
DbmToW(double dBm)
{
	return std::pow(10.0, 0.1 * (dBm - 30.0));
}

double
WToDbm(double w)
{
	return 10.0 * std::log10(w) + 30.0;
}

// const_num calculation: https://en.wikipedia.org/wiki/Free-space_path_loss
double
calculate_refence_loss(const double freq, const double ref_distance)
{
	// freq is in MHz and ref_distance is in meters
	const double const_num = -27.55;
	return 20 * log10(ref_distance) + 20 * log10(freq) + const_num;
}

unsigned int pkt_counter;

// Function called when a packet is received.
// \param socket The receiving socket.
void
ReceivePacket(Ptr<Socket> socket)
{
	Ptr<Packet> packet;
	Address from;
	while ((packet = socket->RecvFrom(from)))
	{
		Ipv4Address tmp = InetSocketAddress::ConvertFrom(from).GetIpv4();
		NS_LOG_UNCOND("Received one packet!");
		NS_LOG_UNCOND("from ipv4: " << tmp);
		++pkt_counter;
	}
}

static void
GenerateTraffic(Ptr<Socket> socket,
				uint32_t packet_size,
				uint32_t packet_count,
				const Ipv4Address ipv4_addr,
				const double packet_interval)
{
	socket->SetIpTtl(1); // single hop only
	InetSocketAddress recvSink = InetSocketAddress(ipv4_addr, 80);
	socket->SendTo(Create<Packet>(packet_size), 0, recvSink);
	// socket->Send(Create<Packet>(packet_size));
	if (--packet_count > 0)
	{
		Simulator::Schedule(Seconds(packet_interval),
							&GenerateTraffic,
							socket,
							packet_size,
							packet_count,
							ipv4_addr,
							packet_interval);
	}
}

void
monitor_sniffer(std::string context,
				Ptr<const Packet> packet,
				uint16_t channelFreqMhz,
				uint16_t channelNumber,
				uint32_t rate,
				bool isShortPreamble,
				WifiTxVector txVector,
				double signalDbm,
				double noiseDbm)
{
	NS_LOG_UNCOND(" Signal: " << signalDbm << " Noise: " << noiseDbm << std::endl);
}

void
set_tx_power(Ptr<Node> node, const double tx_power)
{
	Ptr<WifiNetDevice> wifi_device = DynamicCast<WifiNetDevice>(node->GetDevice(0));
	Ptr<YansWifiPhy> wifi_phy = DynamicCast<YansWifiPhy>(wifi_device->GetPhy());

	double tx_power_W = DbmToW(tx_power);
	char buffer[100];
	int return_size = snprintf(buffer, 100, "[info] tx_power: %3.1f [mW]", tx_power_W * 1000);
	if (return_size <= 0)
	{
		NS_LOG_UNCOND("Error: snprintf return size is less than 0.\n");
		return;
	}
	NS_LOG_UNCOND(buffer);
	NS_LOG_UNCOND("tx_power dBm: " << tx_power);
	// std::cerr << "[info] tx_power: " << tx_power * 1000 << "[mW]\n";
	wifi_phy->SetTxPowerStart(tx_power);
	wifi_phy->SetTxPowerEnd(tx_power);
}

int
main(int argc, char* argv[])
{
	std::string phy_mode("OfdmRate300KbpsBW1MHz");
	uint32_t packet_size = 100; // bytes
	uint32_t packet_count = 1;
	double packet_interval = 1.0; // seconds

	const int num_nodes = 3;
	NodeContainer c;
	c.Create(num_nodes);

	// The below set of helpers will help us to put together the wifi NICs we want
	WifiHelper wifi;
	wifi.SetStandard(WIFI_PHY_STANDARD_80211ah);

	const double TxPower = 15.0; // dBm
	double freq = 920;			 // [Mhz] operating frequency
	YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();
	wifiPhy.Set("ShortGuardEnabled", BooleanValue(false));
	wifiPhy.Set("ChannelWidth", UintegerValue(1));
	// NOTE!! EnergyDetectionThreshold is the cca threshold!!
	wifiPhy.Set("EnergyDetectionThreshold", DoubleValue(-96.0));
	// wifiPhy.Set("CcaMode1Threshold", DoubleValue(-99.0)); // FIXME: Don't know
	// what this is??
	wifiPhy.Set("TxGain", DoubleValue(0.0));
	wifiPhy.Set("RxGain", DoubleValue(0.0));
	wifiPhy.Set("TxPowerLevels", UintegerValue(1));
	wifiPhy.Set("TxPowerEnd", DoubleValue(TxPower));
	wifiPhy.Set("TxPowerStart", DoubleValue(TxPower));
	wifiPhy.Set("RxNoiseFigure", DoubleValue(7));
	wifiPhy.Set("LdpcEnabled", BooleanValue(false));
	wifiPhy.Set("S1g1MfieldEnabled", BooleanValue(false));

	// IEEE 802.11ah operates in unlicensed sub-GHz frequency bands
	// (863–868 MHz in Europe, 755–787 MHz in China and 902–928 MHz in
	// North-America)
	wifiPhy.Set("Frequency", UintegerValue(freq));
	const double ref_distance = 1.0;
	const double ref_loss = calculate_refence_loss(freq, ref_distance); // dB
	const double path_loss_exponent = 2.5;
	std::cout << "ref_loss: " << ref_loss << "\n";

	YansWifiChannelHelper wifiChannel;
	wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
	wifiChannel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
								   "Exponent",
								   DoubleValue(path_loss_exponent),
								   "ReferenceLoss",
								   DoubleValue(ref_loss),
								   "ReferenceDistance",
								   DoubleValue(ref_distance));

	wifiPhy.SetErrorRateModel("ns3::YansErrorRateModel");
	wifiPhy.SetChannel(wifiChannel.Create());

	wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
								 "DataMode",
								 StringValue(phy_mode),
								 "ControlMode",
								 StringValue(phy_mode));
	// Add a mac and disable rate control
	// WifiMacHelper wifiMac;
	// Set it to adhoc mode
	S1gWifiMacHelper wifiMac = S1gWifiMacHelper();
	wifiMac.SetType("ns3::AdhocWifiMac",
					"S1gSupported",
					BooleanValue(false),
					"HtSupported",
					BooleanValue(false));
	NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, c);

	MobilityHelper mobility;
	Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
	std::vector<float> pos_vec = {0, 1482.41};
	pos_vec.emplace_back(pos_vec.back() + 1.0);
	for (unsigned int i = 0; i < pos_vec.size(); ++i)
	{
		positionAlloc->Add(Vector(pos_vec[i], 0.0, 0.0));
	}
	mobility.SetPositionAllocator(positionAlloc);
	mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
	mobility.Install(c);

	InternetStackHelper internet;
	internet.Install(c);

	Ipv4AddressHelper ipv4;
	ipv4.SetBase("10.1.1.0", "255.255.255.0");
	Ipv4InterfaceContainer ipv4_iface_container = ipv4.Assign(devices);

	// create sockets
	TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
	std::vector<Ptr<Socket>> socket_vec;
	for (int i = 0; i < c.GetN(); ++i)
	{
		Ptr<Socket> skt = Socket::CreateSocket(c.Get(i), tid);
		auto ipv4_addr = ipv4_iface_container.GetAddress(i);
		InetSocketAddress local = InetSocketAddress(ipv4_addr, 80);
		skt->Bind(local);
		socket_vec.emplace_back(skt);
	}
	unsigned int recv_node_id = 0;
	socket_vec[recv_node_id]->SetRecvCallback(MakeCallback(&ReceivePacket));

	// Tracing
	// wifiPhy.EnablePcap("test_power", devices);
	Config::Connect("/NodeList/0/DeviceList/0/$ns3::WifiNetDevice/Phy/MonitorSnifferRx",
					MakeCallback(&monitor_sniffer));

	Time t1 = Seconds(1);
	Time t2 = Seconds(2);
	Time t3 = Seconds(3);
	Time t4 = Seconds(4);
	pkt_counter = 0;
	Ipv4Address recv_addr = ipv4_iface_container.GetAddress(recv_node_id);
	Simulator::Schedule(t1,
						&GenerateTraffic,
						socket_vec[1],
						packet_size,
						packet_count,
						recv_addr,
						packet_interval);
	Simulator::Schedule(t2,
						&GenerateTraffic,
						socket_vec[2],
						packet_size,
						packet_count,
						recv_addr,
						packet_interval);
	// Simulator::Schedule(t3, &set_tx_power, c.Get(2), TxPower + 5.0);
	// Simulator::Schedule(t4, &GenerateTraffic, socket_vec[2], packet_size,
	//                     packet_count, recv_addr, packet_interval);

	Time total_simulation_time = Seconds(10);
	Simulator::Stop(total_simulation_time);
	Simulator::Run();
	NS_LOG_UNCOND("pkt received: " << pkt_counter);
	Simulator::Destroy();
	return 0;
}
