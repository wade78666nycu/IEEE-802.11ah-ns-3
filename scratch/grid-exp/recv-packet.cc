#include "recv-packet.h"

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("RecvPacketApp");

RecvPacketApp::RecvPacketApp(const unsigned port_num)
    : port(port_num),
      m_running(false),
      m_packets_recv(0)
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
    // create listening socket for every net devices
    Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4>();
    const auto socket_type_id = TypeId::LookupByName("ns3::UdpSocketFactory");
    const unsigned int device_num = m_node->GetNDevices();
    // starting from 1 since index 0 is for loopback(127.0.0.1)
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
    // Address localAddress;
    while ((packet = socket->RecvFrom(from)))
    {
        // socket->GetSockName(localAddress);
        uint32_t receive_size = packet->GetSize();
        if (receive_size <= 0)
            return;

        //NS_LOG_DEBUG("At time " << Simulator::Now().As(Time::MS) << " node " << m_node->GetId()
        //                        << " received one packet");

        //  packet->RemoveAllPacketTags();
        //  packet->RemoveAllByteTags();

        //* read packet content
        if (print_data) {
            uint8_t* data = new uint8_t[receive_size];
            packet->CopyData(data, sizeof(uint8_t) * receive_size);
            std::string packet_str = reinterpret_cast<char*>(data);
            packet_str = packet_str.substr(0, receive_size);
            delete[] data;
            //*/
            NS_LOG_INFO("Data: " << packet_str);
        }

        ++m_packets_recv;
    }
}

unsigned int
RecvPacketApp::get_total_recv_packet() {
    return m_packets_recv;
}
