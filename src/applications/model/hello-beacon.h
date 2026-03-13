#pragma once

#include "ns3/application.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/vector.h"
//#include "ns3/wifi-module.h"

#include <chrono>
#include <iomanip>
#include <random>
#include <unordered_set>
#include <vector>
#include <map>
#include <set>
#include <limits>

namespace ns3
{
class Application;

	class Hello_beacon_App : public Application
	{
	  public:
	    Hello_beacon_App();
	    Hello_beacon_App(const unsigned int num_nodes);
	    ~Hello_beacon_App() override;

		/**
		 * Register this type.
		 * \return The TypeId.
		 */
		static TypeId GetTypeId();
		void print_neighbor_set();
		void set_data_rate(const DataRate data_rate);
		void set_hello_interval(const Time hello_interval);
		void set_port(const uint16_t port);
		void set_backoff_limit(const unsigned int max_backoff_slot,
							const unsigned int min_backoff_slot);
		// set the time for one backoff slot
		void set_backoff_slot_time(const Time backoff_slot_time);
	    std::unordered_set<uint32_t> get_neighbor_set();
	    std::vector<std::pair<float, uint32_t>> get_distance_vec();
	    double get_delivery_ratio(uint32_t neighbor_id, uint32_t ifIndex);
	    double get_df(uint32_t neighbor_id, uint32_t ifIndex);
	    double get_etx(uint32_t neighbor_id, uint32_t ifIndex);
	    void set_max_packet_count(const uint32_t max_count);

	    bool enable_print_neighbor;
	    Time run_interval; // interval of how long Hello_beacon_App runs per cycle
    	Time wait_interval; // interval to wait until next cycle starts

  	private:
		void StartApplication() override;
		void StopApplication() override;

		void set_socket();
		Time get_backoff_time();

		void create_hello_packet(uint32_t ifIndex);
		void send_packet(Ptr<Socket> txSocket, Ipv4Address destination);
		void send_periodically();
		void handle_recv_packet(Ptr<Socket> socket);
		double CalculateDeliveryRatio(uint32_t neighbor_id, uint32_t ifIndex);

		bool m_running;        // True if the application is running.
		TypeId socket_type_id; // default socket type is udp socket
		std::vector<Ptr<Socket>> m_tx_sockets;
		std::vector<uint32_t> m_tx_ifIndices;
		std::vector<Ipv4Address> m_tx_destinations;
		Ptr<Socket> m_rx_socket;
		uint32_t m_packetSize; // The default packet size is 128 [bytes]
		uint16_t m_port;       // default port udp uses is 80
		uint32_t m_max_packet_count; // Maximum packets to send (default 1)

		Time m_hello_interval;    // default is 100 milli-seconds
		Time m_backoff_slot_time; // default is 100 micro-seconds
		std::string m_packet_content;

		DataRate m_data_rate; // The datarate to use.
		EventId m_send_event; // Send event.

		Vector2D m_node_position;

		// m_neighbor_set: unordered_set<neighbor_node_id>
		std::unordered_set<uint32_t> m_neighbor_set;

		// m_distance_vec: vector<std::pair<distance, node_id>>
		// placing the distance as the first item to sort conveniently
		std::vector<std::pair<float, uint32_t>> m_distance_vec;

	    // Sequence number per interface index (Ipv4 interface index).
	    // Index 0 is reserved for loopback.
	    std::vector<uint32_t> m_sequence_number_by_if;
	    uint32_t m_packet_count; // Current packet count sent

	    // Per-interface neighbor sets and delivery stats.
	    // Index 0 is reserved for loopback.
	    std::vector<std::unordered_set<uint32_t>> m_neighbor_set_by_if;
	    // 記錄每個鄰居的序列號<neighbor_id, received_seq_nums> per interface index
	    std::vector<std::map<uint32_t, std::set<uint32_t>>> m_neighbor_info_by_if;

	    // Values reported by neighbours for this node, per interface index.
	    // Keyed by neighbour id, the value is the delivery ratio that the neighbour computed for
	    // packets *from* this node (i.e. our DF as seen by them).
	    std::vector<std::map<uint32_t, double>> m_neighbor_df_by_if;
	};
} // namespace ns3
