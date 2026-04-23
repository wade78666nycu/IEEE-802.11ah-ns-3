#include "hello-beacon.h"

using namespace ns3;
static Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();

NS_LOG_COMPONENT_DEFINE("HelloBeacon");

Hello_beacon_App::Hello_beacon_App()
    : enable_print_neighbor(false),
      run_interval(0),
      wait_interval(0),
      m_running(false),
      socket_type_id(TypeId::LookupByName("ns3::UdpSocketFactory")),
      m_tx_socket(nullptr),
      m_rx_socket(nullptr),
      m_packetSize(128),
      m_port(80),
      m_hello_interval(MilliSeconds(100.0)),
      m_backoff_slot_time(MicroSeconds(100.0)),
      m_packet_content(""),
      m_data_rate("3MB/s")
{
}

Hello_beacon_App::Hello_beacon_App(const unsigned int num_nodes)
    : Hello_beacon_App()
{
    m_neighbor_set.reserve(num_nodes);
    m_distance_vec.reserve(num_nodes);
}

Hello_beacon_App::~Hello_beacon_App()
{
    m_tx_socket = nullptr;
    m_rx_socket = nullptr;
}

TypeId
Hello_beacon_App::GetTypeId()
{
    static TypeId tid = TypeId("Hello_beacon_App")
                            .SetParent<Application>()
                            .SetGroupName("Hello_beacon")
                            .AddConstructor<Hello_beacon_App>();
    return tid;
}

void
Hello_beacon_App::set_data_rate(const DataRate data_rate)
{
    m_data_rate = data_rate;
}

void
Hello_beacon_App::set_hello_interval(const Time hello_interval)
{
    m_hello_interval = hello_interval;
}

void
Hello_beacon_App::set_port(const uint16_t port)
{
    m_port = port;
}

void
Hello_beacon_App::set_backoff_limit(const unsigned int max_backoff_slot,
                                    const unsigned int min_backoff_slot)
{
    uv->SetAttribute("Min", DoubleValue(max_backoff_slot));
    uv->SetAttribute("Max", DoubleValue(min_backoff_slot));
}

void
Hello_beacon_App::set_backoff_slot_time(const Time backoff_slot_time)
{
    // check if passed in backoff_slot_time is larger than default
    if (backoff_slot_time > m_backoff_slot_time)
    {
        m_backoff_slot_time = backoff_slot_time;
    }
}

// setup tx_socket and rx_socket
void
Hello_beacon_App::set_socket()
{
    Ptr<Node> node = GetNode();
    // setup rx_socket
    // NOTE: need to bind rx_socket with broadcast address to make SetRecvCallback work
    m_rx_socket = Socket::CreateSocket(node, socket_type_id);
    InetSocketAddress rx_local = InetSocketAddress("255.255.255.255", m_port);
    // Ipv4Address broadcast_ip_addr = Ipv4Address::ConvertFrom(node->GetDevice(0)->GetBroadcast());
    // InetSocketAddress rx_local = InetSocketAddress(broadcast_ip_addr, m_port);
    if (m_rx_socket->Bind(rx_local) == -1)
    {
        NS_FATAL_ERROR("Failed to bind socket");
    }
    m_rx_socket->SetRecvCallback(MakeCallback(&Hello_beacon_App::handle_recv_packet, this));

    // setup tx_socket
    m_tx_socket = Socket::CreateSocket(node, socket_type_id);
    m_tx_socket->BindToNetDevice(node->GetDevice(0));
    m_tx_socket->SetAllowBroadcast(true);
    m_tx_socket->Connect(rx_local);
}

std::unordered_set<uint32_t>
Hello_beacon_App::get_neighbor_set()
{
    return m_neighbor_set;
}

std::vector<std::pair<float, uint32_t>>
Hello_beacon_App::get_distance_vec()
{
    return m_distance_vec;
}

Time
Hello_beacon_App::get_backoff_time()
{
    return m_backoff_slot_time * uv->GetInteger();
}

void
Hello_beacon_App::StartApplication()
{
    // check if backoff_slot is set to valid values
    if (uv->GetMin() < 0.0 || uv->GetMax() * m_backoff_slot_time > m_hello_interval)
    {
        // if min/max values are not valid, set to default values
        const double max_backoff_slot = 32;
        const double min_backoff_slot = 0;
        set_backoff_limit(max_backoff_slot, min_backoff_slot);
    }
    set_socket();

    // if the node moves, need to move this part into send_periodically
    create_hello_packet();
    if (run_interval != 0) {
        Simulator::Schedule(run_interval, &Hello_beacon_App::StopApplication, this);
    }
    m_running = true;

    // start sending hello-beacon
    Time interval = get_backoff_time();
    m_send_event = Simulator::Schedule(interval, &Hello_beacon_App::send_periodically, this);
}

