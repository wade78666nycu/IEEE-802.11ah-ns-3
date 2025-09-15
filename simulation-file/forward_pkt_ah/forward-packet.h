#pragma once

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/log.h"
#include "ns3/network-module.h"

#include <cassert>
#include <sstream>
#include <string>
#include <unordered_map>
#include <mutex>

struct next_hop_key
{
    unsigned int dst;
    std::string our_addr;

    next_hop_key()
    {
        dst = 0;
        our_addr = "";
    }

    next_hop_key(unsigned int dst_num, std::string addr)
    {
        dst = dst_num;
        our_addr = addr;
    }

    bool operator==(const next_hop_key& other_key) const
    {
        return dst == other_key.dst && our_addr == other_key.our_addr;
    }
};

// custom specialization of std::hash injected in namespace std
template <>
struct std::hash<next_hop_key>
{
    std::size_t operator()(const next_hop_key& key) const noexcept
    {
        std::size_t h1 = std::hash<unsigned int>{}(key.dst);
        std::size_t h2 = std::hash<std::string>{}(key.our_addr);
        return h1 ^ (h2 << 1);
    }
};

namespace ns3
{
class Application;

class ForwardPktApp : public Application
{
  public:
    ForwardPktApp();
    ~ForwardPktApp() override;

    /**
     * Register this type.
     * \return The TypeId.
     */
    static TypeId GetTypeId();

    /**
     * Setup route arguments.
     * \param packet_size The packet size to transmit.
     * \param send_packet_num The number of packets to transmit.
     * \param data_rate the data rate to use.
     */
    void Setup(std::unordered_map<next_hop_key, std::string> hop_map,
               unsigned int src_port_num,
               unsigned int dst_port_num);

    void Set_send(uint32_t packet_size, uint32_t send_packet_num, DataRate data_rate);

    // Set maximum packets that can be sent per round.
    // If send_packet_num is larger max_packet_num_per_round,
    // the extra packets will be sent after some intervals.
    void Set_max_packet_num_per_round(const unsigned int max_num);

    void StartApplication() override;
    void StopApplication() override;

    uint32_t get_pkt_sent();
    uint32_t get_pkt_recv();

    std::string packet_msg;
    unsigned int max_packet_num_per_round;

    // map<our_address, next_hop_address>
    // map cannot use Ipv4Address as key, so use string instead
    std::unordered_map<next_hop_key, std::string> next_hop_map;
    bool is_source;
    bool is_destination;
    bool print_data;
    unsigned int m_send_buffer_max;

    // start_key if the node is source
    next_hop_key start_key;
    Time start_send_pkt_time;

  private:
    void SetupSocket();
    // Schedule a new transmission.
    void ScheduleTx(const next_hop_key key);
    // Send a packet.
    void SendPacket(const next_hop_key key);
    void gen_packet(const next_hop_key key);
    void handle_recv_packet(Ptr<Socket> socket);

    std::vector<Ptr<Socket>> m_tx_socket_vec; // The transmission sockets.
    std::vector<Ptr<Socket>> m_rx_socket_vec; // The receive sockets.
    InetSocketAddress m_recv_sink;
    unsigned int m_src_port;    // socket port number used at source
    unsigned int m_dst_port;    // socket port number used at destination
    uint32_t m_packet_size;     // The packet size.
    uint32_t m_send_packet_num;
    DataRate m_data_rate;       // The data rate to use.
    EventId m_send_event;       // Send event.
    bool m_running;             // True if the application is running.
    uint32_t m_packets_sent;    // The number of packets sent.
    uint32_t m_packets_recv;    // The number of packets received.

    std::mutex m_mutex;
    std::vector<Ptr<Packet>> m_send_buffer;
};

} // namespace ns3
