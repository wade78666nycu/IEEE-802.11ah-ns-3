#include "hello-beacon.h"
#include <algorithm>
#include <limits>

using namespace ns3;
static void
OnHelloPhyTxTrace(Hello_beacon_App* app,
				  uint32_t ifIndex,
				  const Ptr<const Packet> packet,
				  uint16_t channelFreqMhz,
				  uint16_t channelNumber,
				  uint32_t rate,
				  bool isShortPreamble,
				  WifiTxVector txvector)
{
	(void)packet;
	(void)channelFreqMhz;
	(void)channelNumber;
	(void)rate;
	(void)isShortPreamble;

	// Keep only data-bearing traffic samples:
	// - MAC data frames
	// - unicast destination (exclude broadcast/multicast control/hello traffic)
	// - reasonably large payload (exclude most control packets)
	if (!packet)
	{
		return;
	}
	Ptr<Packet> copy = packet->Copy();
	WifiMacHeader wifiHdr;
	if (copy->PeekHeader(wifiHdr) == 0)
	{
		return;
	}
	if (!wifiHdr.IsData() || wifiHdr.GetAddr1().IsGroup() || packet->GetSize() < 200)
	{
		return;
	}

	app->update_phy_rate_sample(ifIndex, txvector);
}

NS_LOG_COMPONENT_DEFINE("HelloBeacon");

Hello_beacon_App::Hello_beacon_App()
	    : enable_print_neighbor(false),
	      run_interval(0),
	      wait_interval(0),
	      m_running(false),
	      socket_type_id(TypeId::LookupByName("ns3::UdpSocketFactory")),
	      m_packetSize(128),
	      m_port(80),
	      m_hello_interval(MilliSeconds(100.0)),
	      m_backoff_slot_time(MicroSeconds(100.0)),
	      m_packet_content(""),
	      m_data_rate("3MB/s"),
	      m_packet_count(0),
	      m_max_packet_count(1)
{
    m_uv = CreateObject<UniformRandomVariable>();
}

Hello_beacon_App::Hello_beacon_App(const unsigned int num_nodes)
    : Hello_beacon_App()
{
    m_neighbor_set.reserve(num_nodes);
    m_distance_vec.reserve(num_nodes);
}

