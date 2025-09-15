#include "forward-packet.h"

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("ForwardPktApp");

static std::string
convert_ipv4_to_string(const Ipv4Address ipv4_addr)
{
    uint32_t addr = ipv4_addr.Get(); // get the address in uint32_t type
    std::string ipv4_str =
        std::to_string((addr >> 24) & 0xff) + "." + std::to_string((addr >> 16) & 0xff) + "." +
        std::to_string((addr >> 8) & 0xff) + "." + std::to_string((addr >> 0) & 0xff);
    return ipv4_str;
}

template<typename T>
static void pop_front(std::vector<T>& vec)
{
    //assert(!vec.empty());
    vec.erase(vec.begin());
}

ForwardPktApp::ForwardPktApp()
    : max_packet_num_per_round(100),
      is_source(false),
      is_destination(false),
      print_data(false),
      m_send_buffer_max(128),
      m_recv_sink(InetSocketAddress(uint16_t(0))),
      m_packet_size(1024),
      m_send_packet_num(0),
      m_data_rate(0),
      m_send_event(),
      m_running(false),
      m_packets_sent(0),
      m_packets_recv(0)
{
}

ForwardPktApp::~ForwardPktApp()
{
    for (auto m_socket : m_rx_socket_vec)
    {
        m_socket = nullptr;
    }
    if (is_source)
    {
        for (auto m_socket : m_tx_socket_vec)
        {
            m_socket = nullptr;
        }
    }
}

TypeId
ForwardPktApp::GetTypeId()
{
    static TypeId tid = TypeId("ForwardPktApp")
                            .SetParent<Application>()
                            .SetGroupName("ForwardPktApp")
                            .AddConstructor<ForwardPktApp>();
    return tid;
}

uint32_t 
ForwardPktApp::get_pkt_sent() {
    return m_packets_sent;
}
uint32_t 
ForwardPktApp::get_pkt_recv() {
    return m_packets_recv;
}

void
ForwardPktApp::Setup(std::unordered_map<next_hop_key, std::string> hop_map,
                     unsigned int src_port_num,
                     unsigned int dst_port_num)
{
    // only setup when the app is not running
    NS_ASSERT_MSG(!m_running, "Setup is called while ForwardPktApp is still running.");
    next_hop_map = hop_map;
    m_src_port = src_port_num;
    m_dst_port = dst_port_num;
}

void
ForwardPktApp::Set_send(uint32_t packet_size, uint32_t send_packet_num, DataRate data_rate)
{
    m_packet_size = packet_size;
    m_send_packet_num = send_packet_num;
    m_data_rate = data_rate;
}

void
ForwardPktApp::Set_max_packet_num_per_round(const unsigned int max_num)
{
    max_packet_num_per_round = max_num;
}

void
ForwardPktApp::StartApplication()
{
    NS_ASSERT_MSG(max_packet_num_per_round > 0, "max_packet_num_per_round is invalid");

    unsigned int net_card_num = m_node->GetNDevices() - 1;

    // Initialize receive sockets
    Ptr<Ipv4> m_src_ipv4 = m_node->GetObject<Ipv4>();
    const auto socket_type_id = TypeId::LookupByName("ns3::UdpSocketFactory");
    for (unsigned int channel = 1; channel <= net_card_num; ++channel)
    {
        Ptr<Socket> m_socket = Socket::CreateSocket(m_node, socket_type_id);
        Ipv4Address m_src_addr = m_src_ipv4->GetAddress(channel, 0).GetLocal();
        m_socket->Bind(InetSocketAddress(m_src_addr, m_dst_port));
        m_socket->SetRecvCallback(MakeCallback(&ForwardPktApp::handle_recv_packet, this));
        m_rx_socket_vec.emplace_back(m_socket);
    }

    // Initialize transmit sockets
    for (unsigned int channel = 1; channel <= net_card_num; ++channel)
    {
        Ptr<Socket> m_socket = Socket::CreateSocket(m_node, socket_type_id);
        m_socket->SetAllowBroadcast(true);
        // m_socket->SetRecvCallback(MakeCallback());
        Ipv4Address m_src_addr = m_src_ipv4->GetAddress(channel, 0).GetLocal();
        m_socket->Bind(InetSocketAddress(m_src_addr, m_src_port));
        m_tx_socket_vec.emplace_back(m_socket);
    }

    // m_recv_sink = InetSocketAddress(m_dst_addr, m_dst_port);
    // m_socket->Connect(m_recv_sink);

    m_packets_sent = 0;
    packet_msg = std::to_string(m_packets_sent) + " ";
    m_running = true;
    if (is_source)
    {
        // SendPacket(start_key);
        Simulator::Schedule(start_send_pkt_time, &ForwardPktApp::gen_packet, this, start_key);
    }
}