void
Hello_beacon_App::StopApplication()
{
    //NS_LOG_DEBUG("Hello beacon app stop");
    m_running = false;
    if (m_send_event.IsRunning())
    {
        Simulator::Cancel(m_send_event);
    }

    if (m_tx_socket)
    {
        m_tx_socket->Close();
        m_tx_socket = nullptr;
    }
    if (m_rx_socket)
    {
        m_rx_socket->Close();
        m_rx_socket = nullptr;
    }
    if (enable_print_neighbor)
        print_neighbor_set();

    if (wait_interval == 0) {
        // Hello_beacon_App only runs once
        return;
    }
    Simulator::Schedule(wait_interval, &Hello_beacon_App::StartApplication, this);
}

void
Hello_beacon_App::send_periodically()
{
    send_packet();
    /*Time interval = get_backoff_time();
    m_send_event = Simulator::Schedule(interval, &Hello_beacon_App::send_periodically, this);*/
}

// TODO: add my own header in the hello-beacon packet
void
Hello_beacon_App::create_hello_packet()
{
    Ptr<Node> node = GetNode();
    m_packet_content = std::to_string(node->GetId()) + " ";
    float x_pos = node->GetObject<MobilityModel>()->GetPosition().x;
    float y_pos = node->GetObject<MobilityModel>()->GetPosition().y;
    m_node_position = Vector2D(x_pos, y_pos);
    auto truncate_pos = [](const float position, const int precision = 1) {
        std::string trimmed_str =
            std::to_string(position).substr(0, std::to_string(position).find(".") + precision + 1);
        return trimmed_str;
    };
    //m_packet_content += "x" + truncate_pos(x_pos) + " y" + truncate_pos(y_pos) + " ";
    m_packet_content += truncate_pos(x_pos) + " " + truncate_pos(y_pos);
    // TODO: maybe add timestamp Simulator::Now() into packet_content
    // check if packet_content size is too large (NOTE: content max size is 255)
    const int max_data_size = 65507; // ns3::MAX_IPV4_UDP_DATAGRAM_SIZE
    NS_ASSERT_MSG(m_packet_content.length() <= max_data_size, "packet_content too large!\n");
    //m_packet_content += " hello message"; // remove this line after testing
}

void
Hello_beacon_App::send_packet()
{
    if (m_running)
    {
        Ptr<Packet> packet;
        if (m_packet_content.size() > 0)
        {
            uint8_t* buffer = reinterpret_cast<uint8_t*>(&m_packet_content[0]);
            packet = Create<Packet>(buffer, m_packet_content.length());
        }
        else
        {
            // create packet with default packet size 128[bytes]
            packet = Create<Packet>(m_packetSize);
        }
        SocketIpTtlTag tag;
        tag.SetTtl(1); // set ttl to 1 to find one hop neighbors
        packet->AddPacketTag(tag);
        m_tx_socket->Send(packet);
    }
}

// TODO: check if received packet is a hello-beacon packet from header
void
Hello_beacon_App::handle_recv_packet(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    // Address localAddress;
    while ((packet = socket->RecvFrom(from)))
    {
        // socket->GetSockName(localAddress);
        if (packet->GetSize() <= 0)
            return;

        uint32_t receivedSize = packet->GetSize();
        // packet->RemoveAllPacketTags();
        // packet->RemoveAllByteTags();

        // read packet content
        uint8_t* data = new uint8_t[receivedSize];
        packet->CopyData(data, sizeof(uint8_t) * receivedSize);
        std::string packet_str = reinterpret_cast<char*>(data);
        packet_str = packet_str.substr(0, receivedSize);
        //std::cout<<packet_str<<std::endl; (nodeID position_x position_y)
        delete[] data;

        std::stringstream ss;
        ss << packet_str;
        uint32_t node_id;
        ss >> node_id;
        // TODO: need to be more generic, access ipv6 address also
        if (m_neighbor_set.count(node_id) == 0)
        {
            // auto ipv4_address = InetSocketAddress::ConvertFrom(from).GetIpv4();
            m_neighbor_set.insert(node_id);

            float x_pos, y_pos;
            ss >> x_pos >> y_pos;
            m_distance_vec.emplace_back(CalculateDistance(m_node_position, Vector2D(x_pos, y_pos)),
                                        node_id);
        }
    }
}

// TODO: change std::cout to NS_LOG
void
Hello_beacon_App::print_neighbor_set()
{
    std::cout << std::string(10, '=');
    std::cout << "\t[info] node_id: " << GetNode()->GetId() << "\t";
    std::cout << std::string(10, '=');
    std::cout << "\n";

    // sort neighbor's id before printing
    std::vector<uint32_t> neighbor_id(m_neighbor_set.begin(), m_neighbor_set.end());
    std::sort(neighbor_id.begin(), neighbor_id.end());
    std::cout << "neighbor node id: ";
    for (const uint32_t id : neighbor_id)
    {
        std::cout << std::setw(2) << id << " ";
    }
    std::cout << "\n";

    // sort according to neighbors distance
    std::sort(m_distance_vec.begin(),
              m_distance_vec.end(),
              [](const std::pair<float, uint32_t>& p1, const std::pair<float, uint32_t>& p2) {
                  return p1.first > p2.first;
              });
    for (const auto& item_pair : m_distance_vec)
    {
        std::cout << "distance to node " << item_pair.second << ": " << item_pair.first
                  << " [meters]\n";
    }
}
