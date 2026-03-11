#include "send-packet.h"

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("SendPacketApp");

SendPacketApp::SendPacketApp()
	: max_packet_num_per_round(100),
	  m_multi_channel(false),
	  m_recv_sink(InetSocketAddress(uint16_t(0))),
	  m_packet_size(1024),
	  m_send_packet_num(0),
	  m_data_rate(0),
	  m_send_event(),
	  m_running(false),
	  m_packets_sent(0),
	  m_send_channel(1)
{
}

SendPacketApp::~SendPacketApp()
{
	for (auto m_socket : m_socket_vec)
	{
		m_socket = nullptr;
	}
}

TypeId
SendPacketApp::GetTypeId()
{
	static TypeId tid =
		TypeId("SendPacketApp").SetParent<Application>().SetGroupName("SendPacketApp").AddConstructor<SendPacketApp>();
	return tid;
}

void
SendPacketApp::Setup(Ptr<Ipv4> dst_ipv4,
					 unsigned int src_port_num,
					 unsigned int dst_port_num,
					 uint32_t packet_size,
					 uint32_t send_packet_num,
					 DataRate data_rate)
{
	// only setup when the app is not running
	NS_ASSERT_MSG(!m_running, "Setup is called while SendPacketApp is still running.");
	m_dst_ipv4 = dst_ipv4;
	m_src_port = src_port_num;
	m_dst_port = dst_port_num;
	m_packet_size = packet_size;
	m_send_packet_num = send_packet_num;
	m_data_rate = data_rate;
}

void
SendPacketApp::Set_max_packet_num_per_round(const unsigned int max_num)
{
	max_packet_num_per_round = max_num;
}

void
SendPacketApp::StartApplication()
{
	NS_ASSERT_MSG(max_packet_num_per_round > 0, "max_packet_num_per_round is invalid");
	unsigned int net_card_num = m_node->GetNDevices() - 1;

	// setup m_phy_vec and Initialize transmission sockets
	Ptr<Ipv4> m_src_ipv4 = m_node->GetObject<Ipv4>();
	const auto socket_type_id = TypeId::LookupByName("ns3::UdpSocketFactory");
	for (unsigned int channel = 1; channel <= net_card_num; ++channel)
	{
		// setup m_phy_vec
		// netdevice's index starts from 0, and the last index is for loopback
		Ptr<WifiNetDevice> wifi_device = DynamicCast<WifiNetDevice>(m_node->GetDevice(channel - 1));
		Ptr<YansWifiPhy> wifi_phy = DynamicCast<YansWifiPhy>(wifi_device->GetPhy());
		m_phy_vec.emplace_back(wifi_phy);

		// setup sockets
		Ptr<Socket> m_socket = Socket::CreateSocket(m_node, socket_type_id);
		m_socket->SetAllowBroadcast(true);
		// m_socket->SetRecvCallback(MakeCallback());
		Ipv4Address m_src_addr = m_src_ipv4->GetAddress(channel, 0).GetLocal();
		m_socket->Bind(InetSocketAddress(m_src_addr, m_src_port));
		m_socket_vec.emplace_back(m_socket);
	}

	m_packets_sent = 0;
	packet_msg = std::to_string(m_packets_sent) + " ";
	m_running = true;
	ScheduleTx();
}

void
SendPacketApp::StopApplication()
{
	m_running = false;
	if (m_send_event.IsRunning())
	{
		Simulator::Cancel(m_send_event);
	}

	for (auto m_socket : m_socket_vec)
	{
		if (m_socket)
		{ //
			m_socket->Close();
		}
	}
	--m_packets_sent;
	NS_LOG_INFO("[lab] "
				<< "[node " << m_node->GetId() << "] total packets sent: " << m_packets_sent);
}

void
SendPacketApp::SetupSocket()
{
	m_running = false;
	if (m_multi_channel)
	{
		// select the first channel with idle state to send packets
		// TODO: maybe check all the channels and randomly choose one?
		m_send_channel = 1;
		for (uint32_t i{1}; i < m_phy_vec.size(); ++i)
		{
			if (m_phy_vec[i]->IsStateIdle())
			{
				m_send_channel = i + 1;
				break;
			}
		}
	}

	Ipv4Address m_dst_addr = m_dst_ipv4->GetAddress(m_send_channel, 0).GetLocal();
	// Ipv4Address m_dst_addr = m_dst_ipv4->GetAddress(1, 0).GetLocal();
	// NS_LOG_DEBUG("m_dst_addr: " << m_dst_addr);
	m_recv_sink = InetSocketAddress(m_dst_addr, m_dst_port);
	// m_socket->Connect(m_recv_sink);
	m_running = true;
}

void
SendPacketApp::SendPacket()
{
	SetupSocket();
	Ptr<Packet> packet;
	if (packet_msg.empty())
	{
		packet = Create<Packet>(m_packet_size);
	}
	else
	{
		packet_msg = std::to_string(m_packets_sent) + " ";
		packet = Create<Packet>(reinterpret_cast<const uint8_t*>(packet_msg.c_str()), m_packet_size);
	}
	// use Send() if m_socket have been connected to destination
	// m_socket->Send(packet);

	// minus 1 since m_send_channel starts from 1
	m_socket_vec[m_send_channel - 1]->SendTo(packet, 0, m_recv_sink);

	if (++m_packets_sent <= m_send_packet_num)
	{
		if (m_packets_sent % max_packet_num_per_round == 0)
		{
			// wait a interval before sending again

			Time wait_time = Seconds(4.0);
			Simulator::Schedule(wait_time, &SendPacketApp::ScheduleTx, this);
		}
		else
		{
			ScheduleTx();
		}
	}
}

void
SendPacketApp::ScheduleTx()
{
	if (m_running)
	{
		Time tNext(Seconds(m_packet_size * 8 / static_cast<double>(m_data_rate.GetBitRate())));
		m_send_event = Simulator::Schedule(tNext, &SendPacketApp::SendPacket, this);
	}
}
