/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 IITP RAS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Based on
 *      NS-2 AODV model developed by the CMU/MONARCH group and optimized and
 *      tuned by Samir Das and Mahesh Marina, University of Cincinnati;
 *
 *      AODV-UU implementation by Erik Nordström of Uppsala University
 *      http://core.it.uu.se/core/index.php/AODV-UU
 *
 * Authors: Elena Buchatskaia <borovkovaes@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */
#ifndef AODVROUTINGPROTOCOL_H
#define AODVROUTINGPROTOCOL_H

#include "aodv-dpd.h"
#include "aodv-neighbor.h"
#include "aodv-packet.h"
#include "aodv-rqueue.h"
#include "aodv-rtable.h"

#include "ns3/ipv4-interface.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/node.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/random-variable-stream.h"

#include "ns3/grad-pc.h"
#include "ns3/event-id.h"
#include "ns3/mac48-address.h"
#include "ns3/wifi-tx-vector.h"
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

namespace ns3
{
namespace aodv
{

/// Per-(neighbor, channel) transmission counters for data-phase ETT calculation.
struct DataPhaseCounters
{
	uint32_t txAttempts{0};
	uint32_t txSuccess{0};
};

/// Key type for per-(neighbor MAC, channel) counters.
typedef std::pair<Mac48Address, uint16_t> NeighborChannelKey;

/// Comparator so NeighborChannelKey can be used in std::map.
struct NeighborChannelKeyCmp
{
	bool operator()(const NeighborChannelKey& a, const NeighborChannelKey& b) const
	{
		if (a.first < b.first) return true;
		if (b.first < a.first) return false;
		return a.second < b.second;
	}
};

/**
 * \ingroup aodv
 *
 * \brief AODV routing protocol
 */
class RoutingProtocol : public Ipv4RoutingProtocol
{
  public:
	static TypeId GetTypeId(void);
	static const uint32_t AODV_PORT;

	/// c-tor
	RoutingProtocol();
	virtual ~RoutingProtocol();
	virtual void DoDispose();

