#include "shared-recv-packet.h"

#include <iomanip>
#include <string>

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("RecvPacketApp");

RecvPacketApp::RecvPacketApp(const unsigned port_num)
    : port(port_num),
      m_running(false),
    m_packets_recv(0),
    m_total_delay_ns(0),
    m_max_delay_ns(0),
    m_first_recv_ns(0),
    m_last_recv_ns(0)
{
}

RecvPacketApp::~RecvPacketApp()
{
    for (auto socket : m_socket_vec)
    {
        socket = nullptr;
    }
}

TypeId
RecvPacketApp::GetTypeId()
{
    static TypeId tid =
        TypeId("RecvPacketApp").SetParent<Application>().SetGroupName("RecvPacketApp");
    return tid;
}

void
RecvPacketApp::Setup()
{
    Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4>();
    const auto socket_type_id = TypeId::LookupByName("ns3::UdpSocketFactory");
    const unsigned int device_num = m_node->GetNDevices();
    for (unsigned int i = 1; i < device_num; ++i)
    {
        Ipv4Address addr = ipv4->GetAddress(i, 0).GetLocal();
        Ptr<Socket> socket_ptr = Socket::CreateSocket(GetNode(), socket_type_id);
        socket_ptr->Bind(InetSocketAddress(addr, port));
        socket_ptr->SetRecvCallback(MakeCallback(&RecvPacketApp::handle_recv_packet, this));
        m_socket_vec.emplace_back(socket_ptr);
    }
}

void
RecvPacketApp::StartApplication()
{
    NS_ASSERT_MSG(port != 0, "port number not initialized");
    Setup();
    m_running = true;
}

void
RecvPacketApp::StopApplication()
{
    m_running = false;
    for (auto socket : m_socket_vec)
    {
        socket->Close();
    }
    NS_LOG_INFO("[lab] "
                << "[node " << std::setw(2) << m_node->GetId() << "] total packets received: " << m_packets_recv);
}

void
RecvPacketApp::handle_recv_packet(Ptr<Socket> socket)
{
    bool print_data = true;
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from)))
    {
        uint32_t receive_size = packet->GetSize();
        if (receive_size <= 0)
            return;

        if (print_data)
        {
            uint8_t* data = new uint8_t[receive_size];
            packet->CopyData(data, sizeof(uint8_t) * receive_size);
            std::string packet_str = reinterpret_cast<char*>(data);
            packet_str = packet_str.substr(0, receive_size);
            delete[] data;
            NS_LOG_INFO("Data: " << packet_str);

            const std::string send_tag = "send_ns=";
            size_t send_pos = packet_str.find(send_tag);
            if (send_pos != std::string::npos)
            {
                send_pos += send_tag.size();
                size_t send_end = packet_str.find(' ', send_pos);
                const std::string send_ns_str = packet_str.substr(send_pos, send_end - send_pos);
                const uint64_t send_ns = static_cast<uint64_t>(std::stoull(send_ns_str));
                const uint64_t now_ns = static_cast<uint64_t>(Simulator::Now().GetNanoSeconds());
                if (now_ns >= send_ns)
                {
                    const uint64_t delay_ns = now_ns - send_ns;
                    m_total_delay_ns += delay_ns;
                    m_max_delay_ns = std::max(m_max_delay_ns, delay_ns);
                }
            }
        }

        const uint64_t recv_ns = static_cast<uint64_t>(Simulator::Now().GetNanoSeconds());
        if (m_packets_recv == 0)
        {
            m_first_recv_ns = recv_ns;
        }
        m_last_recv_ns = recv_ns;
        ++m_packets_recv;
    }
}

unsigned int
RecvPacketApp::get_total_recv_packet()
{
    return m_packets_recv;
}

uint64_t
RecvPacketApp::get_total_delay_ns() const
{
    return m_total_delay_ns;
}

uint64_t
RecvPacketApp::get_max_delay_ns() const
{
    return m_max_delay_ns;
}

uint64_t
RecvPacketApp::get_first_recv_ns() const
{
    return m_first_recv_ns;
}

uint64_t
RecvPacketApp::get_last_recv_ns() const
{
    return m_last_recv_ns;
}
