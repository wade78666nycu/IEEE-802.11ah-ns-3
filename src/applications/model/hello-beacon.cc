#include "hello-beacon.h"
#include <algorithm>
#include <limits>

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
      m_data_rate("3MB/s"),
      m_sequence_number(0),
      m_packet_count(0),
      m_max_packet_count(1)
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
    const double min_slot = static_cast<double>(std::min(min_backoff_slot, max_backoff_slot));
    const double max_slot = static_cast<double>(std::max(min_backoff_slot, max_backoff_slot));
    uv->SetAttribute("Min", DoubleValue(min_slot));
    uv->SetAttribute("Max", DoubleValue(max_slot));
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

void
Hello_beacon_App::set_max_packet_count(const uint32_t max_count)
{
    m_max_packet_count = max_count;
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

double
Hello_beacon_App::get_delivery_ratio(uint32_t neighbor_id)
{
    return CalculateDeliveryRatio(neighbor_id);
}

Time
Hello_beacon_App::get_backoff_time()
{
    return m_backoff_slot_time * uv->GetInteger();
}

void
Hello_beacon_App::StartApplication()
{
    // Open debug file for this node
    //std::string filename = "hello_beacon_debug_node_" + std::to_string(GetNode()->GetId()) + ".txt";
    //m_debug_file.open(filename, std::ios::app);
    
    if (uv->GetMin() < 0.0 || uv->GetMax() * m_backoff_slot_time > m_hello_interval)
    {
        // if min/max values are not valid, set to default values
        const double max_backoff_slot = 32;
        const double min_backoff_slot = 0;
        set_backoff_limit(max_backoff_slot, min_backoff_slot);
    }
    set_socket();

    // Reset packet count
    m_packet_count = 0;
    m_sequence_number = 0;
    
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
    
    // Close debug file
    /*if (m_debug_file.is_open())
    {
        m_debug_file.close();
    }*/
    
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
    // Check if we have reached the maximum packet count
    if (m_packet_count < m_max_packet_count)
    {
        // Recreate hello packet with updated neighbor table
        create_hello_packet();
        send_packet();
        m_packet_count++;
        
        // Schedule next send with random jitter to reduce collisions
        Time interval = m_hello_interval + get_backoff_time();
        m_send_event = Simulator::Schedule(interval, &Hello_beacon_App::send_periodically, this);
    }
    else
    {
        // Reached max packet count, stop sending
        NS_LOG_DEBUG("Node " << GetNode()->GetId() << " finished sending " << m_packet_count << " hello messages");
    }
}

// TODO: add my own header in the hello-beacon packet
void
Hello_beacon_App::create_hello_packet()
{
    //每個節點都會執行下面的程式碼來創建自己的hello-beacon packet，然後發送給鄰居節點
    Ptr<Node> node = GetNode();
    uint32_t node_id = node->GetId();
    
    // Build packet content: NodeID SequenceNumber [NeighborID:DR,...] PositionX PositionY
    m_packet_content = std::to_string(node_id) + " " + std::to_string(m_sequence_number) + " ";
    
    // Serialize neighbor table with delivery ratio
    std::string neighbor_table_str;
    if (!m_neighbor_set.empty()) {
        // Sort neighbors for consistent ordering
        std::vector<uint32_t> sorted_neighbors(m_neighbor_set.begin(), m_neighbor_set.end());
        std::sort(sorted_neighbors.begin(), sorted_neighbors.end());
        
        for (size_t i = 0; i < sorted_neighbors.size(); ++i) {
            uint32_t neighbor_id = sorted_neighbors[i];
            double dr = CalculateDeliveryRatio(neighbor_id);
            
            neighbor_table_str += std::to_string(neighbor_id) + ":";
            // Format DR to 2 decimal places
            char dr_str[10];
            snprintf(dr_str, sizeof(dr_str), "%.2f", dr);
            neighbor_table_str += dr_str;
            
            if (i < sorted_neighbors.size() - 1) {
                neighbor_table_str += ",";
            }
        }
    } else {
        neighbor_table_str = "none";
    }
    
    m_packet_content += neighbor_table_str + " ";
    
    // Add position information (optional, for distance calculation)
    float x_pos = node->GetObject<MobilityModel>()->GetPosition().x;
    float y_pos = node->GetObject<MobilityModel>()->GetPosition().y;
    m_node_position = Vector2D(x_pos, y_pos);
    auto truncate_pos = [](const float position, const int precision = 1) {
        std::string trimmed_str =
            std::to_string(position).substr(0, std::to_string(position).find(".") + precision + 1);
        return trimmed_str;
    };
    m_packet_content += truncate_pos(x_pos) + " " + truncate_pos(y_pos);
    //std::cout<<m_packet_content<<std::endl;
    
    // check if packet_content size is too large
    const int max_data_size = 65507; // ns3::MAX_IPV4_UDP_DATAGRAM_SIZE
    NS_ASSERT_MSG(m_packet_content.length() <= max_data_size, "packet_content too large!\n");
    
    // Increment sequence number for next packet
    m_sequence_number++;
}

double
Hello_beacon_App::CalculateDeliveryRatio(uint32_t neighbor_id)
{
    //.end() returns an iterator to the element following the last element,
    // so if find() returns .end(), it means the key was not found in the map.
    if (m_neighbor_info.find(neighbor_id) == m_neighbor_info.end()) {
        return 0.0;
    }
    
    uint32_t received_count = m_neighbor_info[neighbor_id].size();
    
    if (m_max_packet_count == 0) {
        return 0.0;
    }
    
    double dr = static_cast<double>(received_count) / static_cast<double>(m_max_packet_count);
    return (dr > 1.0) ? 1.0 : dr; // Cap at 1.0
}


double
Hello_beacon_App::get_df(uint32_t neighbor_id)
{
    auto it = m_neighbor_df.find(neighbor_id);
    return (it == m_neighbor_df.end()) ? 0.0 : it->second;
}


double
Hello_beacon_App::get_etx(uint32_t neighbor_id)
{
    double dr = CalculateDeliveryRatio(neighbor_id);
    double df = get_df(neighbor_id);
    if (dr <= 0.0 || df <= 0.0) {
        // return a large value to indicate an undefined/poor link
        return std::numeric_limits<double>::infinity();
    }
    return 1.0 / (dr * df);
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
        
        // Debug output for sending
        /*if (m_debug_file.is_open())
        {
            m_debug_file << "Node " << GetNode()->GetId() << " sent hello packet seq_num=" << m_sequence_number 
                         << ", packet_count=" << m_packet_count << std::endl;
        }*/
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
        std::vector<uint8_t> data(receivedSize);
        packet->CopyData(data.data(), receivedSize);
        std::string packet_str(reinterpret_cast<const char*>(data.data()), receivedSize);
        //std::cout << packet_str << std::endl; // (nodeID seq_num neighbor_table pos_x pos_y)

        std::stringstream ss;
        ss << packet_str;
        uint32_t sender_node_id;
        uint32_t sender_seq_num;
        std::string neighbor_table_str;
        float x_pos, y_pos;
        
        ss >> sender_node_id >> sender_seq_num >> neighbor_table_str >> x_pos >> y_pos;
        
        // Update neighbor set with the sender (direct neighbor)
        if (m_neighbor_set.count(sender_node_id) == 0)
        {
            m_neighbor_set.insert(sender_node_id);
            m_distance_vec.emplace_back(CalculateDistance(m_node_position, Vector2D(x_pos, y_pos)),
                                        sender_node_id);
        }
        
        // Record received sequence number for delivery ratio calculation
        m_neighbor_info[sender_node_id].insert(sender_seq_num);

        // Parse the neighbour table string; each entry looks like "id:dr" and
        // entries are comma-separated.  When the entry corresponds to *this*
        // node we interpret the advertised delivery ratio as our DF as seen by
        // the sender.
        if (neighbor_table_str != "none")
        {
            std::stringstream nt_ss(neighbor_table_str);
            std::string entry;
            while (std::getline(nt_ss, entry, ','))
            {
                uint32_t id = 0;
                double dr = 0.0;
                char colon = 0;
                std::stringstream entry_ss(entry);
                entry_ss >> id >> colon >> dr; // will read e.g. "5:0.80"
                if (id == GetNode()->GetId())
                {
                    m_neighbor_df[sender_node_id] = dr;
                }
                /*if (m_debug_file.is_open())
                {
                    m_debug_file << "Node " << GetNode()->GetId()
                                 << " parsed neighbor-table entry " << entry
                                 << " from node " << sender_node_id << std::endl;
                }*/
            }
        }

        // Write to debug file instead of stdout
        /*if (m_debug_file.is_open())
        {
            m_debug_file << "DEBUG: Node " << GetNode()->GetId() << " received msg from node " 
                         << sender_node_id << " with seq_num=" << sender_seq_num 
                         << ", received_count=" << m_neighbor_info[sender_node_id].size()
                         << ", expected_count=" << m_max_packet_count;
            if (m_neighbor_df.find(sender_node_id) != m_neighbor_df.end())
            {
                m_debug_file << ", df=" << m_neighbor_df[sender_node_id];
                double etx = get_etx(sender_node_id);
                m_debug_file << ", etx=" << etx;
            }
            m_debug_file << std::endl;
        }*/
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