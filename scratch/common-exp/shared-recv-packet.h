#pragma once

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/log.h"
#include "ns3/network-module.h"

#include <vector>

namespace ns3
{
class Application;

class RecvPacketApp : public Application
{
  public:
    RecvPacketApp() = delete;
    RecvPacketApp(const unsigned port_num);
    ~RecvPacketApp() override;

    static TypeId GetTypeId();

    void Setup();

    void StartApplication() override;
    void StopApplication() override;

    unsigned int get_total_recv_packet();
    uint64_t get_total_delay_ns() const;
    uint64_t get_max_delay_ns() const;
    uint64_t get_first_recv_ns() const;
    uint64_t get_last_recv_ns() const;

  private:
    void handle_recv_packet(Ptr<Socket> socket);

    std::vector<Ptr<Socket>> m_socket_vec;
    unsigned int port;
    bool m_running;
    unsigned int m_packets_recv;
    uint64_t m_total_delay_ns;
    uint64_t m_max_delay_ns;
    uint64_t m_first_recv_ns;
    uint64_t m_last_recv_ns;
};

} // namespace ns3