Hello_beacon_App::~Hello_beacon_App()
{
	    for (auto& s : m_tx_sockets)
	    {
	        s = nullptr;
	    }
	    m_tx_destinations.clear();
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
    m_uv->SetAttribute("Min", DoubleValue(min_slot));
    m_uv->SetAttribute("Max", DoubleValue(max_slot));
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
	    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
	    NS_ASSERT(ipv4);

	    m_tx_sockets.clear();
	    m_tx_ifIndices.clear();
	    m_tx_destinations.clear();
	    m_rx_socket = nullptr;

	    // RX socket (single). Enable packet info so we can read which interface the packet arrived on.
	    m_rx_socket = Socket::CreateSocket(node, socket_type_id);
	    m_rx_socket->SetAllowBroadcast(true);
	    m_rx_socket->SetRecvPktInfo(true);
	    InetSocketAddress rx_local = InetSocketAddress(Ipv4Address::GetAny(), m_port);
	    if (m_rx_socket->Bind(rx_local) == -1)
	    {
	        NS_FATAL_ERROR("Failed to bind rx socket");
	    }
	    m_rx_socket->SetRecvCallback(MakeCallback(&Hello_beacon_App::handle_recv_packet, this));

	    const uint32_t nIf = ipv4->GetNInterfaces();
	    // Start from 1 since index 0 is loopback.
	    for (uint32_t ifIndex = 1; ifIndex < nIf; ++ifIndex)
	    {
	        if (!ipv4->IsUp(ifIndex) || ipv4->GetNAddresses(ifIndex) == 0)
	        {
	            continue;
	        }

	        Ptr<NetDevice> dev = ipv4->GetNetDevice(ifIndex);
	        if (!dev)
	        {
	            continue;
	        }

	        if (ifIndex >= m_phy_trace_connected_by_if.size())
	        {
	            m_phy_trace_connected_by_if.resize(ifIndex + 1, false);
	        }
	        if (!m_phy_trace_connected_by_if[ifIndex])
	        {
	            Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(dev);
	            if (wifiDev)
	            {
	                Ptr<WifiPhy> phy = wifiDev->GetPhy();
	                if (phy)
	                {
	                    phy->TraceConnectWithoutContext(
	                        "MonitorSnifferTx",
	                        MakeBoundCallback(&OnHelloPhyTxTrace, this, ifIndex));
	                    m_phy_trace_connected_by_if[ifIndex] = true;
	                }
	            }
	        }

	        // TX socket bound to this interface/device.
	        Ptr<Socket> tx = Socket::CreateSocket(node, socket_type_id);
	        tx->SetAllowBroadcast(true);
	        // Use limited broadcast to avoid consulting the routing protocol.
	        // (Subnet-directed broadcast would trigger RouteOutput(), which can crash under DSR.)
	        Ipv4Address destination = Ipv4Address("255.255.255.255");
	        // Bind to device to avoid routing-protocol interactions when sending broadcast.
	        tx->BindToNetDevice(dev);
	        m_tx_sockets.emplace_back(tx);
	        m_tx_ifIndices.emplace_back(ifIndex);
	        m_tx_destinations.emplace_back(destination);
	    }

	    const uint32_t maxIfIndex = nIf > 0 ? (nIf - 1) : 0;
	    m_sequence_number_by_if.assign(maxIfIndex + 1, 0);
	    if (!m_hasRunOnce)
	    {
	        // First cycle: clear all neighbour data.
	        m_neighbor_set_by_if.assign(maxIfIndex + 1, {});
	        m_neighbor_info_by_if.assign(maxIfIndex + 1, {});
	        m_neighbor_df_by_if.assign(maxIfIndex + 1, {});
	        m_last_phy_rate_bps_by_if.assign(maxIfIndex + 1,
	                                        static_cast<double>(m_data_rate.GetBitRate()));
	    }
	    else
	    {
	        // Subsequent cycles: keep existing neighbour data, only clear
	        // per-sequence tracking so delivery ratio is computed from new
	        // packets only (avoids mixing stale pre-GradPC measurements).
	        for (auto& info : m_neighbor_info_by_if)
	        {
	            info.clear();
	        }
	        // Resize vectors if needed (should already be correct).
	        if (m_neighbor_set_by_if.size() < maxIfIndex + 1)
	            m_neighbor_set_by_if.resize(maxIfIndex + 1);
	        if (m_neighbor_info_by_if.size() < maxIfIndex + 1)
	            m_neighbor_info_by_if.resize(maxIfIndex + 1);
	        if (m_neighbor_df_by_if.size() < maxIfIndex + 1)
	            m_neighbor_df_by_if.resize(maxIfIndex + 1);
	        if (m_last_phy_rate_bps_by_if.size() < maxIfIndex + 1)
	            m_last_phy_rate_bps_by_if.resize(maxIfIndex + 1,
	                                             static_cast<double>(m_data_rate.GetBitRate()));
	    }
	    if (m_phy_trace_connected_by_if.size() < maxIfIndex + 1)
	    {
	        m_phy_trace_connected_by_if.resize(maxIfIndex + 1, false);
	    }
}

