#include "shared-send-packet.h"

#include <algorithm>
#include <cstring>

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("SendPacketApp");

SendPacketApp::SendPacketApp()
    : max_packet_num_per_round(50),
      m_multi_channel(false),
      m_socket(nullptr),
      m_recv_sink(InetSocketAddress(uint16_t(0))),
      m_packet_size(256),
      m_send_packet_num(0),
      m_data_rate(0),
      m_send_event(),
      m_running(false),
      m_packets_sent(0)
{
}

SendPacketApp::~SendPacketApp()
{
    m_socket = nullptr;
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
    const auto socket_type_id = TypeId::LookupByName("ns3::UdpSocketFactory");
    m_socket = Socket::CreateSocket(m_node, socket_type_id);
    m_socket->SetAllowBroadcast(true);
    m_socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), m_src_port));

    m_packets_sent = 0;
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

    if (m_socket)
    {
        m_socket->Close();
    }
    NS_LOG_INFO("[lab] "
                << "[node " << m_node->GetId() << "] total packets sent: " << m_packets_sent);
}

void
SendPacketApp::SetupSocket()
{
    m_running = false;
    // The application targets a single destination IP and leaves interface/
    // path selection to the routing protocol.
    Ipv4Address m_dst_addr = m_dst_ipv4->GetAddress(1, 0).GetLocal();
    m_recv_sink = InetSocketAddress(m_dst_addr, m_dst_port);
    m_running = true;
}

void
SendPacketApp::SendPacket()
{
    SetupSocket();
    const uint32_t src_node_id = m_node->GetId();
    const std::string header =
        "src=" + std::to_string(src_node_id) + " seq=" + std::to_string(m_packets_sent) +
        " send_ns=" + std::to_string(Simulator::Now().GetNanoSeconds()) + " ";
    std::string payload(m_packet_size, '\0');
    const size_t copy_len = std::min(payload.size(), header.size());
    std::memcpy(payload.data(), header.data(), copy_len);
    Ptr<Packet> packet = Create<Packet>(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    if (m_packets_sent < m_send_packet_num)
    {
        m_socket->SendTo(packet, 0, m_recv_sink);
        if (((m_packets_sent + 1) % max_packet_num_per_round) == 0)
        {
            Ptr<UniformRandomVariable> rv = CreateObject<UniformRandomVariable>();
            Time wait_time = Seconds(3.0 + rv->GetValue(0.0, 2.0));
            Simulator::Schedule(wait_time, &SendPacketApp::ScheduleTx, this);
        }
        else
        {
            ScheduleTx();
        }
        m_packets_sent++;
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
