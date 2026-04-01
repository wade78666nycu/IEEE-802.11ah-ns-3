#pragma once

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/log.h"
#include "ns3/network-module.h"

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

    static TypeId GetTypeId();

    void Setup(Ptr<Ipv4> dst_ipv4,
               unsigned int src_port_num,
               unsigned int dst_port_num,
               uint32_t packet_size,
               uint32_t send_packet_num,
               DataRate data_rate);

    void Set_max_packet_num_per_round(const unsigned int max_num);
    // Set tolerance multiplier for ETT-based channel selection (default 1.2).
    // A channel with higher index (lower power) is preferred when its ETT is
    // no worse than channel-1 ETT * tolerance.
    void set_ett_tolerance(double tolerance);

    void StartApplication() override;
    void StopApplication() override;

    std::string packet_msg;
    unsigned int max_packet_num_per_round;
    bool m_multi_channel;

  private:
    void SetupSocket();
    void ScheduleTx();
    void SendPacket();

    Ptr<Socket> m_socket;
    Ptr<Ipv4> m_dst_ipv4;
    InetSocketAddress m_recv_sink;
    unsigned int m_src_port;
    unsigned int m_dst_port;
    uint32_t m_packet_size;
    uint32_t m_send_packet_num;
    DataRate m_data_rate;
    EventId m_send_event;
    bool m_running;
    uint32_t m_packets_sent;

};

} // namespace ns3