void
Hello_beacon_App::update_phy_rate_sample(uint32_t ifIndex, const WifiTxVector& txvector)
{
	if (ifIndex == 0)
	{
		return;
	}
	if (ifIndex >= m_last_phy_rate_bps_by_if.size())
	{
		m_last_phy_rate_bps_by_if.resize(ifIndex + 1,
		                                 static_cast<double>(m_data_rate.GetBitRate()));
	}
	const double bps = static_cast<double>(txvector.GetMode().GetDataRate());
	if (bps > 0.0)
	{
		m_last_phy_rate_bps_by_if[ifIndex] = bps;
	}
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
Hello_beacon_App::get_delivery_ratio(uint32_t neighbor_id, uint32_t ifIndex)
{
	    return CalculateDeliveryRatio(neighbor_id, ifIndex);
}

Time
Hello_beacon_App::get_backoff_time()
{
    return m_backoff_slot_time * m_uv->GetInteger();
}

void
Hello_beacon_App::StartApplication()
{
	// Initialize local position before any receive callback may compute distance.
	Ptr<Node> node = GetNode();
	if (node && node->GetObject<MobilityModel>())
	{
		const Vector pos = node->GetObject<MobilityModel>()->GetPosition();
		m_node_position = Vector2D(pos.x, pos.y);
	}

    if (m_uv->GetMin() < 0.0 || m_uv->GetMax() * m_backoff_slot_time > m_hello_interval)
    {
        // if min/max values are not valid, set to default values
        const double max_backoff_slot = 32;
        const double min_backoff_slot = 0;
        set_backoff_limit(max_backoff_slot, min_backoff_slot);
	    }
    set_socket();

	    // Reset packet count
	    m_packet_count = 0;
	    std::fill(m_sequence_number_by_if.begin(), m_sequence_number_by_if.end(), 0);
    
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
    if (!m_running)
        return; // already stopped; prevent double-scheduling a restart
    //NS_LOG_DEBUG("Hello beacon app stop");
    m_running = false;
    m_hasRunOnce = true;
    if (m_send_event.IsRunning())
    {
        Simulator::Cancel(m_send_event);
    }

    for (auto& s : m_tx_sockets)
    {
        if (s)
        {
            s->Close();
            s = nullptr;
        }
    }
    m_tx_sockets.clear();
    m_tx_ifIndices.clear();
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
		    // Check if we have reached the maximum packet count
		    if (m_packet_count < m_max_packet_count)
		    {
		        // Send one hello packet per interface/NIC per round.
		        for (size_t idx = 0; idx < m_tx_sockets.size(); ++idx)
		        {
		            const uint32_t ifIndex = m_tx_ifIndices.at(idx);
		            create_hello_packet(ifIndex);
		            send_packet(m_tx_sockets.at(idx), m_tx_destinations.at(idx));
		        }
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
Hello_beacon_App::create_hello_packet(uint32_t ifIndex)
{
	    //每個節點都會執行下面的程式碼來創建自己的hello-beacon packet，然後發送給鄰居節點
	    Ptr<Node> node = GetNode();
	    uint32_t node_id = node->GetId();
	    
	    // Build packet content:
	    // NodeID SequenceNumber IfIndex [NeighborID:DR,...] PositionX PositionY
	    if (ifIndex >= m_sequence_number_by_if.size())
	    {
	        NS_FATAL_ERROR("Invalid interface index " << ifIndex);
	    }
	    uint32_t seq = m_sequence_number_by_if[ifIndex];
	    m_packet_content =
	        std::to_string(node_id) + " " + std::to_string(seq) + " " + std::to_string(ifIndex) + " ";
	    
	    // Serialize neighbor table with delivery ratio
	    std::string neighbor_table_str;
	    if (ifIndex < m_neighbor_set_by_if.size() && !m_neighbor_set_by_if[ifIndex].empty()) {
	        // Sort neighbors for consistent ordering
	        std::vector<uint32_t> sorted_neighbors(m_neighbor_set_by_if[ifIndex].begin(),
	                                              m_neighbor_set_by_if[ifIndex].end());
	        std::sort(sorted_neighbors.begin(), sorted_neighbors.end());
	        
	        for (size_t i = 0; i < sorted_neighbors.size(); ++i) {
	            uint32_t neighbor_id = sorted_neighbors[i];
	            double dr = CalculateDeliveryRatio(neighbor_id, ifIndex);
	            
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
	    m_sequence_number_by_if[ifIndex]++;
}

double
Hello_beacon_App::CalculateDeliveryRatio(uint32_t neighbor_id, uint32_t ifIndex)
{
	    //.end() returns an iterator to the element following the last element,
	    // so if find() returns .end(), it means the key was not found in the map.
	    if (ifIndex >= m_neighbor_info_by_if.size())
	    {
	        return 0.0;
	    }
	    auto& info = m_neighbor_info_by_if[ifIndex];
	    if (info.find(neighbor_id) == info.end()) {
	        return 0.0;
	    }

	    const auto& seqSet = info[neighbor_id];
	    if (seqSet.empty()) {
	        return 0.0;
	    }

	    const uint32_t received_count = seqSet.size();
	    // Sequence numbers start from 0 each cycle, so maxSeq + 1 approximates
	    // the number of packets the neighbor has actually sent in this round.
	    const uint32_t sent_count_est = (*seqSet.rbegin()) + 1;
	    if (sent_count_est == 0) {
	        return 0.0;
	    }

    double dr = static_cast<double>(received_count) / static_cast<double>(sent_count_est);
    return (dr > 1.0) ? 1.0 : dr; // Cap at 1.0
}

double
Hello_beacon_App::get_df(uint32_t neighbor_id, uint32_t ifIndex)
{
	    if (ifIndex >= m_neighbor_df_by_if.size())
	    {
	        return 0.0;
	    }
	    auto& dfMap = m_neighbor_df_by_if[ifIndex];
	    auto it = dfMap.find(neighbor_id);
	    return (it == dfMap.end()) ? 0.0 : it->second;
}

double
Hello_beacon_App::get_etx(uint32_t neighbor_id, uint32_t ifIndex)
{
	    double dr = CalculateDeliveryRatio(neighbor_id, ifIndex);
	    double df = get_df(neighbor_id, ifIndex);
	    if (dr <= 0.0 || df <= 0.0) {
	        return std::numeric_limits<double>::infinity();
	    }
	    return 1.0 / (dr * df);
}

double
Hello_beacon_App::get_ett(uint32_t neighbor_id, uint32_t ifIndex)
{
    double etx = get_etx(neighbor_id, ifIndex);
    if (std::isinf(etx)) {
        return std::numeric_limits<double>::infinity();
    }
    double packet_size_bits = static_cast<double>(m_packetSize) * 8.0;
	double bandwidth_bps = static_cast<double>(m_data_rate.GetBitRate());
	if (ifIndex < m_last_phy_rate_bps_by_if.size() && m_last_phy_rate_bps_by_if[ifIndex] > 0.0)
	{
		bandwidth_bps = m_last_phy_rate_bps_by_if[ifIndex];
	}
    return etx * packet_size_bits / bandwidth_bps * 1000.0; // Convert to milliseconds
}

void
Hello_beacon_App::send_packet(Ptr<Socket> txSocket, Ipv4Address destination)
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
	        txSocket->SendTo(packet, 0, InetSocketAddress(destination, m_port));
    }
}

// TODO: check if received packet is a hello-beacon packet from header
void
Hello_beacon_App::handle_recv_packet(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    uint32_t rxIfIndex = 0;
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
	        uint32_t sender_if_index = 0;
	        std::string neighbor_table_str;
	        float x_pos, y_pos;
	        
	        ss >> sender_node_id >> sender_seq_num >> sender_if_index >> neighbor_table_str >> x_pos >> y_pos;
	        // Prefer the sender-advertised interface index. In this experiment setup,
	        // each interface maps to one "channel" (separate NetDevice + subnet).
	        // PacketInfoTag recvIf values are not guaranteed to align with our indexing
	        // expectations across configurations.
	        const uint32_t linkIfIndex = (sender_if_index != 0) ? sender_if_index : rxIfIndex;
	        if (linkIfIndex == 0 || linkIfIndex >= m_neighbor_info_by_if.size())
	        {
	            // Unknown interface; skip accounting to avoid OOB.
	            continue;
	        }
	        
	        // Update neighbor set with the sender (direct neighbor)
	        if (m_neighbor_set.count(sender_node_id) == 0)
	        {
	            m_neighbor_set.insert(sender_node_id);
	            Vector2D local_pos = m_node_position;
	            Ptr<Node> local_node = GetNode();
	            if (local_node && local_node->GetObject<MobilityModel>())
	            {
	                const Vector pos = local_node->GetObject<MobilityModel>()->GetPosition();
	                local_pos = Vector2D(pos.x, pos.y);
	                m_node_position = local_pos;
	            }
            m_distance_vec.emplace_back(CalculateDistance(local_pos, Vector2D(x_pos, y_pos)),
                                        sender_node_id);
	        }
	        m_neighbor_set_by_if[linkIfIndex].insert(sender_node_id);
	        
	        // Record received sequence number for delivery ratio calculation
	        m_neighbor_info_by_if[linkIfIndex][sender_node_id].insert(sender_seq_num);

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
	                    m_neighbor_df_by_if[linkIfIndex][sender_node_id] = dr;
	                }
	        }
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
