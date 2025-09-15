#pragma once

#include "packet-info-tag.h"

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
	void set_count_delay();
	void unset_count_delay();

	void StartApplication() override;
	void StopApplication() override;

	unsigned int get_total_recv_packet();
	unsigned int get_src_total_recv_packet(const unsigned int src_node_id);
	float get_total_delay_time(const unsigned int src_node_id);
	float get_avg_delay_time(const unsigned int src_node_id);

  private:
	void handle_recv_packet(Ptr<Socket> socket);

	std::vector<Ptr<Socket>> m_socket_vec; // The receive sockets
	unsigned int port;					   // socket port number used
	bool m_running;						   // True if the application is running.
	bool m_count_delay;
	std::unordered_map<unsigned int, std::unordered_map<unsigned int, float>>
		packet_delay; // src_id -> packet_id -> delay time
	unsigned int m_packets_recv;
};

} // namespace ns3
