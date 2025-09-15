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

    /**
     * Register this type.
     * \return The TypeId.
     */
    static TypeId GetTypeId();

    void Setup();

    void StartApplication() override;
    void StopApplication() override;

    unsigned int get_total_recv_packet();

  private:
    void handle_recv_packet(Ptr<Socket> socket);

    std::vector<Ptr<Socket>> m_socket_vec; // The receive sockets
    unsigned int port;                     // socket port number used
    bool m_running;                        // True if the application is running.
    unsigned int m_packets_recv;
};

} // namespace ns3