	// Inherited from Ipv4RoutingProtocol
	Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p,
							   const Ipv4Header& header,
							   Ptr<NetDevice> oif,
							   Socket::SocketErrno& sockerr);
	bool RouteInput(Ptr<const Packet> p,
					const Ipv4Header& header,
					Ptr<const NetDevice> idev,
					UnicastForwardCallback ucb,
					MulticastForwardCallback mcb,
					LocalDeliverCallback lcb,
					ErrorCallback ecb);
	virtual void NotifyInterfaceUp(uint32_t interface);
	virtual void NotifyInterfaceDown(uint32_t interface);
	virtual void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address);
	virtual void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address);
	virtual void SetIpv4(Ptr<Ipv4> ipv4);
	virtual void PrintRoutingTable(Ptr<OutputStreamWrapper> stream) const;

	// Handle protocol parameters
	Time GetMaxQueueTime() const
	{
		return MaxQueueTime;
	}

	void SetMaxQueueTime(Time t);

	uint32_t GetMaxQueueLen() const
	{
		return MaxQueueLen;
	}

	void SetMaxQueueLen(uint32_t len);

	bool GetDesinationOnlyFlag() const
	{
		return DestinationOnly;
	}

	void SetDesinationOnlyFlag(bool f)
	{
		DestinationOnly = f;
	}

	bool GetGratuitousReplyFlag() const
	{
		return GratuitousReply;
	}

	void SetGratuitousReplyFlag(bool f)
	{
		GratuitousReply = f;
	}

	void SetHelloEnable(bool f)
	{
		EnableHello = f;
	}

	bool GetHelloEnable() const
	{
		return EnableHello;
	}

	void SetBroadcastEnable(bool f)
	{
		EnableBroadcast = f;
	}

	bool GetBroadcastEnable() const
	{
		return EnableBroadcast;
	}

	/// wraper to print routing table to standard output
	void print_routing_table();

	// ---- Data-phase ETT trace callbacks (connected externally) ----

	/// Called on every PHY TX begin (includes MAC retransmissions).
	/// Section 1: increments per-(neighbor,channel) txAttempts; triggers channel switch at >= 5.
	void DataPhasePhyTxBegin(std::string context, Ptr<const Packet> packet);

	/// Called on every PHY RX end.
	/// Section 2: if ACK for us, compute ETX/ETT via EWMA and update data-phase ETT table.
	void DataPhasePhyRxEnd(std::string context, Ptr<const Packet> packet);

	/// Called when MAC gives up after max retransmissions.
	/// Section 3: set ETX=10 penalty, update ETT, clear blacklist for neighbor, send RERR.
	void DataPhaseMacTxFinalFailed(std::string context, Mac48Address address);

	/// Get the data-phase ETT for a given neighbor/channel.  Returns INF if not yet measured.
	double GetDataPhaseEtt(uint32_t neighborId, uint32_t ifIndex) const;

	/// Return true if data-phase ETT measurement is active (i.e. hello phase is over).
	bool IsDataPhaseActive() const { return m_dataPhaseActive; }

	/// Activate data-phase ETT measurement (call when data transfer starts).
	void ActivateDataPhase();

	/**
	 * Assign a fixed random variable stream number to the random variables
	 * used by this model.  Return the number of streams (possibly zero) that
	 * have been assigned.
	 *
	 * \param stream first stream index to use
	 * \return the number of stream indices assigned by this model
	 */
	int64_t AssignStreams(int64_t stream);

  protected:
	virtual void DoInitialize(void);

  private:
	Ptr<Node> m_node;
	std::unordered_map<std::string, uint64_t>
		ip_to_id_map; // Ipv4Address to node id map, Ipv4Address needs to converted to string first
    int gradpc_app_idx;

	// Protocol parameters.
	uint32_t RreqRetries;	 ///< Maximum number of retransmissions of RREQ with TTL = NetDiameter to discover a route
	uint16_t RreqRateLimit;	 ///< Maximum number of RREQ per second.
	uint16_t RerrRateLimit;	 ///< Maximum number of REER per second.
	Time ActiveRouteTimeout; ///< Period of time during which the route is considered to be valid.
	uint32_t
		NetDiameter; ///< Net diameter measures the maximum possible number of hops between two nodes in the network
	/**
	 *  NodeTraversalTime is a conservative estimate of the average one hop traversal time for packets
	 *  and should include queuing delays, interrupt processing times and transfer times.
	 */
	Time NodeTraversalTime;
	Time NetTraversalTime;	///< Estimate of the average net traversal time.
	Time PathDiscoveryTime; ///< Estimate of maximum time needed to find route in network.
	Time MyRouteTimeout;	///< Value of lifetime field in RREP generating by this node.
	/**
	 * Every HelloInterval the node checks whether it has sent a broadcast  within the last HelloInterval.
	 * If it has not, it MAY broadcast a  Hello message
	 */
	Time HelloInterval;
	uint32_t AllowedHelloLoss; ///< Number of hello messages which may be loss for valid link
	/**
	 * DeletePeriod is intended to provide an upper bound on the time for which an upstream node A
	 * can have a neighbor B as an active next hop for destination D, while B has invalidated the route to D.
	 */
	Time DeletePeriod;
	Time NextHopWait;	   ///< Period of our waiting for the neighbour's RREP_ACK
	Time BlackListTimeout; ///< Time for which the node is put into the blacklist
	uint32_t MaxQueueLen;  ///< The maximum number of packets that we allow a routing protocol to buffer.
	Time MaxQueueTime;	   ///< The maximum period of time that a routing protocol is allowed to buffer a packet for.
	bool DestinationOnly;  ///< Indicates only the destination may respond to this RREQ.
	bool GratuitousReply;  ///< Indicates whether a gratuitous RREP should be unicast to the node originated route
						   ///< discovery.
	bool EnableHello;	   ///< Indicates whether a hello messages enable
	bool EnableBroadcast;  ///< Indicates whether a a broadcast data packets forwarding enable
	//\}

	/// IP protocol
	Ptr<Ipv4> m_ipv4;
	/// Raw unicast socket per each IP interface, map socket -> iface address (IP + mask)
	std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketAddresses;
	/// Raw subnet directed broadcast socket per each IP interface, map socket -> iface address (IP + mask)
	std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketSubnetBroadcastAddresses;
	/// Loopback device used to defer RREQ until packet will be fully formed
	Ptr<NetDevice> m_lo;

	/// vectors of Routing table, index is the interface
	/// so index 0 is loopback, index 1 is for the first channel, index 2 is for the second channel ...
	std::vector<RoutingTable> m_routingTable_vec;
	RoutingTable m_routingTable;

	/// A "drop-front" queue used by the routing layer to buffer packets to which it does not have a route.
	RequestQueue m_queue;
	/// Broadcast ID
	uint32_t m_requestId;
	/// Request sequence number
	uint32_t m_seqNo;
	/// Handle duplicated RREQ
	IdCache m_rreqIdCache;
	/// Handle duplicated broadcast/multicast packets
	DuplicatePacketDetection m_dpd;
	/// Handle neighbors
	Neighbors m_nb;
	/// Number of RREQs used for RREQ rate control
	uint16_t m_rreqCount;
	/// Number of RERRs used for RERR rate control
	uint16_t m_rerrCount;
	/// Wait time at destination after first RREQ to select lowest cumulative ETT path.
	Time m_rreqWaitTime;
	/// Among channels whose ETT is within this multiplier of the minimum ETT,
	/// prefer the one with lower transmit power.
	double m_channelSelectionEttTolerance;
	/// Enable or disable low-power preference when multiple channels are viable.
	bool m_preferLowPowerChannel;
	/// If false, disable ETT/channel-aware custom routing behavior and fall back to traditional AODV logic.
	bool m_useEttRouting;
	/// Enable/disable intermediate-node RREP generation.
	bool m_enableIntermediateRrep;

	struct PendingRreqReply
	{
		RreqHeader bestRreq;
		RoutingTableEntry toOrigin;
		EventId timerEvent;
	};

	/// key: "origin-ip|rreq-id"
	std::map<std::string, PendingRreqReply> m_pendingRreqReply;

  private:
	/// Start protocol operation
	void Start();
	/// create ip to node id map, in order to check for loops
	void create_ip_to_id_map();
	/// get node using ip address
	Ptr<Node> get_node_with_address(Ipv4Address ipv4_addr) const;
	/// get the Ipv4Address that uses the same channel as channel_addr
	Ipv4Address get_same_channel_Ipv4Address(Ipv4Address channel_addr, Ipv4Address current_ipv4);
	/// Build key for pending destination-side delayed RREP selection.
	std::string BuildRreqKey(Ipv4Address origin, uint32_t requestId) const;
	/// Resolve the output device, local interface, and same-channel neighbor IP for a chosen channel.
	bool ResolveRouteOnChannel(Ipv4Address neighbor,
						   uint8_t channel,
						   Ptr<NetDevice>& dev,
						   Ipv4InterfaceAddress& iface,
						   Ipv4Address& nextHop) const;
	/// Resolve best data channel ETT to a specific neighbor.
	double GetBestEttToNeighbor(Ipv4Address neighbor, uint8_t& bestChannel) const;
	/// Timer callback for delayed destination-side RREP.
	void SendDelayedBestReply(std::string key);
    /// check if there is enough power to send to next node since the power we use on each channel is unsymmetric
    /// if not enough power to send to next node, add tag to packet so gradpc-wifi-manager can adjust power
    void check_valid_link(Ptr<Packet> packet, Ptr<NetDevice> dev, Ipv4Address nextHop);

	/// Queue packet and send route request
	void DeferredRouteOutput(Ptr<const Packet> p,
							 const Ipv4Header& header,
							 UnicastForwardCallback ucb,
							 ErrorCallback ecb);
	/// If route exists and valid, forward packet.
	bool Forwarding(Ptr<const Packet> p, const Ipv4Header& header, UnicastForwardCallback ucb, ErrorCallback ecb);
	/**
	 * To reduce congestion in a network, repeated attempts by a source node at route discovery
	 * for a single destination MUST utilize a binary exponential backoff.
	 */
	void ScheduleRreqRetry(Ipv4Address dst);
	/**
	 * Set lifetime field in routing table entry to the maximum of existing lifetime and lt, if the entry exists
	 * \param addr - destination address
	 * \param lt - proposed time for lifetime field in routing table entry for destination with address addr.
	 * \return true if route to destination address addr exist
	 */
	bool UpdateRouteLifeTime(Ipv4Address addr, Time lt);
	/**
	 * Update neighbor record.
	 * \param receiver is supposed to be my interface
	 * \param sender is supposed to be IP address of my neighbor.
	 */
	void UpdateRouteToNeighbor(Ipv4Address sender, Ipv4Address receiver);
	/// Check that packet is send from own interface
	bool IsMyOwnAddress(Ipv4Address src);
	/// Find unicast socket with local interface address iface
	Ptr<Socket> FindSocketWithInterfaceAddress(Ipv4InterfaceAddress iface) const;
	/// Find subnet directed broadcast socket with local interface address iface
	Ptr<Socket> FindSubnetBroadcastSocketWithInterfaceAddress(Ipv4InterfaceAddress iface) const;
	/// Process hello message
	void ProcessHello(const RrepHeader& rrepHeader, Ipv4Address receiverIfaceAddr);
	/// Create loopback route for given header
	Ptr<Ipv4Route> LoopbackRoute(const Ipv4Header& header, Ptr<NetDevice> oif) const;

	///\name Receive control packets
	//\{
	/// Receive and process control packet
	void RecvAodv(Ptr<Socket> socket);
	/// Receive RREQ
	void RecvRequest(Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src);
	/// Receive RREP
	void RecvReply(Ptr<Packet> p, Ipv4Address my, Ipv4Address src);
	/// Receive RREP_ACK
	void RecvReplyAck(Ipv4Address neighbor);
	/// Receive RERR from node with address src
	void RecvError(Ptr<Packet> p, Ipv4Address src);
	//\}

	///\name Send
	//\{
	/// Forward packet from route request queue
	void SendPacketFromQueue(Ipv4Address dst, Ptr<Ipv4Route> route);
	/// Send hello
	void SendHello();
	/// Send RREQ
	void SendRequest(Ipv4Address dst);
	/// Send RREP
	void SendReply(const RreqHeader& rreqHeader, const RoutingTableEntry& toOrigin);
	/** Send RREP by intermediate node
	 * \param toDst routing table entry to destination
	 * \param toOrigin routing table entry to originator
	 * \param gratRep indicates whether a gratuitous RREP should be unicast to destination
	 */
	void SendReplyByIntermediateNode(RoutingTableEntry& toDst, RoutingTableEntry& toOrigin, bool gratRep);
	/// Send RREP_ACK
	void SendReplyAck(Ipv4Address neighbor);
	/// Initiate RERR
	void SendRerrWhenBreaksLinkToNextHop(Ipv4Address nextHop);
	/// Forward RERR
	void SendRerrMessage(Ptr<Packet> packet, std::vector<Ipv4Address> precursors);
	/**
	 * Send RERR message when no route to forward input packet. Unicast if there is reverse route to originating node,
	 * broadcast otherwise. \param dst - destination node IP address \param dstSeqNo - destination node sequence number
	 * \param origin - originating node IP address
	 */
	void SendRerrWhenNoRouteToForward(Ipv4Address dst, uint32_t dstSeqNo, Ipv4Address origin);
	/// @}

	void SendTo(Ptr<Socket> socket, Ptr<Packet> packet, Ipv4Address destination);

	/// Hello timer
	Timer m_htimer;
	/// Schedule next send of hello message
	void HelloTimerExpire();
	/// RREQ rate limit timer
	Timer m_rreqRateLimitTimer;
	/// Reset RREQ count and schedule RREQ rate limit timer with delay 1 sec.
	void RreqRateLimitTimerExpire();
	/// RERR rate limit timer
	Timer m_rerrRateLimitTimer;
	/// Reset RERR count and schedule RERR rate limit timer with delay 1 sec.
	void RerrRateLimitTimerExpire();
	/// Map IP address + RREQ timer.
	std::map<Ipv4Address, Timer> m_addressReqTimer;
	/// Handle route discovery process
	void RouteRequestTimerExpire(Ipv4Address dst);
	/// Mark link to neighbor node as unidirectional for blacklistTimeout
	void AckTimerExpire(Ipv4Address neighbor, Time blacklistTimeout);

	/// Provides uniform random variables.
	Ptr<UniformRandomVariable> m_uniformRandomVariable;
	/// Keep track of the last bcast time
	Time m_lastBcastTime;

	/// Proactive route re-discovery
	Time m_proactiveRediscoveryInterval; ///< 0 means disabled
	Timer m_proactiveRediscoveryTimer;   ///< Fires every m_proactiveRediscoveryInterval
	std::set<Ipv4Address> m_activeDestinations; ///< Destinations this node is actively sending to
	/// Periodically re-issue RREQ for all active destinations to update routes with fresh ETT
	void ProactiveRediscoveryTimerExpire();

	// ---- Data-phase ETT measurement state ----

	/// Whether data-phase ETT measurement is active (set true when data transfer starts).
	bool m_dataPhaseActive{false};

	/// Per-(neighbor MAC, channel) transmission counters.
	std::map<NeighborChannelKey, DataPhaseCounters, NeighborChannelKeyCmp> m_dataCounters;

	/// Blacklisted (neighbor MAC, channel) pairs — prevents oscillation after channel switch.
	/// Value is the time the entry was added.  Entries expire after ChannelBlacklistTimeout.
	/// Also cleared when RERR/RREQ triggers route rebuild.
	std::map<NeighborChannelKey, Time, NeighborChannelKeyCmp> m_channelBlacklist;

	/// How long a channel stays blacklisted before it can be reconsidered.
	static const double CHANNEL_BLACKLIST_TIMEOUT_S;

	/// Last observed data TX rate (bps) per channel interface index.
	/// Updated via MonitorSnifferTx; falls back to PHY base rate when not yet observed.




	/// Helper: resolve MAC address to neighbor node ID.  Returns false if not found.
	bool ResolveMacToNodeId(Mac48Address mac, uint32_t& nodeId) const;

	/// Helper: resolve MAC address to an Ipv4Address on any interface. Returns Ipv4Address() if not found.
	Ipv4Address ResolveMacToIpv4(Mac48Address mac) const;

	/// Helper: get the channel (interface index) from a PHY trace context string.
	uint16_t GetChannelFromContext(const std::string& context) const;

	/// Internal without-context callback wrappers (connected in DoInitialize).
	void DataPhasePhyTxBeginLocal(Ptr<const Packet> packet);
	void DataPhasePhyRxEndLocal(Ptr<const Packet> packet);
	void DataPhaseMacTxFinalFailedLocal(Mac48Address address);

	/// Attempt to switch to a better channel for the given neighbor MAC.
	/// Returns true if switch was performed.
	bool TryChannelSwitch(Mac48Address neighborMac, uint16_t currentChannel);
};

} // namespace aodv
} // namespace ns3
#endif /* AODVROUTINGPROTOCOL_H */