void
ForwardPktApp::StopApplication()
{
    m_running = false;
    if (m_send_event.IsRunning())
    {
        Simulator::Cancel(m_send_event);
    }

    for (auto m_socket : m_tx_socket_vec)
    {
        if (m_socket)
        {
            m_socket->Close();
        }
    }
    for (auto m_socket : m_rx_socket_vec)
    {
        if (m_socket)
        {
            m_socket->Close();
        }
    }
    if (is_source)
    {
        NS_LOG_INFO("[lab] "
                    << "[node " << m_node->GetId() << "] total packets sent: " << m_packets_sent);
    }

    if (is_destination)
    {
        NS_LOG_INFO("[lab] "
                    << "[node " << m_node->GetId() << "] total packets recv: " << m_packets_recv);
    }
}

void
ForwardPktApp::gen_packet(const next_hop_key key)
{
    if (m_packets_sent >= m_send_packet_num)
        return;

    //NS_LOG_DEBUG("pkt sent: " << m_packets_sent);
    packet_msg = std::to_string(key.dst) + " " + std::to_string(m_packets_sent);
    Ptr<Packet> packet =
        Create<Packet>(reinterpret_cast<const uint8_t*>(packet_msg.c_str()), m_packet_size);

    const std::lock_guard<std::mutex> lock(m_mutex);
    if (m_send_buffer.size() <= m_send_buffer_max) {
        m_send_buffer.emplace_back(packet);
        ScheduleTx(key);
    }

    Time tNext(Seconds(m_packet_size * 8 / static_cast<double>(m_data_rate.GetBitRate()) + 1));
    Simulator::Schedule(tNext, &ForwardPktApp::gen_packet, this, key);
}

void
ForwardPktApp::SendPacket(const next_hop_key key)
{
    if (next_hop_map.count(key) == 0)
        return;

    Ptr<Packet> packet = m_send_buffer.front()->Copy();
    packet->RemoveAllPacketTags();
    packet->RemoveAllByteTags();

    // use Send() if m_socket have been connected to destination
    // m_socket->Send(packet);

    auto get_channel_num = [](const std::string addr) -> unsigned int {
        return (Ipv4Address(addr.c_str()).Get() >> 8) & 0xff;
    };

    Ipv4Address next_hop_addr = Ipv4Address(next_hop_map[key].c_str());
    m_recv_sink = InetSocketAddress(next_hop_addr, m_dst_port);
    unsigned int channel = get_channel_num(key.our_addr) - 1;
    m_tx_socket_vec[channel]->SendTo(packet, 0, m_recv_sink);
    //m_tx_socket_vec[channel]->Connect(m_recv_sink);
    //m_tx_socket_vec[channel]->Send(packet);

    if (!m_send_buffer.empty()) {
        const std::lock_guard<std::mutex> lock(m_mutex);
        pop_front(m_send_buffer);
    }

    if (is_source && key == start_key) {
        ++m_packets_sent;
    }
}

void
ForwardPktApp::ScheduleTx(const next_hop_key key)
{
    if (m_running)
    {
        Time tNext(Seconds(m_packet_size * 8 / static_cast<double>(m_data_rate.GetBitRate())));
        //NS_LOG_DEBUG("tnext: " << tNext);
        m_send_event = Simulator::Schedule(tNext, &ForwardPktApp::SendPacket, this, key);
    }
}

void
ForwardPktApp::handle_recv_packet(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address src;
    Address local_addr;
    while ((packet = socket->RecvFrom(src)))
    {
        socket->GetSockName(local_addr);
        uint32_t receive_size = packet->GetSize();
        if (receive_size <= 0)
            return;

        // NS_LOG_DEBUG("At time " << Simulator::Now().As(Time::MS) << " node " << m_node->GetId()
        //                         << " received one packet");

        //  packet->RemoveAllPacketTags();
        //  packet->RemoveAllByteTags();

        //* read packet content

        uint8_t* data = new uint8_t[receive_size];
        packet->CopyData(data, sizeof(uint8_t) * receive_size);
        std::string packet_str = reinterpret_cast<char*>(data);
        packet_str = packet_str.substr(0, receive_size);
        delete[] data;
        std::stringstream ss;
        ss << packet_str;
        unsigned int dst_node_id;
        ss >> dst_node_id;

        if (m_node->GetId() == dst_node_id)
        {
            ++m_packets_recv;
        }
        else
        {
            Ipv4Address our_addr = InetSocketAddress::ConvertFrom(local_addr).GetIpv4();
            std::string our_addr_str = convert_ipv4_to_string(our_addr);
            next_hop_key key(dst_node_id, our_addr_str);

            const std::lock_guard<std::mutex> lock(m_mutex);
            if (m_send_buffer.size() <= m_send_buffer_max) {
                m_send_buffer.emplace_back(packet);
                ScheduleTx(key);
            }
        }

        if (print_data)
        {
            NS_LOG_INFO("[lab] [node " << m_node->GetId() << "] Data: " << packet_str);
        }
        ss.clear();
    }
}
