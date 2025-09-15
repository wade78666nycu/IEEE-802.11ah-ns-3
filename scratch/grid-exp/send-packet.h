#pragma once

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/log.h"
#include "ns3/network-module.h"

// included to get channel rssi
#include "ns3/double.h"
#include "ns3/wifi-tx-vector.h"

#include <string>

namespace ns3
{
class Application;

class Time;

class SendPacketApp : public Application
{
  public:
    SendPacketApp();
    ~SendPacketApp() override;

    /**
     * Register this type.
     * \return The TypeId.
     */
    static TypeId GetTypeId();

    /**
     * Setup socket arguments.
     * \param packet_size The packet size to transmit.
     * \param send_packet_num The number of packets to transmit.
     * \param data_rate the data rate to use.
     */
    void Setup(Ptr<Ipv4> dst_ipv4,
               unsigned int src_port_num,
               unsigned int dst_port_num,
               uint32_t packet_size,
               uint32_t send_packet_num,
               DataRate data_rate);

    // Set maximum packets that can be sent per round.
    // If send_packet_num is larger max_packet_num_per_round,
    // the extra packets will be sent after some intervals.
    void Set_max_packet_num_per_round(const unsigned int max_num);

    void StartApplication() override;
    void StopApplication() override;

    void set_phy_vec();

    std::string packet_msg;
    unsigned int max_packet_num_per_round;
    bool m_multi_channel;

  private:
    void SetupSocket();
    // Schedule a new transmission.
    void ScheduleTx();
    // Send a packet.
    void SendPacket();

    std::vector<Ptr<Socket>> m_socket_vec; // The transmission sockets.
    Ptr<Ipv4> m_dst_ipv4;                  // The destination ipv4. Used for getting Ipv4Address
    InetSocketAddress m_recv_sink;
    unsigned int m_src_port;    // socket port number used at source
    unsigned int m_dst_port;    // socket port number used at destination
    uint32_t m_packet_size;     // The packet size.
    uint32_t m_send_packet_num; // The number of packets to send.
    DataRate m_data_rate;       // The data rate to use.
    EventId m_send_event;       // Send event.
    bool m_running;             // True if the application is running.
    uint32_t m_packets_sent;    // The number of packets sent.

    std::vector<Ptr<YansWifiPhy>> m_phy_vec;
    int m_send_channel; // the channel used to send packet
};

} // namespace ns3
