/*
 * Copyright (c) 2011 Yufei Cheng
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
 * Author: Yufei Cheng   <yfcheng@ittc.ku.edu>
 *
 * James P.G. Sterbenz <jpgs@ittc.ku.edu>, director
 * ResiliNets Research Group  https://resilinets.org/
 * Information and Telecommunication Technology Center (ITTC)
 * and Department of Electrical Engineering and Computer Science
 * The University of Kansas Lawrence, KS USA.
 *
 * Work supported in part by NSF FIND (Future Internet Design) Program
 * under grant CNS-0626918 (Postmodern Internet Architecture),
 * NSF grant CNS-1050226 (Multilayer Network Resilience Analysis and Experimentation on GENI),
 * US Department of Defense (DoD), and ITTC at The University of Kansas.
 */

#define NS_LOG_APPEND_CONTEXT                                                                                          \
	if (GetObject<Node>())                                                                                             \
	{                                                                                                                  \
		std::clog << "[node " << GetObject<Node>()->GetId() << "] ";                                                   \
	}

#include "dsr-routing.h"

#include "dsr-fs-header.h"
#include "dsr-options.h"
#include "dsr-rcache.h"
#include "dsr-rreq-table.h"

#include "ns3/adhoc-wifi-mac.h"
#include "ns3/arp-header.h"
#include "ns3/assert.h"
#include "ns3/boolean.h"
#include "ns3/config.h"
#include "ns3/double.h"
#include "ns3/enum.h"
#include "ns3/grad-pc.h"
#include "ns3/icmpv4-l4-protocol.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv6-interface.h"
#include "ns3/llc-snap-header.h"
#include "ns3/log.h"
#include "ns3/net-device.h"
#include "ns3/node-list.h"
#include "ns3/object-vector.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/ptr.h"
#include "ns3/string.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/timer.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-l4-protocol.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-net-device.h"

#include <algorithm>
#include <ctime>
#include <iostream>
#include <limits>
#include <list>
#include <map>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DsrRouting");

namespace dsr
{

NS_OBJECT_ENSURE_REGISTERED(DsrRouting);

static std::string
Ipv4addr_to_str(const Ipv4Address ipv4_addr)
{
	uint32_t addr = ipv4_addr.Get(); // Get the host-order 32-bit IP address.
	std::string ipv4_str{};
	ipv4_str += std::to_string((addr >> 24) & 0xff) + ".";
	ipv4_str += std::to_string((addr >> 16) & 0xff) + ".";
	ipv4_str += std::to_string((addr >> 8) & 0xff) + ".";
	ipv4_str += std::to_string((addr >> 0) & 0xff);
	return ipv4_str;
}

// debug function
void
DsrRouting::Print_sendBuffer(DsrSendBuffer dsr_sendBuffer)
{
	NS_LOG_DEBUG("In Print sendBuffer function");
	std::vector<DsrSendBuffEntry> sendBuffer = dsr_sendBuffer.GetBuffer();
	NS_LOG_DEBUG("sendBuffer size: " << sendBuffer.size());
	int packet_num = 0;
	for (std::vector<DsrSendBuffEntry>::const_iterator i = sendBuffer.begin(); i != sendBuffer.end(); ++i)
	{
		// debug print
		NS_LOG_DEBUG("packet " << packet_num << " destination: " << i->GetDestination());
		++packet_num;
	}
}

// get the Ipv4Address that uses the same channel as channel_addr
Ipv4Address
DsrRouting::get_same_channel_Ipv4Address(Ipv4Address channel_addr, Ipv4Address current_ipv4)
{
	auto ipv4 = GetNodeWithAddress(channel_addr)->GetObject<Ipv4>();
	int32_t interface_id = ipv4->GetInterfaceForAddress(channel_addr);
	auto curr_ipv4 = GetNodeWithAddress(current_ipv4)->GetObject<Ipv4>();
	return curr_ipv4->GetAddress(interface_id, 0).GetLocal();
}

std::tuple<bool, Ipv4Address>
DsrRouting::Lookup_destination_route_with_all_ip(Ipv4Address destination, DsrRouteCacheEntry& min_hop_toDst)
{
	bool findRoute = false;
	Ptr<Node> dst_node = GetNodeWithAddress(destination);
	Ptr<Ipv4> dst_ipv4 = dst_node->GetObject<Ipv4>();
	unsigned int net_card_num = dst_node->GetNDevices() - 1;
	Ipv4Address addr;
	unsigned int min_hop = m_discoveryHopLimit;
	DsrRouteCacheEntry toDst;
	for (uint32_t i = 1; i <= net_card_num; ++i)
	{
		addr = dst_ipv4->GetAddress(i, 0).GetLocal();
		if (m_routeCache->LookupRoute(addr, toDst))
		{
			const unsigned int vec_size = toDst.GetVector().size();
			if (vec_size > 0 && vec_size < min_hop)
			{
				findRoute = true;
				min_hop = toDst.GetVector().size();
				min_hop_toDst = toDst;
			}
		}
	}
	return {findRoute, addr};
}

/* see http://www.iana.org/assignments/protocol-numbers */
const uint8_t DsrRouting::PROT_NUMBER = 48;

/*
 * The extension header is the fixed size dsr header, it is response for recognizing DSR option
 types
 * and demux to right options to process the packet.
 *
 * The header format with neighboring layers is as follows:
 *
 +-+-+-+-+-+-+-+-+-+-+-
 |  Application Header |
 +-+-+-+-+-+-+-+-+-+-+-+
 |   Transport Header  |
 +-+-+-+-+-+-+-+-+-+-+-+
 |   Fixed DSR Header  |
 +---------------------+
 |     DSR Options     |
 +-+-+-+-+-+-+-+-+-+-+-+
 |      IP Header      |
 +-+-+-+-+-+-+-+-+-+-+-+
 */

TypeId
DsrRouting::GetTypeId()
{
	static TypeId tid =
		TypeId("ns3::dsr::DsrRouting")
			.SetParent<IpL4Protocol>()
			.SetGroupName("Dsr")
			.AddConstructor<DsrRouting>()
			.AddAttribute("RouteCache",
						  "The route cache for saving routes from "
						  "route discovery process.",
						  PointerValue(nullptr),
						  MakePointerAccessor(&DsrRouting::SetRouteCache, &DsrRouting::GetRouteCache),
						  MakePointerChecker<DsrRouteCache>())
			.AddAttribute("RreqTable",
						  "The request table to manage route requests.",
						  PointerValue(nullptr),
						  MakePointerAccessor(&DsrRouting::SetRequestTable, &DsrRouting::GetRequestTable),
						  MakePointerChecker<DsrRreqTable>())
			.AddAttribute("PassiveBuffer",
						  "The passive buffer to manage "
						  "promisucously received passive ack.",
						  PointerValue(nullptr),
						  MakePointerAccessor(&DsrRouting::SetPassiveBuffer, &DsrRouting::GetPassiveBuffer),
						  MakePointerChecker<DsrPassiveBuffer>())
			.AddAttribute("MaxSendBuffLen",
						  "Maximum number of packets that can be stored "
						  "in send buffer.",
						  UintegerValue(64),
						  MakeUintegerAccessor(&DsrRouting::m_maxSendBuffLen),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("MaxSendBuffTime",
						  "Maximum time packets can be queued in the send buffer .",
						  TimeValue(Seconds(30)),
						  MakeTimeAccessor(&DsrRouting::m_sendBufferTimeout),
						  MakeTimeChecker())
			.AddAttribute("MaxMaintLen",
						  "Maximum number of packets that can be stored "
						  "in maintenance buffer.",
						  UintegerValue(50),
						  MakeUintegerAccessor(&DsrRouting::m_maxMaintainLen),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("MaxMaintTime",
						  "Maximum time packets can be queued in maintenance buffer.",
						  TimeValue(Seconds(30)),
						  MakeTimeAccessor(&DsrRouting::m_maxMaintainTime),
						  MakeTimeChecker())
			.AddAttribute("MaxCacheLen",
						  "Maximum number of route entries that can be stored "
						  "in route cache.",
						  UintegerValue(64),
						  MakeUintegerAccessor(&DsrRouting::m_maxCacheLen),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("RouteCacheTimeout",
						  "Maximum time the route cache can be queued in "
						  "route cache.",
						  TimeValue(Seconds(300)),
						  MakeTimeAccessor(&DsrRouting::m_maxCacheTime),
						  MakeTimeChecker())
			.AddAttribute("MaxEntriesEachDst",
						  "Maximum number of route entries for a "
						  "single destination to respond.",
						  UintegerValue(20),
						  MakeUintegerAccessor(&DsrRouting::m_maxEntriesEachDst),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("SendBuffInterval",
						  "How often to check send buffer for packet with route.",
						  TimeValue(Seconds(500)),
						  MakeTimeAccessor(&DsrRouting::m_sendBuffInterval),
						  MakeTimeChecker())
			.AddAttribute("NodeTraversalTime",
						  "The time it takes to traverse two neighboring nodes.",
						  TimeValue(MilliSeconds(40)),
						  MakeTimeAccessor(&DsrRouting::m_nodeTraversalTime),
						  MakeTimeChecker())
			.AddAttribute("RreqRetries",
						  "Maximum number of retransmissions for "
						  "request discovery of a route.",
						  UintegerValue(16),
						  MakeUintegerAccessor(&DsrRouting::m_rreqRetries),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("MaintenanceRetries",
						  "Maximum number of retransmissions for "
						  "data packets from maintenance buffer.",
						  UintegerValue(2),
						  MakeUintegerAccessor(&DsrRouting::m_maxMaintRexmt),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("RequestTableSize",
						  "Maximum number of request entries in the request table, "
						  "set this as the number of nodes in the simulation.",
						  UintegerValue(64),
						  MakeUintegerAccessor(&DsrRouting::m_requestTableSize),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("RequestIdSize",
						  "Maximum number of request source Ids in "
						  "the request table.",
						  UintegerValue(16),
						  MakeUintegerAccessor(&DsrRouting::m_requestTableIds),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("UniqueRequestIdSize",
						  "Maximum number of request Ids in "
						  "the request table for a single destination.",
						  UintegerValue(256),
						  MakeUintegerAccessor(&DsrRouting::m_maxRreqId),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("NonPropRequestTimeout",
						  "The timeout value for non-propagation request.",
						  TimeValue(MilliSeconds(30)),
						  MakeTimeAccessor(&DsrRouting::m_nonpropRequestTimeout),
						  MakeTimeChecker())
			.AddAttribute("DiscoveryHopLimit",
						  "The max discovery hop limit for route requests.",
						  UintegerValue(255),
						  MakeUintegerAccessor(&DsrRouting::m_discoveryHopLimit),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("MaxSalvageCount",
						  "The max salvage count for a single data packet.",
						  UintegerValue(15),
						  MakeUintegerAccessor(&DsrRouting::m_maxSalvageCount),
						  MakeUintegerChecker<uint8_t>())
			.AddAttribute("BlacklistTimeout",
						  "The time for a neighbor to stay in blacklist.",
						  TimeValue(Seconds(3)),
						  MakeTimeAccessor(&DsrRouting::m_blacklistTimeout),
						  MakeTimeChecker())
			.AddAttribute("GratReplyHoldoff",
						  "The time for gratuitous reply entry to expire.",
						  TimeValue(Seconds(1)),
						  MakeTimeAccessor(&DsrRouting::m_gratReplyHoldoff),
						  MakeTimeChecker())
			.AddAttribute("BroadcastJitter",
						  "The jitter time to avoid collision for broadcast packets.",
						  UintegerValue(10),
						  MakeUintegerAccessor(&DsrRouting::m_broadcastJitter),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("LinkAckTimeout",
						  "The time a packet in maintenance buffer wait for "
						  "link acknowledgment.",
						  TimeValue(MilliSeconds(100)),
						  MakeTimeAccessor(&DsrRouting::m_linkAckTimeout),
						  MakeTimeChecker())
			.AddAttribute("TryLinkAcks",
						  "The number of link acknowledgment to use.",
						  UintegerValue(1),
						  MakeUintegerAccessor(&DsrRouting::m_tryLinkAcks),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("PassiveAckTimeout",
						  "The time a packet in maintenance buffer wait for "
						  "passive acknowledgment.",
						  TimeValue(MilliSeconds(100)),
						  MakeTimeAccessor(&DsrRouting::m_passiveAckTimeout),
						  MakeTimeChecker())
			.AddAttribute("TryPassiveAcks",
						  "The number of passive acknowledgment to use.",
						  UintegerValue(1),
						  MakeUintegerAccessor(&DsrRouting::m_tryPassiveAcks),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("RequestPeriod",
						  "The base time interval between route requests.",
						  TimeValue(MilliSeconds(500)),
						  MakeTimeAccessor(&DsrRouting::m_requestPeriod),
						  MakeTimeChecker())
			.AddAttribute("MaxRequestPeriod",
						  "The max time interval between route requests.",
						  TimeValue(Seconds(10)),
						  MakeTimeAccessor(&DsrRouting::m_maxRequestPeriod),
						  MakeTimeChecker())
			.AddAttribute("GraReplyTableSize",
						  "The gratuitous reply table size.",
						  UintegerValue(64),
						  MakeUintegerAccessor(&DsrRouting::m_graReplyTableSize),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("CacheType",
						  "Use Link Cache or use Path Cache",
						  StringValue("LinkCache"),
						  MakeStringAccessor(&DsrRouting::m_cacheType),
						  MakeStringChecker())
			.AddAttribute("StabilityDecrFactor",
						  "The stability decrease factor for link cache",
						  UintegerValue(2),
						  MakeUintegerAccessor(&DsrRouting::m_stabilityDecrFactor),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("StabilityIncrFactor",
						  "The stability increase factor for link cache",
						  UintegerValue(4),
						  MakeUintegerAccessor(&DsrRouting::m_stabilityIncrFactor),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("InitStability",
						  "The initial stability factor for link cache",
						  TimeValue(Seconds(25)),
						  MakeTimeAccessor(&DsrRouting::m_initStability),
						  MakeTimeChecker())
			.AddAttribute("MinLifeTime",
						  "The minimal life time for link cache",
						  TimeValue(Seconds(1)),
						  MakeTimeAccessor(&DsrRouting::m_minLifeTime),
						  MakeTimeChecker())
			.AddAttribute("UseExtends",
						  "The extension time for link cache",
						  TimeValue(Seconds(120)),
						  MakeTimeAccessor(&DsrRouting::m_useExtends),
						  MakeTimeChecker())
			.AddAttribute("EnableSubRoute",
						  "Enables saving of sub route when receiving "
						  "route error messages, only available when "
						  "using path route cache",
						  BooleanValue(true),
						  MakeBooleanAccessor(&DsrRouting::m_subRoute),
						  MakeBooleanChecker())
			.AddAttribute("RetransIncr",
						  "The increase time for retransmission timer "
						  "when facing network congestion",
						  TimeValue(MilliSeconds(20)),
						  MakeTimeAccessor(&DsrRouting::m_retransIncr),
						  MakeTimeChecker())
			.AddAttribute("MaxNetworkQueueSize",
						  "The max number of packet to save in the network queue.",
						  UintegerValue(400),
						  MakeUintegerAccessor(&DsrRouting::m_maxNetworkSize),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("MaxNetworkQueueDelay",
						  "The max time for a packet to stay in the network queue.",
						  TimeValue(Seconds(30.0)),
						  MakeTimeAccessor(&DsrRouting::m_maxNetworkDelay),
						  MakeTimeChecker())
			.AddAttribute("NumPriorityQueues",
						  "The max number of packet to save in the network queue.",
						  UintegerValue(2),
						  MakeUintegerAccessor(&DsrRouting::m_numPriorityQueues),
						  MakeUintegerChecker<uint32_t>())
			.AddAttribute("LinkAcknowledgment",
						  "Enable Link layer acknowledgment mechanism",
						  BooleanValue(true),
						  MakeBooleanAccessor(&DsrRouting::m_linkAck),
						  MakeBooleanChecker())
			.AddAttribute("EnableGlobalBroadcast",
						  "Enable single hop global broadcast",
						  BooleanValue(true),
						  MakeBooleanAccessor(&DsrRouting::m_enable_global_broadcast),
						  MakeBooleanChecker())
			.AddAttribute("UseGradPC",
						  "Set to true if using grad power control",
						  BooleanValue(false),
						  MakeBooleanAccessor(&DsrRouting::m_use_gradpc),
						  MakeBooleanChecker())
			/*
			.AddAttribute("MinChannelRssiInterval",
						  "Minimum time for the channel rssi detection to be valid",
						  TimeValue(MilliSeconds(20)),
						  MakeTimeAccessor(&DsrRouting::m_min_channel_rssi_interval),
						  MakeTimeChecker())
			.AddAttribute("ChannelRssiInitValue",
						  "initial value of channel rssi",
						  DoubleValue(-1000.0),
						  MakeDoubleAccessor(&DsrRouting::m_channel_rssi_init_value),
						  MakeDoubleChecker<double>())
			*/
			.AddTraceSource("Drop",
							"Drop DSR packet",
							MakeTraceSourceAccessor(&DsrRouting::m_dropTrace),
							"ns3::Packet::TracedCallback")
			.AddTraceSource("Tx",
							"Send DSR packet.",
							MakeTraceSourceAccessor(&DsrRouting::m_txPacketTrace),
							"ns3::dsr::DsrOptionSRHeader::TracedCallback");
	return tid;
}

DsrRouting::DsrRouting()
{
	NS_LOG_FUNCTION_NOARGS();

	m_uniformRandomVariable = CreateObject<UniformRandomVariable>();

	/*
	 * The following Ptr statements created objects for all the options header for DSR, and each of
	 * them have distinct option number assigned, when DSR Routing received a packet from higher
	 * layer, it will find the following options based on the option number, and pass the packet to
	 * the appropriate option to process it. After the option processing, it will pass the packet
	 * back to DSR Routing to send down layer.
	 */
	Ptr<dsr::DsrOptionPad1> pad1Option = CreateObject<dsr::DsrOptionPad1>();
	Ptr<dsr::DsrOptionPadn> padnOption = CreateObject<dsr::DsrOptionPadn>();
	Ptr<dsr::DsrOptionRreq> rreqOption = CreateObject<dsr::DsrOptionRreq>();
	Ptr<dsr::DsrOptionRrep> rrepOption = CreateObject<dsr::DsrOptionRrep>();
	Ptr<dsr::DsrOptionSR> srOption = CreateObject<dsr::DsrOptionSR>();
	Ptr<dsr::DsrOptionRerr> rerrOption = CreateObject<dsr::DsrOptionRerr>();
	Ptr<dsr::DsrOptionAckReq> ackReq = CreateObject<dsr::DsrOptionAckReq>();
	Ptr<dsr::DsrOptionAck> ack = CreateObject<dsr::DsrOptionAck>();

	Insert(pad1Option);
	Insert(padnOption);
	Insert(rreqOption);
	Insert(rrepOption);
	Insert(srOption);
	Insert(rerrOption);
	Insert(ackReq);
	Insert(ack);

	// Check the send buffer for sending packets
	m_sendBuffTimer.SetFunction(&DsrRouting::SendBuffTimerExpire, this);
	m_sendBuffTimer.Schedule(Seconds(100));
}

DsrRouting::~DsrRouting()
{
	NS_LOG_FUNCTION_NOARGS();
}

void
DsrRouting::NotifyNewAggregate()
{
	NS_LOG_FUNCTION(this << "NotifyNewAggregate");
	if (!m_node)
	{
		Ptr<Node> node = this->GetObject<Node>();
		if (node)
		{
			m_ipv4 = this->GetObject<Ipv4L3Protocol>();
			if (m_ipv4)
			{
				this->SetNode(node);
				m_ipv4->Insert(this);
				this->SetDownTarget(MakeCallback(&Ipv4L3Protocol::Send, m_ipv4));
			}

			m_ip = node->GetObject<Ipv4>();
			if (m_ip)
			{
				NS_LOG_DEBUG("Ipv4 started");
			}
		}
	}
	IpL4Protocol::NotifyNewAggregate();
	Simulator::ScheduleNow(&DsrRouting::Start, this);
}

void
DsrRouting::create_ip_to_id_map()
{
	int32_t nNodes = NodeList::GetNNodes();
	for (int32_t i = 0; i < nNodes; ++i)
	{
		Ptr<Node> node = NodeList::GetNode(i);
		Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
		unsigned int net_card_num = node->GetNDevices() - 1;
		for (uint32_t j = 1; j <= net_card_num; ++j)
		{
			std::string key = Ipv4addr_to_str(ipv4->GetAddress(j, 0).GetLocal());
			ip_to_id_map[key] = i;
		}
	}
}

void
DsrRouting::Start()
{
	NS_LOG_FUNCTION(this << "Start DSR Routing protocol");

	NS_LOG_INFO("The number of network queues " << m_numPriorityQueues);
	for (uint32_t i = 0; i < m_numPriorityQueues; i++)
	{
		// Set the network queue max size and the delay
		NS_LOG_INFO("The network queue size " << m_maxNetworkSize << " and the queue delay "
											  << m_maxNetworkDelay.As(Time::S));
		Ptr<dsr::DsrNetworkQueue> queue_i = CreateObject<dsr::DsrNetworkQueue>(m_maxNetworkSize, m_maxNetworkDelay);
		std::pair<std::map<uint32_t, Ptr<dsr::DsrNetworkQueue>>::iterator, bool> result_i =
			m_priorityQueue.insert(std::make_pair(i, queue_i));
		NS_ASSERT_MSG(result_i.second, "Error in creating queues");
	}
	Ptr<dsr::DsrRreqTable> rreqTable = CreateObject<dsr::DsrRreqTable>();
	// Set the initial hop limit
	rreqTable->SetInitHopLimit(m_discoveryHopLimit);
	// Configure the request table parameters
	rreqTable->SetRreqTableSize(m_requestTableSize);
	rreqTable->SetRreqIdSize(m_requestTableIds);
	rreqTable->SetUniqueRreqIdSize(m_maxRreqId);
	SetRequestTable(rreqTable);
	// Set the passive buffer parameters using just the send buffer parameters
	Ptr<dsr::DsrPassiveBuffer> passiveBuffer = CreateObject<dsr::DsrPassiveBuffer>();
	passiveBuffer->SetMaxQueueLen(m_maxSendBuffLen);
	passiveBuffer->SetPassiveBufferTimeout(m_sendBufferTimeout);
	SetPassiveBuffer(passiveBuffer);

	// Set the send buffer parameters
	m_sendBuffer.SetMaxQueueLen(m_maxSendBuffLen);
	m_sendBuffer.SetSendBufferTimeout(m_sendBufferTimeout);
	// Set the error buffer parameters using just the send buffer parameters
	m_errorBuffer.SetMaxQueueLen(m_maxSendBuffLen);
	m_errorBuffer.SetErrorBufferTimeout(m_sendBufferTimeout);
	// Set the maintenance buffer parameters
	m_maintainBuffer.SetMaxQueueLen(m_maxMaintainLen);
	m_maintainBuffer.SetMaintainBufferTimeout(m_maxMaintainTime);
	// Set the gratuitous reply table size
	m_graReply.SetGraTableSize(m_graReplyTableSize);

	if (m_mainAddress == Ipv4Address())
	{
		Ipv4Address loopback("127.0.0.1");
		for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++)
		{
			// Use primary address, if multiple
			Ipv4Address addr = m_ipv4->GetAddress(i, 0).GetLocal();
			m_broadcast = m_ipv4->GetAddress(i, 0).GetBroadcast();
			if (addr != loopback)
			{
				/*
				 * Set dsr route cache
				 */
				Ptr<dsr::DsrRouteCache> routeCache = CreateObject<dsr::DsrRouteCache>();
				// Configure the path cache parameters
				routeCache->SetCacheType(m_cacheType);
				routeCache->SetSubRoute(m_subRoute);
				routeCache->SetMaxCacheLen(m_maxCacheLen);
				routeCache->SetCacheTimeout(m_maxCacheTime);
				routeCache->SetMaxEntriesEachDst(m_maxEntriesEachDst);
				// Parameters for link cache
				routeCache->SetStabilityDecrFactor(m_stabilityDecrFactor);
				routeCache->SetStabilityIncrFactor(m_stabilityIncrFactor);
				routeCache->SetInitStability(m_initStability);
				routeCache->SetMinLifeTime(m_minLifeTime);
				routeCache->SetUseExtends(m_useExtends);
				routeCache->ScheduleTimer();
				// The call back to handle link error and send error message to appropriate nodes
				/// TODO whether this SendRerrWhenBreaksLinkToNextHop is used or not
				// routeCache->SetCallback (MakeCallback
				// (&DsrRouting::SendRerrWhenBreaksLinkToNextHop, this));
				SetRouteCache(routeCache);
				// Set the main address as the current ip address
				m_mainAddress = addr;

				unsigned int net_card_num = m_node->GetNDevices() - 1;
				for (unsigned int j = 1; j <= net_card_num; ++j)
				{
					m_ipv4->GetNetDevice(j)->SetPromiscReceiveCallback(MakeCallback(&DsrRouting::PromiscReceive, this));
				}

				// Allow neighbor manager use this interface for layer 2 feedback if possible
				Ptr<NetDevice> dev = m_ipv4->GetNetDevice(m_ipv4->GetInterfaceForAddress(addr));
				Ptr<WifiNetDevice> wifi = dev->GetObject<WifiNetDevice>();
				if (!wifi)
				{
					break;
				}
				Ptr<WifiMac> mac = wifi->GetMac();
				if (!mac)
				{
					break;
				}

				routeCache->AddArpCache(m_ipv4->GetInterface(i)->GetArpCache());
				NS_LOG_LOGIC("Starting DSR on node " << m_mainAddress);
				break;
			}
		}
		NS_ASSERT(m_mainAddress != Ipv4Address() && m_broadcast != Ipv4Address());
	}
	create_ip_to_id_map();
}

Ptr<NetDevice>
DsrRouting::GetNetDeviceFromContext(std::string context)
{
	// Use "NodeList/*/DeviceList/*/ as reference
	// where element [1] is the Node Id
	// element [2] is the NetDevice Id
	std::vector<std::string> elements = GetElementsFromContext(context);
	Ptr<Node> n = NodeList::GetNode(std::stoi(elements[1]));
	NS_ASSERT(n);
	return n->GetDevice(std::stoi(elements[3]));
}

std::vector<std::string>
DsrRouting::GetElementsFromContext(std::string context)
{
	std::vector<std::string> elements;
	size_t pos1 = 0;
	size_t pos2;
	while (pos1 != std::string::npos)
	{
		pos1 = context.find('/', pos1);
		pos2 = context.find('/', pos1 + 1);
		elements.push_back(context.substr(pos1 + 1, pos2 - (pos1 + 1)));
		pos1 = pos2;
	}
	return elements;
}

void
DsrRouting::DoDispose()
{
	// NS_LOG_FUNCTION_NOARGS();
	m_node = nullptr;
	for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++)
	{
		// Disable layer 2 link state monitoring (if possible)
		Ptr<NetDevice> dev = m_ipv4->GetNetDevice(i);
		Ptr<WifiNetDevice> wifi = dev->GetObject<WifiNetDevice>();
		if (wifi)
		{
			Ptr<WifiMac> mac = wifi->GetMac();
			if (mac)
			{
				Ptr<AdhocWifiMac> adhoc = mac->GetObject<AdhocWifiMac>();
				if (adhoc)
				{
					// remove this line since it is a dsr bug
					// adhoc->TraceDisconnectWithoutContext("TxErrHeader",
					// m_routeCache->GetTxErrorCallback());
					m_routeCache->DelArpCache(m_ipv4->GetInterface(i)->GetArpCache());
				}
			}
		}
	}
	IpL4Protocol::DoDispose();
}

void
DsrRouting::SetNode(Ptr<Node> node)
{
	m_node = node;
}

Ptr<Node>
DsrRouting::GetNode() const
{
	NS_LOG_FUNCTION_NOARGS();
	return m_node;
}

void
DsrRouting::SetRouteCache(Ptr<dsr::DsrRouteCache> r)
{
	// / Set the route cache to use
	m_routeCache = r;
}

Ptr<dsr::DsrRouteCache>
DsrRouting::GetRouteCache() const
{
	// / Get the route cache to use
	return m_routeCache;
}

void
DsrRouting::SetRequestTable(Ptr<dsr::DsrRreqTable> q)
{
	// / Set the request table to use
	m_rreqTable = q;
}

Ptr<dsr::DsrRreqTable>
DsrRouting::GetRequestTable() const
{
	// / Get the request table to use
	return m_rreqTable;
}

void
DsrRouting::SetPassiveBuffer(Ptr<dsr::DsrPassiveBuffer> p)
{
	// / Set the request table to use
	m_passiveBuffer = p;
}

Ptr<dsr::DsrPassiveBuffer>
DsrRouting::GetPassiveBuffer() const
{
	// / Get the request table to use
	return m_passiveBuffer;
}

Ptr<Node>
DsrRouting::GetNodeWithAddress(Ipv4Address ipv4Address)
{
	NS_LOG_FUNCTION(this << ipv4Address);
	int32_t nNodes = NodeList::GetNNodes();
	std::string key = Ipv4addr_to_str(ipv4Address);
	if (ip_to_id_map.count(key))
	{
		return NodeList::GetNode(ip_to_id_map.at(key));
	}
	return nullptr;
	/*
	for (int32_t i = 0; i < nNodes; ++i)
	{
		Ptr<Node> node = NodeList::GetNode(i);
		Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
		int32_t ifIndex = ipv4->GetInterfaceForAddress(ipv4Address);
		if (ifIndex != -1)
		{
			return node;
		}
	}
	return nullptr;
	*/
}

bool
DsrRouting::IsLinkCache()
{
	return m_routeCache->IsLinkCache();
}

void
DsrRouting::UseExtends(DsrRouteCacheEntry::IP_VECTOR rt)
{
	m_routeCache->UseExtends(rt);
}

bool
DsrRouting::LookupRoute(Ipv4Address id, DsrRouteCacheEntry& rt)
{
	return m_routeCache->LookupRoute(id, rt);
}

bool
DsrRouting::AddRoute_Link(DsrRouteCacheEntry::IP_VECTOR nodelist, Ipv4Address source)
{
	Ipv4Address nextHop = SearchNextHop(source, nodelist);
	m_errorBuffer.DropPacketForErrLink(source, nextHop);
	return m_routeCache->AddRoute_Link(nodelist, source);
}

bool
DsrRouting::AddRoute(DsrRouteCacheEntry& rt)
{
	std::vector<Ipv4Address> nodelist = rt.GetVector();
	Ipv4Address nextHop = SearchNextHop(m_mainAddress, nodelist);
	NS_LOG_INFO("m_mainAddress: " << m_mainAddress);
	m_errorBuffer.DropPacketForErrLink(m_mainAddress, nextHop);
	return m_routeCache->AddRoute(rt);
}

void
DsrRouting::DeleteAllRoutesIncludeLink(Ipv4Address errorSrc, Ipv4Address unreachNode, Ipv4Address node)
{
	m_routeCache->DeleteAllRoutesIncludeLink(errorSrc, unreachNode, node);
}

bool
DsrRouting::UpdateRouteEntry(Ipv4Address dst)
{
	return m_routeCache->UpdateRouteEntry(dst);
}

bool
DsrRouting::FindSourceEntry(Ipv4Address src, Ipv4Address dst, uint16_t id)
{
	return m_rreqTable->FindSourceEntry(src, dst, id);
}

Ipv4Address
DsrRouting::GetIPfromMAC(Mac48Address address)
{
	NS_LOG_FUNCTION(this << address);
	int32_t nNodes = NodeList::GetNNodes();
	for (int32_t i = 0; i < nNodes; ++i)
	{
		Ptr<Node> node = NodeList::GetNode(i);
		Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
		Ptr<NetDevice> netDevice = ipv4->GetNetDevice(1);

		// need to minus 1 since we are using GetNetDevice
		// GetNDevices() need to minus 1 since loopback(127.0.0.1) is included
		unsigned int net_card_num = node->GetNDevices() - 1;
		// GetAddress(0, 0) is the loopback ip address
		// for loop starts at index 1 because we don't need to check loopback
		for (uint32_t j = 1; j <= net_card_num; ++j)
		{
			Ptr<NetDevice> netDevice = ipv4->GetNetDevice(j);
			if (netDevice->GetAddress() == address)
			{
				return ipv4->GetAddress(j, 0).GetLocal();
			}
		}
	}
	return nullptr;
}

void
DsrRouting::PrintVector(std::vector<Ipv4Address>& vec)
{
	NS_LOG_FUNCTION(this);
	/*
	 * Check elements in a route vector
	 */
	if (vec.empty())
	{
		NS_LOG_DEBUG("The vector is empty");
	}
	else
	{
		// NS_LOG_DEBUG("Print all the elements in a vector");
		for (std::vector<Ipv4Address>::const_iterator i = vec.begin(); i != vec.end(); ++i)
		{
			NS_LOG_DEBUG("The ip address " << *i);
		}
	}
}

Ipv4Address
DsrRouting::SearchNextHop_with_all_ip(Ipv4Address ipv4Address, std::vector<Ipv4Address>& vec)
{
	NS_LOG_FUNCTION(this << ipv4Address);
	Ipv4Address nextHop;
	NS_LOG_DEBUG("the vector size " << vec.size());
	if (vec.size() == 2)
	{
		NS_LOG_DEBUG("The two nodes are neighbors");
		nextHop = vec[1];
		return nextHop;
	}
	else
	{
		unsigned int node_id = GetIDfromIP(ipv4Address);
		if (node_id == GetIDfromIP(vec.back()))
		{
			NS_LOG_DEBUG("We have reached to the final destination " << vec.back());
			return vec.back();
		}
		for (std::vector<Ipv4Address>::const_iterator i = vec.begin(); i != vec.end(); ++i)
		{
			if (node_id == GetIDfromIP(*i))
			{
				nextHop = *(++i);
				return nextHop;
			}
		}
	}
	NS_LOG_DEBUG("Next hop address not found");
	Ipv4Address none = "0.0.0.0";
	return none;
}

Ipv4Address
DsrRouting::SearchNextHop(Ipv4Address ipv4Address, std::vector<Ipv4Address>& vec)
{
	NS_LOG_FUNCTION(this << ipv4Address);
	Ipv4Address nextHop;
	// NS_LOG_DEBUG("the vector size " << vec.size());
	if (vec.size() == 2)
	{
		NS_LOG_DEBUG("The two nodes are neighbors");
		nextHop = vec[1];
		return nextHop;
	}
	else
	{
		if (ipv4Address == vec.back())
		{
			NS_LOG_DEBUG("We have reached to the final destination " << ipv4Address << " " << vec.back());
			return ipv4Address;
		}
		for (std::vector<Ipv4Address>::const_iterator i = vec.begin(); i != vec.end(); ++i)
		{
			if (ipv4Address == (*i))
			{
				nextHop = *(++i);
				return nextHop;
			}
		}
	}
	NS_LOG_DEBUG("Next hop address not found");
	Ipv4Address none = "0.0.0.0";
	return none;
}

Ptr<Ipv4Route>
DsrRouting::SetRoute(Ipv4Address nextHop, Ipv4Address srcAddress)
{
	NS_LOG_FUNCTION(this << nextHop << srcAddress);
	m_ipv4Route = Create<Ipv4Route>();
	m_ipv4Route->SetDestination(nextHop);
	m_ipv4Route->SetGateway(nextHop);
	m_ipv4Route->SetSource(srcAddress);
	return m_ipv4Route;
}

int
DsrRouting::GetProtocolNumber() const
{
	// / This is the protocol number for DSR which is 48
	return PROT_NUMBER;
}

uint16_t
DsrRouting::GetIDfromIP(Ipv4Address address)
{
	std::string key = Ipv4addr_to_str(address);
	if (ip_to_id_map.count(key))
	{
		return ip_to_id_map.at(key);
	}
	return 256;
}

Ipv4Address
DsrRouting::GetIPfromID(uint16_t id)
{
	if (id >= 256)
	{
		NS_LOG_DEBUG("Exceed the node range");
		return "0.0.0.0";
	}
	else
	{
		Ptr<Node> node = NodeList::GetNode(uint32_t(id));
		Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
		return ipv4->GetAddress(1, 0).GetLocal();
	}
}

uint32_t
DsrRouting::GetPriority(DsrMessageType messageType)
{
	if (messageType == DSR_CONTROL_PACKET)
	{
		return 0;
	}
	else
	{
		return 1;
	}
}

void
DsrRouting::SendBuffTimerExpire()
{
	if (m_sendBuffTimer.IsRunning())
	{
		m_sendBuffTimer.Cancel();
	}
	m_sendBuffTimer.Schedule(m_sendBuffInterval);
	CheckSendBuffer();
}

void
DsrRouting::CheckSendBuffer()
{
	NS_LOG_INFO(Simulator::Now().As(Time::S)
				<< " Checking send buffer at " << m_mainAddress << " with size " << m_sendBuffer.GetSize());

	for (std::vector<DsrSendBuffEntry>::iterator i = m_sendBuffer.GetBuffer().begin();
		 i != m_sendBuffer.GetBuffer().end();)
	{
		NS_LOG_DEBUG("Here we try to find the data packet in the send buffer");
		Ipv4Address destination = i->GetDestination();
		DsrRouteCacheEntry toDst;
		bool findRoute = m_routeCache->LookupRoute(destination, toDst);
		if (findRoute)
		{
			NS_LOG_INFO("We have found a route for the packet");
			Ptr<const Packet> packet = i->GetPacket();
			Ptr<Packet> cleanP = packet->Copy();
			uint8_t protocol = i->GetProtocol();

			i = m_sendBuffer.GetBuffer().erase(i);

			DsrRoutingHeader dsrRoutingHeader;
			Ptr<Packet> copyP = packet->Copy();
			Ptr<Packet> dsrPacket = packet->Copy();
			dsrPacket->RemoveHeader(dsrRoutingHeader);
			uint32_t offset = dsrRoutingHeader.GetDsrOptionsOffset();
			copyP->RemoveAtStart(offset); // Here the processed size is 8 bytes, which is the fixed
										  // sized extension header
			// The packet to get ipv4 header
			Ptr<Packet> ipv4P = copyP->Copy();
			/*
			 * Peek data to get the option type as well as length and segmentsLeft field
			 */
			uint32_t size = copyP->GetSize();
			uint8_t* data = new uint8_t[size];
			copyP->CopyData(data, size);

			uint8_t optionType = 0;
			optionType = *(data);

			if (optionType == 3)
			{
				Ptr<dsr::DsrOptions> dsrOption;
				DsrOptionHeader dsrOptionHeader;
				uint8_t errorType = *(data + 2);

				if (errorType == 1) // This is the Route Error Option
				{
					DsrOptionRerrUnreachHeader rerr;
					copyP->RemoveHeader(rerr);
					NS_ASSERT(copyP->GetSize() == 0);

					DsrOptionRerrUnreachHeader newUnreach;
					newUnreach.SetErrorType(1);
					newUnreach.SetErrorSrc(rerr.GetErrorSrc());
					newUnreach.SetUnreachNode(rerr.GetUnreachNode());
					newUnreach.SetErrorDst(rerr.GetErrorDst());
					newUnreach.SetSalvage(rerr.GetSalvage()); // Set the value about whether to
															  // salvage a packet or not

					DsrOptionSRHeader sourceRoute;
					std::vector<Ipv4Address> errorRoute = toDst.GetVector();
					sourceRoute.SetNodesAddress(errorRoute);
					/// When found a route and use it, UseExtends to the link cache
					if (m_routeCache->IsLinkCache())
					{
						m_routeCache->UseExtends(errorRoute);
					}
					sourceRoute.SetSegmentsLeft((errorRoute.size() - 2));
					uint8_t salvage = 0;
					sourceRoute.SetSalvage(salvage);
					Ipv4Address nextHop = SearchNextHop(m_mainAddress, errorRoute); // Get the next hop address

					if (nextHop == "0.0.0.0")
					{
						PacketNewRoute(dsrPacket, m_mainAddress, destination, protocol);
						return;
					}

					SetRoute(nextHop, m_mainAddress);
					uint8_t length = (sourceRoute.GetLength() + newUnreach.GetLength());
					dsrRoutingHeader.SetNextHeader(protocol);
					dsrRoutingHeader.SetMessageType(1);
					dsrRoutingHeader.SetSourceId(GetIDfromIP(m_mainAddress));
					dsrRoutingHeader.SetDestId(255);
					dsrRoutingHeader.SetPayloadLength(uint16_t(length) + 4);
					dsrRoutingHeader.AddDsrOption(newUnreach);
					dsrRoutingHeader.AddDsrOption(sourceRoute);

					Ptr<Packet> newPacket = Create<Packet>();
					newPacket->AddHeader(dsrRoutingHeader); // Add the routing header with rerr and
															// sourceRoute attached to it
					Ptr<NetDevice> dev = m_ip->GetNetDevice(m_ip->GetInterfaceForAddress(m_mainAddress));
					m_ipv4Route->SetOutputDevice(dev);

					uint32_t priority = GetPriority(DSR_CONTROL_PACKET); /// This will be priority 0
					std::map<uint32_t, Ptr<dsr::DsrNetworkQueue>>::iterator i = m_priorityQueue.find(priority);
					Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
					NS_LOG_LOGIC("Will be inserting into priority queue number: " << priority);

					// m_downTarget (newPacket, m_mainAddress, nextHop, GetProtocolNumber (),
					// m_ipv4Route);

					/// \todo New DsrNetworkQueueEntry
					DsrNetworkQueueEntry newEntry(newPacket, m_mainAddress, nextHop, Simulator::Now(), m_ipv4Route);

					if (dsrNetworkQueue->Enqueue(newEntry))
					{
						Scheduler(priority);
					}
					else
					{
						NS_LOG_INFO("Packet dropped as dsr network queue is full");
					}
				}
			}
			else
			{
				dsrRoutingHeader.SetNextHeader(protocol);
				dsrRoutingHeader.SetMessageType(2);
				dsrRoutingHeader.SetSourceId(GetIDfromIP(m_mainAddress));
				dsrRoutingHeader.SetDestId(GetIDfromIP(destination));

				DsrOptionSRHeader sourceRoute;
				std::vector<Ipv4Address> nodeList = toDst.GetVector(); // Get the route from the route entry we found
				Ipv4Address nextHop = SearchNextHop(m_mainAddress,
													nodeList); // Get the next hop address for the route
				if (nextHop == "0.0.0.0")
				{
					PacketNewRoute(dsrPacket, m_mainAddress, destination, protocol);
					return;
				}
				uint8_t salvage = 0;
				sourceRoute.SetNodesAddress(nodeList); // Save the whole route in the source route header of the packet
				sourceRoute.SetSegmentsLeft(
					(nodeList.size() - 2)); // The segmentsLeft field will indicate the hops to go
				sourceRoute.SetSalvage(salvage);
				/// When found a route and use it, UseExtends to the link cache
				if (m_routeCache->IsLinkCache())
				{
					m_routeCache->UseExtends(nodeList);
				}
				uint8_t length = sourceRoute.GetLength();
				dsrRoutingHeader.SetPayloadLength(uint16_t(length) + 2);
				dsrRoutingHeader.AddDsrOption(sourceRoute);
				cleanP->AddHeader(dsrRoutingHeader);
				Ptr<const Packet> mtP = cleanP->Copy();
				// Put the data packet in the maintenance queue for data packet retransmission
				DsrMaintainBuffEntry newEntry(/*packet=*/mtP,
											  /*ourAddress=*/m_mainAddress,
											  /*nextHop=*/nextHop,
											  /*src=*/m_mainAddress,
											  /*dst=*/destination,
											  /*ackId=*/0,
											  /*segsLeft=*/nodeList.size() - 2,
											  /*expire=*/m_maxMaintainTime);
				bool result = m_maintainBuffer.Enqueue(newEntry); // Enqueue the packet the the maintenance buffer
				if (result)
				{
					NetworkKey networkKey;
					networkKey.m_ackId = newEntry.GetAckId();
					networkKey.m_ourAdd = newEntry.GetOurAdd();
					networkKey.m_nextHop = newEntry.GetNextHop();
					networkKey.m_source = newEntry.GetSrc();
					networkKey.m_destination = newEntry.GetDst();

					PassiveKey passiveKey;
					passiveKey.m_ackId = 0;
					passiveKey.m_source = newEntry.GetSrc();
					passiveKey.m_destination = newEntry.GetDst();
					passiveKey.m_segsLeft = newEntry.GetSegsLeft();

					LinkKey linkKey;
					linkKey.m_source = newEntry.GetSrc();
					linkKey.m_destination = newEntry.GetDst();
					linkKey.m_ourAdd = newEntry.GetOurAdd();
					linkKey.m_nextHop = newEntry.GetNextHop();

					m_addressForwardCnt[networkKey] = 0;
					m_passiveCnt[passiveKey] = 0;
					m_linkCnt[linkKey] = 0;

					if (m_linkAck)
					{
						ScheduleLinkPacketRetry(newEntry, protocol);
					}
					else
					{
						NS_LOG_LOGIC("Not using link acknowledgment");
						if (nextHop != destination)
						{
							SchedulePassivePacketRetry(newEntry, protocol);
						}
						else
						{
							// This is the first network retry
							ScheduleNetworkPacketRetry(newEntry, true, protocol);
						}
					}
				}
				// we need to suspend the normal timer that checks the send buffer
				// until we are done sending packets
				if (!m_sendBuffTimer.IsSuspended())
				{
					m_sendBuffTimer.Suspend();
				}
				Simulator::Schedule(m_sendBuffInterval, &DsrRouting::SendBuffTimerExpire, this);
				return;
			}
		}
		else
		{
			++i;
		}
	}
	// after going through the entire send buffer and send all packets found route,
	// we need to resume the timer if it has been suspended
	if (m_sendBuffTimer.IsSuspended())
	{
		NS_LOG_DEBUG("Resume the send buffer timer");
		m_sendBuffTimer.Resume();
	}
}

bool
DsrRouting::PromiscReceive(Ptr<NetDevice> device,
						   Ptr<const Packet> packet,
						   uint16_t protocol,
						   const Address& from,
						   const Address& to,
						   NetDevice::PacketType packetType)
{
	if (protocol != Ipv4L3Protocol::PROT_NUMBER)
	{
		return false;
	}

	// Remove the ipv4 header here
	Ptr<Packet> pktMinusIpHdr = packet->Copy();
	Ipv4Header ipv4Header;
	pktMinusIpHdr->RemoveHeader(ipv4Header);
	Ipv4Address source_addr = ipv4Header.GetSource();
	// Ipv4Address destination_addr = ipv4Header.GetDestination();

	if (ipv4Header.GetProtocol() != DsrRouting::PROT_NUMBER)
	{
		return false;
	}
	NS_LOG_DEBUG("In PromiscReceive function");
	// NS_LOG_DEBUG("source_addr: " << source_addr);
	// NS_LOG_DEBUG("destination_addr: " << destination_addr);

	// Remove the dsr routing header here
	Ptr<Packet> pktMinusDsrHdr = pktMinusIpHdr->Copy();
	DsrRoutingHeader dsrRouting;
	pktMinusDsrHdr->RemoveHeader(dsrRouting);

	/*
	 * Message type 2 means the data packet, we will further process the data
	 * packet for delivery notification, safely ignore control packet
	 * Another check here is our own address, if this is the data destinated for us,
	 * process it further, otherwise, just ignore it
	 */
	Ipv4Address ourAddress = m_ipv4->GetAddress(1, 0).GetLocal();
	// check if the message type is 2 and if the ipv4 address matches
	if (dsrRouting.GetMessageType() == 2 && ourAddress == m_mainAddress)
	{
		NS_LOG_DEBUG("data packet receives " << packet->GetUid());
		/// This is the ip address we just received data packet from
		Ipv4Address previousHop = GetIPfromMAC(Mac48Address::ConvertFrom(from));

		Ptr<Packet> p = Create<Packet>();
		// Here the segments left value need to plus one to check the earlier hop maintain buffer
		// entry
		DsrMaintainBuffEntry newEntry;
		newEntry.SetPacket(p);
		newEntry.SetSrc(source_addr);

		Ipv4Address destinationIp = GetIPfromID(dsrRouting.GetDestId());
		newEntry.SetDst(destinationIp);

		/// Remember this is the entry for previous node
		newEntry.SetOurAdd(previousHop);

		int interface_id = m_ipv4->GetInterfaceForDevice(device);
		ourAddress = m_ipv4->GetAddress(interface_id, 0).GetLocal();
		newEntry.SetNextHop(ourAddress);

		NS_LOG_DEBUG("source: " << source_addr);
		NS_LOG_DEBUG("destination: " << destinationIp);
		NS_LOG_DEBUG("prev ourAddress: " << previousHop);
		NS_LOG_DEBUG("prev nextHop: " << ourAddress);

		/// Get the previous node's maintenance buffer and passive ack
		Ptr<Node> node = GetNodeWithAddress(previousHop);

		Ptr<dsr::DsrRouting> dsr = node->GetObject<dsr::DsrRouting>();
		// dsr->CancelLinkPacketTimer(newEntry);
		dsr->CancelLinkPacketTimer_with_all_ip(newEntry);
	}

	// Receive only IP packets and packets destined for other hosts
	// FIXME: causes route error, not using this for now
	// if (packetType == NetDevice::PACKET_OTHERHOST)
	//{
	//	// just to minimize debug output
	//	NS_LOG_INFO(this << from << to << packetType << *pktMinusIpHdr);

	//	uint8_t offset = dsrRouting.GetDsrOptionsOffset(); // Get the offset for option header, 4 bytes in this case
	//	uint8_t nextHeader = dsrRouting.GetNextHeader();
	//	uint32_t sourceId = dsrRouting.GetSourceId();
	//	Ipv4Address source = GetIPfromID(sourceId);

	//	// This packet is used to peek option type
	//	pktMinusIpHdr->RemoveAtStart(offset);
	//	/*
	//	 * Peek data to get the option type as well as length and segmentsLeft field
	//	 */
	//	uint32_t size = pktMinusIpHdr->GetSize();
	//	uint8_t* data = new uint8_t[size];
	//	pktMinusIpHdr->CopyData(data, size);
	//	uint8_t optionType = 0;
	//	optionType = *(data);

	//	Ptr<dsr::DsrOptions> dsrOption;

	//	if (optionType == 96) // This is the source route option
	//	{
	//		Ipv4Address promiscSource = GetIPfromMAC(Mac48Address::ConvertFrom(from));
	//		dsrOption = GetOption(optionType); // Get the relative DSR option and demux to the process function
	//		NS_LOG_DEBUG(Simulator::Now().As(Time::S)
	//					 << " DSR node " << m_mainAddress << " overhearing packet PID: " << pktMinusIpHdr->GetUid()
	//					 << " from " << promiscSource << " to " << GetIPfromMAC(Mac48Address::ConvertFrom(to))
	//					 << " with source IP " << ipv4Header.GetSource() << " and destination IP "
	//					 << ipv4Header.GetDestination() << " and packet : " << *pktMinusDsrHdr);

	//		bool isPromisc = true; // Set the boolean value isPromisc as true
	//		dsrOption->Process(pktMinusIpHdr,
	//						   pktMinusDsrHdr,
	//						   m_mainAddress,
	//						   source,
	//						   ipv4Header,
	//						   nextHeader,
	//						   isPromisc,
	//						   promiscSource);
	//		return true;
	//	}
	//}
	return false;
}

void
DsrRouting::PacketNewRoute(Ptr<Packet> packet, Ipv4Address source, Ipv4Address destination, uint8_t protocol)
{
	NS_LOG_FUNCTION(this << packet << source << destination << (uint32_t)protocol);
	// FIXME: BUG, just return for now
	// NS_LOG_UNCOND("DsrRouting::PacketNewRoute, should not happen in normal situation. Return for now");
	NS_ASSERT_MSG(false, "DsrRouting::PacketNewRoute, should not happen in normal situation");

	// Look up routes for the specific destination
	DsrRouteCacheEntry toDst;
	auto [findRoute, matched_dst_addr] = Lookup_destination_route_with_all_ip(destination, toDst);

	destination = matched_dst_addr;
	// bool findRoute = m_routeCache->LookupRoute(destination, toDst);

	// Queue the packet if there is no route pre-existing
	if (!findRoute)
	{
		NS_LOG_INFO(Simulator::Now().As(Time::S)
					<< " " << m_mainAddress << " there is no route for this packet, queue the packet");

		Ptr<Packet> p = packet->Copy();
		DsrSendBuffEntry newEntry(p, destination, m_sendBufferTimeout,
								  protocol);		  // Create a new entry for send buffer
		bool result = m_sendBuffer.Enqueue(newEntry); // Enqueue the packet in send buffer
		if (result)
		{
			NS_LOG_INFO(Simulator::Now().As(Time::S)
						<< " Add packet PID: " << packet->GetUid() << " to queue. Packet: " << *packet);

			NS_LOG_LOGIC("Send RREQ to" << destination);
			if ((m_addressReqTimer.find(destination) == m_addressReqTimer.end()) &&
				(m_nonPropReqTimer.find(destination) == m_nonPropReqTimer.end()))
			{
				/*
				 * Call the send request function, it will update the request table entry and ttl
				 * there
				 */
				SendInitialRequest(source, destination, protocol);
			}
		}
	}
	else
	{
		Ptr<Packet> cleanP = packet->Copy();
		DsrRoutingHeader dsrRoutingHeader;
		dsrRoutingHeader.SetNextHeader(protocol);
		dsrRoutingHeader.SetMessageType(2);
		dsrRoutingHeader.SetSourceId(GetIDfromIP(source));
		dsrRoutingHeader.SetDestId(GetIDfromIP(destination));

		DsrOptionSRHeader sourceRoute;
		std::vector<Ipv4Address> nodeList = toDst.GetVector(); // Get the route from the route entry we found
		// Get the next hop address for the route
		Ipv4Address nextHop = SearchNextHop_with_all_ip(m_mainAddress, nodeList);
		// Ipv4Address nextHop = SearchNextHop(m_mainAddress, nodeList);
		if (nextHop == "0.0.0.0")
		{
			NS_ASSERT_MSG(false, "DsrRouting::PacketNewRoute, should not happen in normal situation");
			PacketNewRoute(cleanP, source, destination, protocol);
			return;
		}
		uint8_t salvage = 0;
		sourceRoute.SetNodesAddress(nodeList); // Save the whole route in the source route header of the packet
		/// When found a route and use it, UseExtends to the link cache
		if (m_routeCache->IsLinkCache())
		{
			m_routeCache->UseExtends(nodeList);
		}
		sourceRoute.SetSegmentsLeft((nodeList.size() - 2)); // The segmentsLeft field will indicate the hops to go
		sourceRoute.SetSalvage(salvage);

		uint8_t length = sourceRoute.GetLength();
		dsrRoutingHeader.SetPayloadLength(uint16_t(length) + 2);
		dsrRoutingHeader.AddDsrOption(sourceRoute);
		cleanP->AddHeader(dsrRoutingHeader);
		Ptr<const Packet> mtP = cleanP->Copy();
		SetRoute(nextHop, m_mainAddress);
		// Put the data packet in the maintenance queue for data packet retransmission
		DsrMaintainBuffEntry newEntry(/*packet=*/mtP,
									  /*ourAddress=*/m_mainAddress,
									  /*nextHop=*/nextHop,
									  /*src=*/source,
									  /*dst=*/destination,
									  /*ackId=*/0,
									  /*segsLeft=*/nodeList.size() - 2,
									  /*expire=*/m_maxMaintainTime);
		bool result = m_maintainBuffer.Enqueue(newEntry); // Enqueue the packet the the maintenance buffer

		if (result)
		{
			NetworkKey networkKey;
			networkKey.m_ackId = newEntry.GetAckId();
			networkKey.m_ourAdd = newEntry.GetOurAdd();
			networkKey.m_nextHop = newEntry.GetNextHop();
			networkKey.m_source = newEntry.GetSrc();
			networkKey.m_destination = newEntry.GetDst();

			PassiveKey passiveKey;
			passiveKey.m_ackId = 0;
			passiveKey.m_source = newEntry.GetSrc();
			passiveKey.m_destination = newEntry.GetDst();
			passiveKey.m_segsLeft = newEntry.GetSegsLeft();

			LinkKey linkKey;
			linkKey.m_source = newEntry.GetSrc();
			linkKey.m_destination = newEntry.GetDst();
			linkKey.m_ourAdd = newEntry.GetOurAdd();
			linkKey.m_nextHop = newEntry.GetNextHop();

			m_addressForwardCnt[networkKey] = 0;
			m_passiveCnt[passiveKey] = 0;
			m_linkCnt[linkKey] = 0;

			if (m_linkAck)
			{
				ScheduleLinkPacketRetry(newEntry, protocol);
			}
			else
			{
				NS_LOG_LOGIC("Not using link acknowledgment");
				if (nextHop != destination)
				{
					SchedulePassivePacketRetry(newEntry, protocol);
				}
				else
				{
					// This is the first network retry
					ScheduleNetworkPacketRetry(newEntry, true, protocol);
				}
			}
		}
	}
}

void
DsrRouting::SendUnreachError(Ipv4Address unreachNode,
							 Ipv4Address destination,
							 Ipv4Address originalDst,
							 uint8_t salvage,
							 uint8_t protocol)
{
	NS_LOG_FUNCTION(this << unreachNode << destination << originalDst << (uint32_t)salvage << (uint32_t)protocol);
	DsrRoutingHeader dsrRoutingHeader;
	dsrRoutingHeader.SetNextHeader(protocol);
	dsrRoutingHeader.SetMessageType(1);
	dsrRoutingHeader.SetSourceId(GetIDfromIP(m_mainAddress));
	dsrRoutingHeader.SetDestId(GetIDfromIP(destination));

	DsrOptionRerrUnreachHeader rerrUnreachHeader;
	rerrUnreachHeader.SetErrorType(1);
	rerrUnreachHeader.SetErrorSrc(m_mainAddress);
	rerrUnreachHeader.SetUnreachNode(unreachNode);
	rerrUnreachHeader.SetErrorDst(destination);
	rerrUnreachHeader.SetOriginalDst(originalDst);
	rerrUnreachHeader.SetSalvage(salvage); // Set the value about whether to salvage a packet or not
	uint8_t rerrLength = rerrUnreachHeader.GetLength();

	if (GetIDfromIP(destination) == GetIDfromIP(m_mainAddress))
	{
		NS_LOG_INFO("We are the error source, send request to original dst " << originalDst);
		// Send error request message if we are the source node
		SendErrorRequest(rerrUnreachHeader, protocol);
		return;
	}
	DsrRouteCacheEntry toDst;
	// bool findRoute = m_routeCache->LookupRoute(destination, toDst);
	auto [findRoute, matched_dst_addr] = Lookup_destination_route_with_all_ip(destination, toDst);
	// Queue the packet if there is no route pre-existing
	Ptr<Packet> newPacket = Create<Packet>();
	if (!findRoute)
	{
		NS_LOG_INFO(Simulator::Now().As(Time::S)
					<< " " << m_mainAddress << " there is no route for this packet, queue the packet");

		dsrRoutingHeader.SetPayloadLength(rerrLength + 2);
		dsrRoutingHeader.AddDsrOption(rerrUnreachHeader);
		newPacket->AddHeader(dsrRoutingHeader);
		Ptr<Packet> p = newPacket->Copy();
		// Save the error packet in the error buffer
		DsrErrorBuffEntry newEntry(p, destination, m_mainAddress, unreachNode, m_sendBufferTimeout, protocol);
		bool result = m_errorBuffer.Enqueue(newEntry); // Enqueue the packet in send buffer
		if (result)
		{
			NS_LOG_INFO(Simulator::Now().As(Time::S)
						<< " Add packet PID: " << p->GetUid() << " to queue. Packet: " << *p);
			NS_LOG_LOGIC("Send RREQ to" << destination);
			if ((m_addressReqTimer.find(destination) == m_addressReqTimer.end()) &&
				(m_nonPropReqTimer.find(destination) == m_nonPropReqTimer.end()))
			{
				NS_LOG_DEBUG("When there is no existing route request for " << destination << ", initialize one");
				/*
				 * Call the send request function, it will update the request table entry and
				 * ttl there
				 */
				SendInitialRequest(m_mainAddress, destination, protocol);
			}
		}
	}
	else
	{
		std::vector<Ipv4Address> nodeList = toDst.GetVector();
		// PrintVector(nodeList);
		Ipv4Address nextHop = SearchNextHop_with_all_ip(m_mainAddress, nodeList);
		if (nextHop == "0.0.0.0")
		{
			NS_LOG_DEBUG("The route is not right");
			PacketNewRoute(newPacket, m_mainAddress, destination, protocol);
			return;
		}
		DsrOptionSRHeader sourceRoute;
		sourceRoute.SetNodesAddress(nodeList);
		/// When found a route and use it, UseExtends to the link cache
		if (m_routeCache->IsLinkCache())
		{
			m_routeCache->UseExtends(nodeList);
		}
		sourceRoute.SetSegmentsLeft((nodeList.size() - 2));
		uint8_t srLength = sourceRoute.GetLength();
		uint8_t length = (srLength + rerrLength);

		dsrRoutingHeader.SetPayloadLength(uint16_t(length) + 4);
		dsrRoutingHeader.AddDsrOption(rerrUnreachHeader);
		dsrRoutingHeader.AddDsrOption(sourceRoute);
		newPacket->AddHeader(dsrRoutingHeader);

		SetRoute(nextHop, m_mainAddress);
		Ptr<NetDevice> dev = m_ip->GetNetDevice(m_ip->GetInterfaceForAddress(m_mainAddress));
		m_ipv4Route->SetOutputDevice(dev);
		NS_LOG_INFO("Send the packet to the next hop address " << nextHop << " from " << m_mainAddress
															   << " with the size " << newPacket->GetSize());

		uint32_t priority = GetPriority(DSR_CONTROL_PACKET);
		std::map<uint32_t, Ptr<dsr::DsrNetworkQueue>>::iterator i = m_priorityQueue.find(priority);
		Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
		NS_LOG_DEBUG("Will be inserting into priority queue " << dsrNetworkQueue << " number: " << priority);

		// m_downTarget (newPacket, m_mainAddress, nextHop, GetProtocolNumber (), m_ipv4Route);

		/// \todo New DsrNetworkQueueEntry
		DsrNetworkQueueEntry newEntry(newPacket, m_mainAddress, nextHop, Simulator::Now(), m_ipv4Route);

		if (dsrNetworkQueue->Enqueue(newEntry))
		{
			Scheduler(priority);
		}
		else
		{
			NS_LOG_INFO("Packet dropped as dsr network queue is full");
		}
	}
}

void
DsrRouting::ForwardErrPacket(DsrOptionRerrUnreachHeader& rerr,
							 DsrOptionSRHeader& sourceRoute,
							 Ipv4Address nextHop,
							 uint8_t protocol,
							 Ptr<Ipv4Route> route)
{
	NS_LOG_FUNCTION(this << rerr << sourceRoute << nextHop << (uint32_t)protocol << route);
	NS_ASSERT_MSG(!m_downTarget.IsNull(), "Error, DsrRouting cannot send downward");
	DsrRoutingHeader dsrRoutingHeader;
	dsrRoutingHeader.SetNextHeader(protocol);
	dsrRoutingHeader.SetMessageType(1);
	dsrRoutingHeader.SetSourceId(GetIDfromIP(rerr.GetErrorSrc()));
	dsrRoutingHeader.SetDestId(GetIDfromIP(rerr.GetErrorDst()));

	uint8_t length = (sourceRoute.GetLength() + rerr.GetLength());
	dsrRoutingHeader.SetPayloadLength(uint16_t(length) + 4);
	dsrRoutingHeader.AddDsrOption(rerr);
	dsrRoutingHeader.AddDsrOption(sourceRoute);
	Ptr<Packet> packet = Create<Packet>();
	packet->AddHeader(dsrRoutingHeader);
	Ptr<NetDevice> dev = m_ip->GetNetDevice(m_ip->GetInterfaceForAddress(m_mainAddress));
	route->SetOutputDevice(dev);

	uint32_t priority = GetPriority(DSR_CONTROL_PACKET);
	std::map<uint32_t, Ptr<dsr::DsrNetworkQueue>>::iterator i = m_priorityQueue.find(priority);
	Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
	NS_LOG_DEBUG("Will be inserting into priority queue " << dsrNetworkQueue << " number: " << priority);

	// m_downTarget (packet, m_mainAddress, nextHop, GetProtocolNumber (), route);

	/// \todo New DsrNetworkQueueEntry
	DsrNetworkQueueEntry newEntry(packet, m_mainAddress, nextHop, Simulator::Now(), route);

	if (dsrNetworkQueue->Enqueue(newEntry))
	{
		Scheduler(priority);
	}
	else
	{
		NS_LOG_INFO("Packet dropped as dsr network queue is full");
	}
}

void
DsrRouting::SendBroadcast(Ptr<Packet> packet, Ipv4Address source)
{
	NS_LOG_FUNCTION(this << packet << source);

	NS_ASSERT_MSG(!m_downTarget.IsNull(), "Error, DsrRouting cannot send downward");
	// send single hop broadcast packets
	/*
	 * The destination address here is global broadcast address
	 */
	uint32_t priority = GetPriority(DSR_CONTROL_PACKET);
	std::map<uint32_t, Ptr<dsr::DsrNetworkQueue>>::iterator i = m_priorityQueue.find(priority);
	Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
	NS_LOG_LOGIC("Inserting into priority queue number: " << priority);

	DsrNetworkQueueEntry newEntry(packet, source, Ipv4Address::GetBroadcast(), Simulator::Now(), nullptr);
	if (dsrNetworkQueue->Enqueue(newEntry))
	{
		Scheduler(priority);
	}
	else
	{
		NS_LOG_INFO("Packet dropped as dsr network queue is full");
	}
}

void
DsrRouting::Send(Ptr<Packet> packet,
				 Ipv4Address source,
				 Ipv4Address destination,
				 uint8_t protocol,
				 Ptr<Ipv4Route> route)
{
	NS_LOG_FUNCTION(this << packet << source << destination << (uint32_t)protocol << route);
	NS_ASSERT_MSG(!m_downTarget.IsNull(), "Error, DsrRouting cannot send downward");

	if (protocol == 1)
	{
		NS_LOG_INFO("Drop packet. Not handling ICMP packet for now");
	}
	else if (m_enable_global_broadcast && destination == Ipv4Address::GetBroadcast())
	{
		NS_LOG_LOGIC("Send a global broadcast packet from " << source);
		Ptr<Packet> cleanP = packet->Copy();
		DsrRoutingHeader dsrRoutingHeader;
		dsrRoutingHeader.SetNextHeader(protocol);
		dsrRoutingHeader.SetMessageType(2);
		dsrRoutingHeader.SetSourceId(GetIDfromIP(source));
		dsrRoutingHeader.SetDestId(GetIDfromIP(destination));

		DsrOptionSRHeader sourceRoute;
		std::vector<Ipv4Address> nodeList;
		Ipv4Address nextHop = Ipv4Address::GetBroadcast();
		nodeList.push_back(m_mainAddress);
		nodeList.push_back(nextHop);

		uint8_t salvage = 0;
		// Save the whole route in the source route header of the packet
		sourceRoute.SetNodesAddress(nodeList);
		// The segmentsLeft field will indicate the hops to go
		sourceRoute.SetSegmentsLeft(0);
		sourceRoute.SetSalvage(salvage);

		uint8_t length = sourceRoute.GetLength();
		dsrRoutingHeader.SetPayloadLength(uint16_t(length) + 2);
		dsrRoutingHeader.AddDsrOption(sourceRoute);
		cleanP->AddHeader(dsrRoutingHeader);

		DsrSendBuffEntry newEntry(cleanP,
								  destination,
								  m_sendBufferTimeout,
								  protocol);		  // Create a new entry for send buffer
		bool result = m_sendBuffer.Enqueue(newEntry); // Enqueue the packet in send buffer
		if (result)
		{
			// send broadcast packet
			Simulator::ScheduleNow(&DsrRouting::SendPacketFromBuffer, this, sourceRoute, nextHop, protocol);
		}
	}
	else
	{
		// Look up routes for the specific destination
		DsrRouteCacheEntry toDst;
		auto [findRoute, matched_dst_addr] = Lookup_destination_route_with_all_ip(destination, toDst);
		// Queue the packet if there is no route pre-existing
		if (!findRoute)
		{
			NS_LOG_INFO(Simulator::Now().As(Time::S) << " there is no route for this packet, queue the packet");

			Ipv4Address destination_main = get_same_channel_Ipv4Address(m_mainAddress, destination);
			Ptr<Packet> p = packet->Copy();
			DsrSendBuffEntry newEntry(p,
									  destination_main,
									  // destination,
									  m_sendBufferTimeout,
									  protocol);		  // Create a new entry for send buffer
			bool result = m_sendBuffer.Enqueue(newEntry); // Enqueue the packet in send buffer
			if (result)
			{
				NS_LOG_INFO(Simulator::Now().As(Time::S)
							<< " Add packet PID: " << packet->GetUid() << " to send buffer. Packet: " << *packet);
				// Only when there is no existing route request timer when new route request is
				// scheduled
				if ((m_addressReqTimer.find(destination) == m_addressReqTimer.end()) &&
					(m_nonPropReqTimer.find(destination) == m_nonPropReqTimer.end()))
				{
					// Call the send request function, it will update the request table entry and ttl value
					NS_LOG_LOGIC("Send initial RREQ to " << destination);
					SendInitialRequest(source, destination, protocol);
				}
				else
				{
					NS_LOG_LOGIC("There is existing route request timer with request count "
								 << m_rreqTable->GetRreqCnt(destination));
				}
			}
		}
		else
		{
			NS_LOG_DEBUG("Found a route to destiantion");
			Ptr<Packet> cleanP = packet->Copy();
			DsrRoutingHeader dsrRoutingHeader;
			dsrRoutingHeader.SetNextHeader(protocol);
			dsrRoutingHeader.SetMessageType(2);
			dsrRoutingHeader.SetSourceId(GetIDfromIP(source));
			dsrRoutingHeader.SetDestId(GetIDfromIP(destination));

			DsrOptionSRHeader sourceRoute;
			std::vector<Ipv4Address> nodeList = toDst.GetVector(); // Get the route from the route entry we found

			// Get the next hop address for the route
			Ipv4Address nextHop;
			Ipv4Address ourAddress;
			if (nodeList.size() == 2)
			{
				ourAddress = nodeList[0];
				nextHop = nodeList[1];
			}
			else
			{
				Ptr<Node> node = GetNodeWithAddress(m_mainAddress);
				Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
				unsigned int net_card_num = node->GetNDevices() - 1;
				for (uint32_t i = 1; i <= net_card_num; ++i)
				{
					ourAddress = ipv4->GetAddress(i, 0).GetLocal();
					nextHop = SearchNextHop(ourAddress, nodeList);
					if (nextHop != "0.0.0.0")
					{
						break;
					}
				}
			}

			if (nextHop == "0.0.0.0")
			{
				PacketNewRoute(cleanP, source, destination, protocol);
				return;
			}
			uint8_t salvage = 0;
			sourceRoute.SetNodesAddress(nodeList); // Save the whole route in the source route header of the packet
			/// When found a route and use it, UseExtends to the link cache
			if (m_routeCache->IsLinkCache())
			{
				m_routeCache->UseExtends(nodeList);
			}
			sourceRoute.SetSegmentsLeft((nodeList.size() - 2)); // The segmentsLeft field will indicate the hops to go
			sourceRoute.SetSalvage(salvage);

			uint8_t length = sourceRoute.GetLength();

			dsrRoutingHeader.SetPayloadLength(uint16_t(length) + 2);
			dsrRoutingHeader.AddDsrOption(sourceRoute);
			cleanP->AddHeader(dsrRoutingHeader);

			Ptr<const Packet> mtP = cleanP->Copy();
			NS_LOG_DEBUG("maintain packet size " << cleanP->GetSize());
			// Put the data packet in the maintenance queue for data packet retransmission
			// DsrMaintainBuffEntry newEntry(/*packet=*/mtP,
			//                              /*ourAddress=*/m_mainAddress,
			//                              /*nextHop=*/nextHop,
			//                              /*src=*/source,
			//                              /*dst=*/destination,
			//                              /*ackId=*/0,
			//                              /*segsLeft=*/nodeList.size() - 2,
			//                              /*expire=*/m_maxMaintainTime);
			DsrMaintainBuffEntry newEntry(/*packet=*/mtP,
										  /*ourAddress=*/ourAddress,
										  /*nextHop=*/nextHop,
										  /*src=*/source,
										  /*dst=*/destination, // or matched_dst_addr,
										  /*ackId=*/0,
										  /*segsLeft=*/nodeList.size() - 2,
										  /*expire=*/m_maxMaintainTime);
			NS_LOG_DEBUG("source: " << source << " destination: " << destination);
			NS_LOG_DEBUG("ourAddress: " << ourAddress << " nextHop: " << nextHop);
			bool result = m_maintainBuffer.Enqueue(newEntry); // Enqueue the packet the the maintenance buffer
			if (result)
			{
				NetworkKey networkKey;
				networkKey.m_ackId = newEntry.GetAckId();
				networkKey.m_ourAdd = newEntry.GetOurAdd();
				networkKey.m_nextHop = newEntry.GetNextHop();
				networkKey.m_source = newEntry.GetSrc();
				networkKey.m_destination = newEntry.GetDst();

				PassiveKey passiveKey;
				passiveKey.m_ackId = 0;
				passiveKey.m_source = newEntry.GetSrc();
				passiveKey.m_destination = newEntry.GetDst();
				passiveKey.m_segsLeft = newEntry.GetSegsLeft();

				LinkKey linkKey;
				linkKey.m_source = newEntry.GetSrc();
				linkKey.m_destination = newEntry.GetDst();
				linkKey.m_ourAdd = newEntry.GetOurAdd();
				linkKey.m_nextHop = newEntry.GetNextHop();

				m_addressForwardCnt[networkKey] = 0;
				m_passiveCnt[passiveKey] = 0;
				m_linkCnt[linkKey] = 0;

				if (m_linkAck)
				{
					ScheduleLinkPacketRetry(newEntry, protocol);
				}
				else
				{
					NS_LOG_LOGIC("Not using link acknowledgment");
					if (nextHop != destination)
					{
						SchedulePassivePacketRetry(newEntry, protocol);
					}
					else
					{
						// This is the first network retry
						ScheduleNetworkPacketRetry(newEntry, true, protocol);
					}
				}
			}

			if (m_sendBuffer.GetSize() != 0 && m_sendBuffer.Find(destination))
			{
				// Try to send packet from *previously* queued entries from send buffer if any
				Simulator::Schedule(MilliSeconds(m_uniformRandomVariable->GetInteger(0, 100)),
									&DsrRouting::SendPacketFromBuffer,
									this,
									sourceRoute,
									nextHop,
									protocol);
			}
		}
	}
}

uint16_t
DsrRouting::AddAckReqHeader(Ptr<Packet>& packet, Ipv4Address nextHop)
{
	NS_LOG_FUNCTION(this << packet << nextHop);
	// This packet is used to peek option type
	Ptr<Packet> dsrP = packet->Copy();
	Ptr<Packet> tmpP = packet->Copy();

	DsrRoutingHeader dsrRoutingHeader;
	dsrP->RemoveHeader(dsrRoutingHeader); // Remove the DSR header in whole
	uint8_t protocol = dsrRoutingHeader.GetNextHeader();
	uint32_t sourceId = dsrRoutingHeader.GetSourceId();
	uint32_t destinationId = dsrRoutingHeader.GetDestId();
	uint32_t offset = dsrRoutingHeader.GetDsrOptionsOffset();
	tmpP->RemoveAtStart(offset); // Here the processed size is 8 bytes, which is the fixed sized extension header

	// Get the number of routers' address field
	uint8_t buf[2];
	tmpP->CopyData(buf, sizeof(buf));
	uint8_t numberAddress = (buf[1] - 2) / 4;
	DsrOptionSRHeader sourceRoute;
	sourceRoute.SetNumberAddress(numberAddress);
	tmpP->RemoveHeader(sourceRoute); // this is a clean packet without any dsr involved headers

	DsrOptionAckReqHeader ackReq;
	m_ackId = m_routeCache->CheckUniqueAckId(nextHop);
	ackReq.SetAckId(m_ackId);
	uint8_t length = (sourceRoute.GetLength() + ackReq.GetLength());
	DsrRoutingHeader newDsrRoutingHeader;
	newDsrRoutingHeader.SetNextHeader(protocol);
	newDsrRoutingHeader.SetMessageType(2);
	newDsrRoutingHeader.SetSourceId(sourceId);
	newDsrRoutingHeader.SetDestId(destinationId);
	newDsrRoutingHeader.SetPayloadLength(length + 4);
	newDsrRoutingHeader.AddDsrOption(sourceRoute);
	newDsrRoutingHeader.AddDsrOption(ackReq);
	dsrP->AddHeader(newDsrRoutingHeader);
	// give the dsrP value to packet and then return
	packet = dsrP;
	return m_ackId;
}

void
DsrRouting::SendPacket(Ptr<Packet> packet, Ipv4Address source, Ipv4Address nextHop, uint8_t protocol)
{
	NS_LOG_FUNCTION(this << packet << source << nextHop << (uint32_t)protocol);
	// Send out the data packet
	// Ptr<NetDevice> dev = m_ip->GetNetDevice(m_ip->GetInterfaceForAddress(m_mainAddress));
	Ptr<NetDevice> dev;
	int interface_num = GetNodeWithAddress(nextHop)->GetObject<Ipv4>()->GetInterfaceForAddress(nextHop);
	Ipv4Address channel_hop_addr = m_node->GetObject<Ipv4>()->GetAddress(interface_num, 0).GetLocal();
	m_ipv4Route = SetRoute(nextHop, channel_hop_addr);
	dev = m_ip->GetNetDevice(m_ip->GetInterfaceForAddress(channel_hop_addr));

	m_ipv4Route->SetOutputDevice(dev);

	uint32_t priority = GetPriority(DSR_DATA_PACKET);
	std::map<uint32_t, Ptr<dsr::DsrNetworkQueue>>::iterator i = m_priorityQueue.find(priority);
	Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
	NS_LOG_INFO("Will be inserting into priority queue number: " << priority);

	// m_downTarget (packet, source, nextHop, GetProtocolNumber (), m_ipv4Route);

	/// \todo New DsrNetworkQueueEntry
	DsrNetworkQueueEntry newEntry(packet, source, nextHop, Simulator::Now(), m_ipv4Route);

	if (dsrNetworkQueue->Enqueue(newEntry))
	{
		Scheduler(priority);
	}
	else
	{
		NS_LOG_INFO("Packet dropped as dsr network queue is full");
	}
}

void
DsrRouting::Scheduler(uint32_t priority)
{
	NS_LOG_FUNCTION(this);
	PriorityScheduler(priority, true);
}

void
DsrRouting::PriorityScheduler(uint32_t priority, bool continueWithFirst)
{
	NS_LOG_FUNCTION(this << priority << continueWithFirst);
	uint32_t numPriorities;
	if (continueWithFirst)
	{
		numPriorities = 0;
	}
	else
	{
		numPriorities = priority;
	}
	// priorities ranging from 0 to m_numPriorityQueues, with 0 as the highest priority
	for (uint32_t i = priority; numPriorities < m_numPriorityQueues; numPriorities++)
	{
		std::map<uint32_t, Ptr<DsrNetworkQueue>>::iterator q = m_priorityQueue.find(i);
		Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = q->second;
		uint32_t queueSize = dsrNetworkQueue->GetSize();
		if (queueSize == 0)
		{
			if ((i == (m_numPriorityQueues - 1)) && continueWithFirst)
			{
				i = 0;
			}
			else
			{
				i++;
			}
		}
		else
		{
			uint32_t totalQueueSize = 0;
			for (std::map<uint32_t, Ptr<dsr::DsrNetworkQueue>>::iterator j = m_priorityQueue.begin();
				 j != m_priorityQueue.end();
				 j++)
			{
				NS_LOG_INFO("The size of the network queue for " << j->first << " is " << j->second->GetSize());
				totalQueueSize += j->second->GetSize();
				NS_LOG_INFO("The total network queue size is " << totalQueueSize);
			}
			if (totalQueueSize > 5)
			{
				// Here the queue size is larger than 5, we need to increase the retransmission
				// timer for each packet in the network queue
				IncreaseRetransTimer();
			}
			DsrNetworkQueueEntry newEntry;
			dsrNetworkQueue->Dequeue(newEntry);
			if (SendRealDown(newEntry))
			{
				// NS_LOG_LOGIC("Packet sent by Dsr. Calling PriorityScheduler after some time");
				NS_LOG_DEBUG("Packet sent by Dsr. Calling PriorityScheduler after some time");
				// packet was successfully sent down. call scheduler after some time
				Simulator::Schedule(MicroSeconds(m_uniformRandomVariable->GetInteger(0, 1000)),
									&DsrRouting::PriorityScheduler,
									this,
									i,
									false);
			}
			else
			{
				// packet was dropped by Dsr. Call scheduler immediately so that we can
				// send another packet immediately.
				// NS_LOG_LOGIC("Packet dropped by Dsr. Calling PriorityScheduler immediately");
				NS_LOG_DEBUG("Packet dropped by Dsr. Calling PriorityScheduler immediately");
				Simulator::Schedule(Seconds(0), &DsrRouting::PriorityScheduler, this, i, false);
			}

			if ((i == (m_numPriorityQueues - 1)) && continueWithFirst)
			{
				i = 0;
			}
			else
			{
				i++;
			}
		}
	}
}

void
DsrRouting::IncreaseRetransTimer()
{
	NS_LOG_FUNCTION(this);
	// We may want to get the queue first and then we need to save a vector of the entries here and
	// then find
	uint32_t priority = GetPriority(DSR_DATA_PACKET);
	std::map<uint32_t, Ptr<dsr::DsrNetworkQueue>>::iterator i = m_priorityQueue.find(priority);
	Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;

	std::vector<DsrNetworkQueueEntry> newNetworkQueue = dsrNetworkQueue->GetQueue();
	for (std::vector<DsrNetworkQueueEntry>::iterator i = newNetworkQueue.begin(); i != newNetworkQueue.end(); i++)
	{
		Ipv4Address nextHop = i->GetNextHopAddress();
		for (std::map<NetworkKey, Timer>::iterator j = m_addressForwardTimer.begin(); j != m_addressForwardTimer.end();
			 j++)
		{
			if (nextHop == j->first.m_nextHop)
			{
				NS_LOG_DEBUG("The network delay left is " << j->second.GetDelayLeft());
				j->second.SetDelay(j->second.GetDelayLeft() + m_retransIncr);
			}
		}
	}
}

bool
DsrRouting::SendRealDown(DsrNetworkQueueEntry& newEntry)
{
	NS_LOG_FUNCTION(this);
	Ipv4Address source = newEntry.GetSourceAddress();
	Ipv4Address nextHop = newEntry.GetNextHopAddress();
	Ptr<Packet> packet = newEntry.GetPacket()->Copy();
	Ptr<Ipv4Route> route = newEntry.GetIpv4Route();
	// m_downTarget(packet, source, nextHop, GetProtocolNumber(), route);
	// return true;

	//*
	if (!m_use_gradpc || route == nullptr)
	{
		m_downTarget(packet, source, nextHop, GetProtocolNumber(), route);
		return true;
	}

	const int gradpc_app_idx = 1;
	static thread_local auto neighbor_list =
		DynamicCast<GradPC_App>(m_node->GetApplication(gradpc_app_idx))->get_neighbor_list();
	// using grad-pc
	// change power if needed
	Ptr<WifiNetDevice> wifi_device = DynamicCast<WifiNetDevice>(route->GetOutputDevice());
	Ptr<YansWifiPhy> wifi_phy = DynamicCast<YansWifiPhy>(wifi_device->GetPhy());
	unsigned int device_idx = wifi_device->GetIfIndex();
	uint32_t next_node_id = GetNodeWithAddress(nextHop)->GetId();
	if (neighbor_list[device_idx].count(next_node_id))
	{
		m_downTarget(packet, source, nextHop, GetProtocolNumber(), route);
		return true;
	}

	NS_LOG_DEBUG("not enough power to send to next node, add tag to packet so gradpc-wifi-manager can adjust power");
	IsNeighborTag neighbor_tag{false}; // is not a neighbor
	if (!packet->PeekPacketTag(neighbor_tag))
	{
		// tag has not been added yet
		packet->AddPacketTag(neighbor_tag);
	}
	m_downTarget(packet, source, nextHop, GetProtocolNumber(), route);
	return true;
	//*/
}

void
DsrRouting::SendPacketFromBuffer(const DsrOptionSRHeader& sourceRoute, Ipv4Address nextHop, uint8_t protocol)
{
	NS_LOG_FUNCTION(this << nextHop << (uint32_t)protocol);
	NS_ASSERT_MSG(!m_downTarget.IsNull(), "Error, DsrRouting cannot send downward");

	// Reconstruct the route and Retransmit the data packet
	std::vector<Ipv4Address> nodeList = sourceRoute.GetNodesAddress();
	Ipv4Address destination = nodeList.back();
	Ipv4Address source = nodeList.front(); // Get the source address
	NS_LOG_INFO("The nexthop address " << nextHop << " the source " << source << " the destination " << destination);
	/*
	 * Here we try to find data packet from send buffer, if packet with this destination found, send
	 * it out
	 */
	NS_LOG_DEBUG("In SendPacketFromBuffer, destination is: " << destination);
	Print_sendBuffer(m_sendBuffer);
	// Originally, we don't know the destination's channel number,
	// so we used the same channel number as source.
	// Now, we search for the destination ip that uses the same channel number as source
	// in the m_sendBuffer to find the packet.
	int source_interface_id;
	Ipv4Address destination_in_buffer;
	if (destination != Ipv4Address::GetBroadcast())
	{
		destination_in_buffer = get_same_channel_Ipv4Address(m_mainAddress, destination);
		// destination_in_buffer = get_same_channel_Ipv4Address(source, destination);
		NS_LOG_DEBUG("destination_in_buffer: " << destination_in_buffer);
	}
	if (m_sendBuffer.Find(Ipv4Address::GetBroadcast()) || m_sendBuffer.Find(destination_in_buffer))
	// if (m_sendBuffer.Find(destination))
	{
		NS_LOG_DEBUG("destination over here " << destination_in_buffer);

		if (m_enable_global_broadcast && destination == Ipv4Address::GetBroadcast())
		{
			DsrSendBuffEntry entry;
			if (m_sendBuffer.Dequeue(destination, entry))
			{
				/*
				Simulator::Schedule(MicroSeconds(m_uniformRandomVariable->GetInteger(0, 100)),
									&DsrRouting::SendBroadcast,
									this,
									entry.GetPacket()->Copy(),
									source);
				*/
				Simulator::ScheduleNow(&DsrRouting::SendBroadcast, this, entry.GetPacket()->Copy(), source);
			}
			return;
		}

		/// When found a route and use it, UseExtends to the link cache
		if (m_routeCache->IsLinkCache())
		{
			m_routeCache->UseExtends(nodeList);
		}
		DsrSendBuffEntry entry;
		NS_LOG_DEBUG("m_sendBuffer size before dequeue: " << m_sendBuffer.GetSize());
		if (m_sendBuffer.Dequeue(destination_in_buffer, entry))
		{
			Ptr<Packet> packet = entry.GetPacket()->Copy();
			Ptr<Packet> p = packet->Copy(); // get a copy of the packet
			// Set the source route option
			DsrRoutingHeader dsrRoutingHeader;
			dsrRoutingHeader.SetNextHeader(protocol);
			dsrRoutingHeader.SetMessageType(2);
			dsrRoutingHeader.SetSourceId(GetIDfromIP(source));
			dsrRoutingHeader.SetDestId(GetIDfromIP(destination));

			uint8_t length = sourceRoute.GetLength();
			dsrRoutingHeader.SetPayloadLength(uint16_t(length) + 2);
			dsrRoutingHeader.AddDsrOption(sourceRoute);

			p->AddHeader(dsrRoutingHeader);

			Ptr<const Packet> mtP = p->Copy();
			// Put the data packet in the maintenance queue for data packet retransmission
			unsigned int current_node_id = m_node->GetId();
			Ipv4Address ourAddress = m_mainAddress;
			for (Ipv4Address ipv4_addr : nodeList)
			{
				if (current_node_id == GetIDfromIP(ipv4_addr))
				{
					ourAddress = ipv4_addr;
					break;
				}
			}

			NS_LOG_DEBUG("ourAddress: " << ourAddress);
			DsrMaintainBuffEntry newEntry(
				/*Packet=*/mtP,
				/*ourAddress=*/ourAddress,
				/*nextHop=*/nextHop,
				/*source=*/source,
				/*destination=*/destination,
				/*ackId=*/0,
				/*SegsLeft=*/nodeList.size() - 2,
				/*expire time=*/m_maxMaintainTime);
			NS_LOG_DEBUG("In Sendpacketfrombuffer, add DsrMaintainBuffEntry, source:"
						 << source << ", nextHop:" << nextHop << ", destination: " << destination);

			bool result = m_maintainBuffer.Enqueue(newEntry); // Enqueue the packet the the maintenance buffer

			if (result)
			{
				NetworkKey networkKey;
				networkKey.m_ackId = newEntry.GetAckId();
				networkKey.m_ourAdd = newEntry.GetOurAdd();
				networkKey.m_nextHop = newEntry.GetNextHop();
				networkKey.m_source = newEntry.GetSrc();
				networkKey.m_destination = newEntry.GetDst();

				PassiveKey passiveKey;
				passiveKey.m_ackId = 0;
				passiveKey.m_source = newEntry.GetSrc();
				passiveKey.m_destination = newEntry.GetDst();
				passiveKey.m_segsLeft = newEntry.GetSegsLeft();

				LinkKey linkKey;
				linkKey.m_source = newEntry.GetSrc();
				linkKey.m_destination = newEntry.GetDst();
				linkKey.m_ourAdd = newEntry.GetOurAdd();
				linkKey.m_nextHop = newEntry.GetNextHop();

				m_addressForwardCnt[networkKey] = 0;
				m_passiveCnt[passiveKey] = 0;
				m_linkCnt[linkKey] = 0;

				if (m_linkAck)
				{
					ScheduleLinkPacketRetry(newEntry, protocol);
				}
				else
				{
					NS_LOG_LOGIC("Not using link acknowledgment");
					if (nextHop != destination)
					{
						SchedulePassivePacketRetry(newEntry, protocol);
					}
					else
					{
						// This is the first network retry
						ScheduleNetworkPacketRetry(newEntry, true, protocol);
					}
				}
			}

			NS_LOG_DEBUG("send buffer size: " << m_sendBuffer.GetSize() << " destination: " << destination_in_buffer);
			if (m_sendBuffer.GetSize() != 0 && m_sendBuffer.Find(destination_in_buffer))
			{
				NS_LOG_LOGIC("Schedule sending the next packet in send buffer");
				Simulator::Schedule(MilliSeconds(m_uniformRandomVariable->GetInteger(0, 100)),
									&DsrRouting::SendPacketFromBuffer,
									this,
									sourceRoute,
									nextHop,
									protocol);
			}
		}
		else
		{
			NS_LOG_LOGIC("All queued packets are out-dated for the destination in send buffer");
		}
	}
	/*
	 * Here we try to find data packet from send buffer, if packet with this destination found, send
	 * it out
	 */
	else if (m_errorBuffer.Find(destination))
	{
		DsrErrorBuffEntry entry;
		if (m_errorBuffer.Dequeue(destination, entry))
		{
			Ptr<Packet> packet = entry.GetPacket()->Copy();
			NS_LOG_DEBUG("The queued packet size " << packet->GetSize());

			DsrRoutingHeader dsrRoutingHeader;
			Ptr<Packet> copyP = packet->Copy();
			Ptr<Packet> dsrPacket = packet->Copy();
			dsrPacket->RemoveHeader(dsrRoutingHeader);
			uint32_t offset = dsrRoutingHeader.GetDsrOptionsOffset();
			copyP->RemoveAtStart(offset); // Here the processed size is 8 bytes, which is the fixed
										  // sized extension header
			/*
			 * Peek data to get the option type as well as length and segmentsLeft field
			 */
			uint32_t size = copyP->GetSize();
			uint8_t* data = new uint8_t[size];
			copyP->CopyData(data, size);

			uint8_t optionType = 0;
			optionType = *(data);
			NS_LOG_DEBUG("The option type value in send packet " << (uint32_t)optionType);
			if (optionType == 3)
			{
				NS_LOG_DEBUG("The packet is error packet");
				Ptr<dsr::DsrOptions> dsrOption;
				DsrOptionHeader dsrOptionHeader;

				uint8_t errorType = *(data + 2);
				NS_LOG_DEBUG("The error type");
				if (errorType == 1)
				{
					NS_LOG_DEBUG("The packet is route error unreach packet");
					DsrOptionRerrUnreachHeader rerr;
					copyP->RemoveHeader(rerr);
					NS_ASSERT(copyP->GetSize() == 0);
					uint8_t length = (sourceRoute.GetLength() + rerr.GetLength());

					DsrOptionRerrUnreachHeader newUnreach;
					newUnreach.SetErrorType(1);
					newUnreach.SetErrorSrc(rerr.GetErrorSrc());
					newUnreach.SetUnreachNode(rerr.GetUnreachNode());
					newUnreach.SetErrorDst(rerr.GetErrorDst());
					newUnreach.SetOriginalDst(rerr.GetOriginalDst());
					newUnreach.SetSalvage(rerr.GetSalvage()); // Set the value about whether to
															  // salvage a packet or not

					std::vector<Ipv4Address> nodeList = sourceRoute.GetNodesAddress();
					DsrRoutingHeader newRoutingHeader;
					newRoutingHeader.SetNextHeader(protocol);
					newRoutingHeader.SetMessageType(1);
					newRoutingHeader.SetSourceId(GetIDfromIP(rerr.GetErrorSrc()));
					newRoutingHeader.SetDestId(GetIDfromIP(rerr.GetErrorDst()));
					newRoutingHeader.SetPayloadLength(uint16_t(length) + 4);
					newRoutingHeader.AddDsrOption(newUnreach);
					newRoutingHeader.AddDsrOption(sourceRoute);
					/// When found a route and use it, UseExtends to the link cache
					if (m_routeCache->IsLinkCache())
					{
						m_routeCache->UseExtends(nodeList);
					}
					SetRoute(nextHop, m_mainAddress);
					Ptr<Packet> newPacket = Create<Packet>();
					newPacket->AddHeader(newRoutingHeader); // Add the extension header with rerr
															// and sourceRoute attached to it
					Ptr<NetDevice> dev = m_ip->GetNetDevice(m_ip->GetInterfaceForAddress(m_mainAddress));
					m_ipv4Route->SetOutputDevice(dev);

					uint32_t priority = GetPriority(DSR_CONTROL_PACKET);
					std::map<uint32_t, Ptr<dsr::DsrNetworkQueue>>::iterator i = m_priorityQueue.find(priority);
					Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
					NS_LOG_DEBUG("Will be inserting into priority queue " << dsrNetworkQueue
																		  << " number: " << priority);

					// m_downTarget (newPacket, m_mainAddress, nextHop, GetProtocolNumber (),
					// m_ipv4Route);

					/// \todo New DsrNetworkQueueEntry
					DsrNetworkQueueEntry newEntry(newPacket, m_mainAddress, nextHop, Simulator::Now(), m_ipv4Route);

					if (dsrNetworkQueue->Enqueue(newEntry))
					{
						Scheduler(priority);
					}
					else
					{
						NS_LOG_INFO("Packet dropped as dsr network queue is full");
					}
				}
			}

			if (m_errorBuffer.GetSize() != 0 && m_errorBuffer.Find(destination))
			{
				NS_LOG_LOGIC("Schedule sending the next packet in error buffer");
				Simulator::Schedule(MilliSeconds(m_uniformRandomVariable->GetInteger(0, 100)),
									&DsrRouting::SendPacketFromBuffer,
									this,
									sourceRoute,
									nextHop,
									protocol);
			}
		}
	}
	else
	{
		NS_LOG_DEBUG("Packet not found in either the send or error buffer");
	}
}

bool
DsrRouting::PassiveEntryCheck(Ptr<Packet> packet,
							  Ipv4Address source,
							  Ipv4Address destination,
							  uint8_t segsLeft,
							  uint16_t fragmentOffset,
							  uint16_t identification,
							  bool saveEntry)
{
	// NS_LOG_FUNCTION(this << packet << source << destination << (uint32_t)segsLeft);

	Ptr<Packet> p = packet->Copy();
	// Here the segments left value need to plus one to check the earlier hop maintain buffer entry
	DsrPassiveBuffEntry newEntry;
	newEntry.SetPacket(p);
	newEntry.SetSource(source);
	newEntry.SetDestination(destination);
	newEntry.SetIdentification(identification);
	newEntry.SetFragmentOffset(fragmentOffset);
	newEntry.SetSegsLeft(segsLeft); // We try to make sure the segments left is larger for 1

	// NS_LOG_DEBUG("The passive buffer size " << m_passiveBuffer->GetSize());

	if (m_passiveBuffer->AllEqual(newEntry) && (!saveEntry))
	{
		// The PromiscEqual function will remove the maintain buffer entry if equal value found
		// It only compares the source and destination address, ackId, and the segments left value
		// NS_LOG_DEBUG("We get the all equal for passive buffer here");

		DsrMaintainBuffEntry mbEntry;
		mbEntry.SetPacket(p);
		mbEntry.SetSrc(source);
		mbEntry.SetDst(destination);
		mbEntry.SetAckId(0);
		mbEntry.SetSegsLeft(segsLeft + 1);

		CancelPassivePacketTimer(mbEntry);
		return true;
	}
	if (saveEntry)
	{
		/// Save this passive buffer entry for later check
		m_passiveBuffer->Enqueue(newEntry);
	}
	return false;
}

bool
DsrRouting::CancelPassiveTimer(Ptr<Packet> packet, Ipv4Address source, Ipv4Address destination, uint8_t segsLeft)
{
	NS_LOG_FUNCTION(this << packet << source << destination << (uint32_t)segsLeft);

	NS_LOG_DEBUG("Cancel the passive timer");

	Ptr<Packet> p = packet->Copy();
	// Here the segments left value need to plus one to check the earlier hop maintain buffer entry
	DsrMaintainBuffEntry newEntry;
	newEntry.SetPacket(p);
	newEntry.SetSrc(source);
	newEntry.SetDst(destination);
	newEntry.SetAckId(0);
	newEntry.SetSegsLeft(segsLeft + 1);

	if (m_maintainBuffer.PromiscEqual(newEntry))
	{
		// The PromiscEqual function will remove the maintain buffer entry if equal value found
		// It only compares the source and destination address, ackId, and the segments left value
		CancelPassivePacketTimer(newEntry);
		return true;
	}
	return false;
}

void
DsrRouting::CallCancelPacketTimer(uint16_t ackId,
								  const Ipv4Header& ipv4Header,
								  Ipv4Address realSrc,
								  Ipv4Address realDst)
{
	NS_LOG_FUNCTION(this << (uint32_t)ackId << ipv4Header << realSrc << realDst);
	Ipv4Address sender = ipv4Header.GetDestination();
	Ipv4Address receiver = ipv4Header.GetSource();
	/*
	 * Create a packet to fill maintenance buffer, not used to compare with maintenance entry
	 * The reason is ack header doesn't have the original packet copy
	 */
	Ptr<Packet> mainP = Create<Packet>();
	DsrMaintainBuffEntry newEntry(/*packet=*/mainP,
								  /*ourAddress=*/sender,
								  /*nextHop=*/receiver,
								  /*src=*/realSrc,
								  /*dst=*/realDst,
								  /*ackId=*/ackId,
								  /*segsLeft=*/0,
								  /*expire=*/Simulator::Now());
	CancelNetworkPacketTimer(newEntry); // Only need to cancel network packet timer
}

void
DsrRouting::CancelPacketAllTimer(DsrMaintainBuffEntry& mb)
{
	NS_LOG_FUNCTION(this);
	CancelLinkPacketTimer(mb);
	CancelNetworkPacketTimer(mb);
	CancelPassivePacketTimer(mb);
}

void
DsrRouting::CancelLinkPacketTimer_with_all_ip(DsrMaintainBuffEntry& mb)
{
	NS_LOG_FUNCTION(this);
	LinkKey linkKey;
	linkKey.m_ourAdd = mb.GetOurAdd();
	linkKey.m_nextHop = mb.GetNextHop();
	linkKey.m_source = mb.GetSrc();
	linkKey.m_destination = mb.GetDst();
	/*
	 * Here we have found the entry for send retries, so we get the value and increase it by one
	 */
	/// TODO need to think about this part
	m_linkCnt[linkKey] = 0;
	m_linkCnt.erase(linkKey);

	// TODO if find the linkkey, we need to remove it

	// Find the network acknowledgment timer
	bool found_link_key = false;
	for (const auto& [lnk_key, _] : m_linkAckTimer)
	{
		if (lnk_key.m_source == linkKey.m_source &&
			GetIDfromIP(lnk_key.m_destination) == GetIDfromIP(linkKey.m_destination) &&
			lnk_key.m_ourAdd == linkKey.m_ourAdd && lnk_key.m_nextHop == linkKey.m_nextHop)
		{
			found_link_key = true;
			linkKey = lnk_key;
			mb.SetDst(lnk_key.m_destination);
		}
		NS_LOG_DEBUG("source: " << lnk_key.m_source);
		NS_LOG_DEBUG("destination: " << lnk_key.m_destination);
		NS_LOG_DEBUG("ourAdd: " << lnk_key.m_ourAdd);
		NS_LOG_DEBUG("nextHop: " << lnk_key.m_nextHop);
	}

	if (!found_link_key)
	{
		NS_LOG_INFO("did not find the link timer");
	}
	else
	{
		NS_LOG_INFO("did find the link timer");
		/*
		 * Schedule the packet retry
		 * Push back the nextHop, source, destination address
		 */
		m_linkAckTimer[linkKey].Cancel();
		if (m_linkAckTimer[linkKey].IsRunning())
		{
			NS_LOG_INFO("Timer not canceled");
		}
		m_linkAckTimer.erase(linkKey);
	}

	// Erase the maintenance entry
	// yet this does not check the segments left value here
	NS_LOG_DEBUG("The link buffer size " << m_maintainBuffer.GetSize());
	if (m_maintainBuffer.LinkEqual(mb))
	{
		NS_LOG_INFO("Link acknowledgment received, remove same maintenance buffer entry");
	}
	// NS_ASSERT_MSG(false, "Testing");
}

void
DsrRouting::CancelLinkPacketTimer(DsrMaintainBuffEntry& mb)
{
	NS_LOG_FUNCTION(this);
	LinkKey linkKey;
	linkKey.m_ourAdd = mb.GetOurAdd();
	linkKey.m_nextHop = mb.GetNextHop();
	linkKey.m_source = mb.GetSrc();
	linkKey.m_destination = mb.GetDst();
	/*
	 * Here we have found the entry for send retries, so we get the value and increase it by one
	 */
	/// TODO need to think about this part
	m_linkCnt[linkKey] = 0;
	m_linkCnt.erase(linkKey);

	// TODO if find the linkkey, we need to remove it

	// Find the network acknowledgment timer
	std::map<LinkKey, Timer>::const_iterator i = m_linkAckTimer.find(linkKey);
	if (i == m_linkAckTimer.end())
	{
		NS_LOG_INFO("did not find the link timer");
	}
	else
	{
		NS_LOG_INFO("did find the link timer");
		/*
		 * Schedule the packet retry
		 * Push back the nextHop, source, destination address
		 */
		m_linkAckTimer[linkKey].Cancel();
		if (m_linkAckTimer[linkKey].IsRunning())
		{
			NS_LOG_INFO("Timer not canceled");
		}
		m_linkAckTimer.erase(linkKey);
	}

	// Erase the maintenance entry
	// yet this does not check the segments left value here
	NS_LOG_DEBUG("The link buffer size " << m_maintainBuffer.GetSize());
	if (m_maintainBuffer.LinkEqual(mb))
	{
		NS_LOG_INFO("Link acknowledgment received, remove same maintenance buffer entry");
	}
}

void
DsrRouting::CancelNetworkPacketTimer(DsrMaintainBuffEntry& mb)
{
	NS_LOG_FUNCTION(this);
	NetworkKey networkKey;
	networkKey.m_ackId = mb.GetAckId();
	networkKey.m_ourAdd = mb.GetOurAdd();
	networkKey.m_nextHop = mb.GetNextHop();
	networkKey.m_source = mb.GetSrc();
	networkKey.m_destination = mb.GetDst();
	/*
	 * Here we have found the entry for send retries, so we get the value and increase it by one
	 */
	m_addressForwardCnt[networkKey] = 0;
	m_addressForwardCnt.erase(networkKey);

	NS_LOG_INFO("ackId " << mb.GetAckId() << " ourAdd " << mb.GetOurAdd() << " nextHop " << mb.GetNextHop()
						 << " source " << mb.GetSrc() << " destination " << mb.GetDst() << " segsLeft "
						 << (uint32_t)mb.GetSegsLeft());
	// Find the network acknowledgment timer
	std::map<NetworkKey, Timer>::const_iterator i = m_addressForwardTimer.find(networkKey);
	if (i == m_addressForwardTimer.end())
	{
		NS_LOG_INFO("did not find the packet timer");
	}
	else
	{
		NS_LOG_INFO("did find the packet timer");
		/*
		 * Schedule the packet retry
		 * Push back the nextHop, source, destination address
		 */
		m_addressForwardTimer[networkKey].Cancel();
		if (m_addressForwardTimer[networkKey].IsRunning())
		{
			NS_LOG_INFO("Timer not canceled");
		}
		m_addressForwardTimer.erase(networkKey);
	}
	// Erase the maintenance entry
	// yet this does not check the segments left value here
	if (m_maintainBuffer.NetworkEqual(mb))
	{
		NS_LOG_INFO("Remove same maintenance buffer entry based on network acknowledgment");
	}
}

void
DsrRouting::CancelPassivePacketTimer(DsrMaintainBuffEntry& mb)
{
	NS_LOG_FUNCTION(this);
	PassiveKey passiveKey;
	passiveKey.m_ackId = 0;
	passiveKey.m_source = mb.GetSrc();
	passiveKey.m_destination = mb.GetDst();
	passiveKey.m_segsLeft = mb.GetSegsLeft();

	m_passiveCnt[passiveKey] = 0;
	m_passiveCnt.erase(passiveKey);

	// Find the passive acknowledgment timer
	std::map<PassiveKey, Timer>::const_iterator j = m_passiveAckTimer.find(passiveKey);
	if (j == m_passiveAckTimer.end())
	{
		NS_LOG_INFO("did not find the passive timer");
	}
	else
	{
		NS_LOG_INFO("find the passive timer");
		/*
		 * Cancel passive acknowledgment timer
		 */
		m_passiveAckTimer[passiveKey].Cancel();
		if (m_passiveAckTimer[passiveKey].IsRunning())
		{
			NS_LOG_INFO("Timer not canceled");
		}
		m_passiveAckTimer.erase(passiveKey);
	}
}

void
DsrRouting::CancelPacketTimerNextHop(Ipv4Address nextHop, uint8_t protocol)
{
	NS_LOG_FUNCTION(this << nextHop << (uint32_t)protocol);

	DsrMaintainBuffEntry entry;
	std::vector<Ipv4Address> previousErrorDst;
	if (m_maintainBuffer.Dequeue(nextHop, entry))
	{
		Ipv4Address source = entry.GetSrc();
		Ipv4Address destination = entry.GetDst();

		Ptr<Packet> dsrP = entry.GetPacket()->Copy();
		Ptr<Packet> p = dsrP->Copy();
		Ptr<Packet> packet = dsrP->Copy();
		DsrRoutingHeader dsrRoutingHeader;
		dsrP->RemoveHeader(dsrRoutingHeader); // Remove the dsr header in whole
		uint32_t offset = dsrRoutingHeader.GetDsrOptionsOffset();
		p->RemoveAtStart(offset);

		// Get the number of routers' address field
		uint8_t buf[2];
		p->CopyData(buf, sizeof(buf));
		uint8_t numberAddress = (buf[1] - 2) / 4;
		NS_LOG_DEBUG("The number of addresses " << (uint32_t)numberAddress);
		DsrOptionSRHeader sourceRoute;
		sourceRoute.SetNumberAddress(numberAddress);
		p->RemoveHeader(sourceRoute);
		std::vector<Ipv4Address> nodeList = sourceRoute.GetNodesAddress();
		uint8_t salvage = sourceRoute.GetSalvage();
		Ipv4Address address1 = nodeList[1];
		PrintVector(nodeList);

		/*
		 * If the salvage is not 0, use the first address in the route as the error dst in error
		 * header otherwise use the source of packet as the error destination
		 */
		Ipv4Address errorDst;
		if (salvage)
		{
			errorDst = address1;
		}
		else
		{
			errorDst = source;
		}
		/// TODO if the errorDst is not seen before
		if (std::find(previousErrorDst.begin(), previousErrorDst.end(), destination) == previousErrorDst.end())
		{
			NS_LOG_DEBUG("have not seen this dst before " << errorDst << " in " << previousErrorDst.size());
			SendUnreachError(nextHop, errorDst, destination, salvage, protocol);
			previousErrorDst.push_back(errorDst);
		}

		/*
		 * Cancel the packet timer and then salvage the data packet
		 */

		CancelPacketAllTimer(entry);
		SalvagePacket(packet, source, destination, protocol);

		if (m_maintainBuffer.GetSize() && m_maintainBuffer.Find(nextHop))
		{
			NS_LOG_INFO("Cancel the packet timer for next maintenance entry");
			Simulator::Schedule(MilliSeconds(m_uniformRandomVariable->GetInteger(0, 100)),
								&DsrRouting::CancelPacketTimerNextHop,
								this,
								nextHop,
								protocol);
		}
	}
	else
	{
		NS_LOG_INFO("Maintenance buffer entry not found");
	}
	/// TODO need to think about whether we need the network queue entry or not
}

void
DsrRouting::SalvagePacket(Ptr<const Packet> packet, Ipv4Address source, Ipv4Address dst, uint8_t protocol)
{
	NS_LOG_FUNCTION(this << packet << source << dst << (uint32_t)protocol);
	// Create two copies of packet
	Ptr<Packet> p = packet->Copy();
	Ptr<Packet> newPacket = packet->Copy();
	// Remove the routing header in a whole to get a clean packet
	DsrRoutingHeader dsrRoutingHeader;
	p->RemoveHeader(dsrRoutingHeader);
	// Remove offset of dsr routing header
	uint8_t offset = dsrRoutingHeader.GetDsrOptionsOffset();
	newPacket->RemoveAtStart(offset);

	// Get the number of routers' address field
	uint8_t buf[2];
	newPacket->CopyData(buf, sizeof(buf));
	uint8_t numberAddress = (buf[1] - 2) / 4;

	DsrOptionSRHeader sourceRoute;
	sourceRoute.SetNumberAddress(numberAddress);
	newPacket->RemoveHeader(sourceRoute);
	uint8_t salvage = sourceRoute.GetSalvage();
	/*
	 * Look in the route cache for other routes for this destination
	 */
	DsrRouteCacheEntry toDst;
	bool findRoute = m_routeCache->LookupRoute(dst, toDst);
	if (findRoute && (salvage < m_maxSalvageCount))
	{
		NS_LOG_DEBUG("We have found a route for the packet");
		DsrRoutingHeader newDsrRoutingHeader;
		newDsrRoutingHeader.SetNextHeader(protocol);
		newDsrRoutingHeader.SetMessageType(2);
		newDsrRoutingHeader.SetSourceId(GetIDfromIP(source));
		newDsrRoutingHeader.SetDestId(GetIDfromIP(dst));

		std::vector<Ipv4Address> nodeList = toDst.GetVector(); // Get the route from the route entry we found
		Ipv4Address nextHop =
			SearchNextHop_with_all_ip(m_mainAddress, nodeList); // Get the next hop address for the route
		if (nextHop == "0.0.0.0")
		{
			PacketNewRoute(p, source, dst, protocol);
			return;
		}
		// Increase the salvage count by 1
		salvage++;
		DsrOptionSRHeader sourceRoute;
		sourceRoute.SetSalvage(salvage);
		sourceRoute.SetNodesAddress(nodeList); // Save the whole route in the source route header of the packet
		sourceRoute.SetSegmentsLeft((nodeList.size() - 2)); // The segmentsLeft field will indicate the hops to go
		/// When found a route and use it, UseExtends to the link cache
		if (m_routeCache->IsLinkCache())
		{
			m_routeCache->UseExtends(nodeList);
		}
		uint8_t length = sourceRoute.GetLength();
		NS_LOG_INFO("length of source route header " << (uint32_t)(sourceRoute.GetLength()));
		newDsrRoutingHeader.SetPayloadLength(uint16_t(length) + 2);
		newDsrRoutingHeader.AddDsrOption(sourceRoute);
		p->AddHeader(newDsrRoutingHeader);

		SetRoute(nextHop, m_mainAddress);
		Ptr<NetDevice> dev = m_ip->GetNetDevice(m_ip->GetInterfaceForAddress(m_mainAddress));
		m_ipv4Route->SetOutputDevice(dev);

		// Send out the data packet
		uint32_t priority = GetPriority(DSR_DATA_PACKET);
		std::map<uint32_t, Ptr<dsr::DsrNetworkQueue>>::iterator i = m_priorityQueue.find(priority);
		Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
		NS_LOG_DEBUG("Will be inserting into priority queue " << dsrNetworkQueue << " number: " << priority);

		// m_downTarget (p, m_mainAddress, nextHop, GetProtocolNumber (), m_ipv4Route);

		/// \todo New DsrNetworkQueueEntry
		DsrNetworkQueueEntry newEntry(p, m_mainAddress, nextHop, Simulator::Now(), m_ipv4Route);

		if (dsrNetworkQueue->Enqueue(newEntry))
		{
			Scheduler(priority);
		}
		else
		{
			NS_LOG_INFO("Packet dropped as dsr network queue is full");
		}

		/*
		 * Mark the next hop address in blacklist
		 */
		//      NS_LOG_DEBUG ("Save the next hop node in blacklist");
		//      m_rreqTable->MarkLinkAsUnidirectional (nextHop, m_blacklistTimeout);
	}
	else
	{
		NS_LOG_DEBUG("Will not salvage this packet, silently drop");
	}
}

void
DsrRouting::ScheduleLinkPacketRetry(DsrMaintainBuffEntry& mb, uint8_t protocol)
{
	NS_LOG_FUNCTION(this << (uint32_t)protocol);

	Ptr<Packet> p = mb.GetPacket()->Copy();
	Ipv4Address source = mb.GetSrc();
	Ipv4Address nextHop = mb.GetNextHop();

	// Send the data packet out before schedule the next packet transmission
	SendPacket(p, source, nextHop, protocol);

	LinkKey linkKey;
	linkKey.m_source = mb.GetSrc();
	linkKey.m_destination = mb.GetDst();
	linkKey.m_ourAdd = mb.GetOurAdd();
	linkKey.m_nextHop = mb.GetNextHop();

	if (m_linkAckTimer.find(linkKey) == m_linkAckTimer.end())
	{
		Timer timer(Timer::CANCEL_ON_DESTROY);
		m_linkAckTimer[linkKey] = timer;
	}
	m_linkAckTimer[linkKey].SetFunction(&DsrRouting::LinkScheduleTimerExpire, this);
	m_linkAckTimer[linkKey].Cancel();
	m_linkAckTimer[linkKey].SetArguments(mb, protocol);
	m_linkAckTimer[linkKey].Schedule(m_linkAckTimeout);
}

void
DsrRouting::SchedulePassivePacketRetry(DsrMaintainBuffEntry& mb, uint8_t protocol)
{
	NS_LOG_FUNCTION(this << (uint32_t)protocol);

	Ptr<Packet> p = mb.GetPacket()->Copy();
	Ipv4Address source = mb.GetSrc();
	Ipv4Address nextHop = mb.GetNextHop();

	// Send the data packet out before schedule the next packet transmission
	SendPacket(p, source, nextHop, protocol);

	PassiveKey passiveKey;
	passiveKey.m_ackId = 0;
	passiveKey.m_source = mb.GetSrc();
	passiveKey.m_destination = mb.GetDst();
	passiveKey.m_segsLeft = mb.GetSegsLeft();

	if (m_passiveAckTimer.find(passiveKey) == m_passiveAckTimer.end())
	{
		Timer timer(Timer::CANCEL_ON_DESTROY);
		m_passiveAckTimer[passiveKey] = timer;
	}
	NS_LOG_DEBUG("The passive acknowledgment option for data packet");
	m_passiveAckTimer[passiveKey].SetFunction(&DsrRouting::PassiveScheduleTimerExpire, this);
	m_passiveAckTimer[passiveKey].Cancel();
	m_passiveAckTimer[passiveKey].SetArguments(mb, protocol);
	m_passiveAckTimer[passiveKey].Schedule(m_passiveAckTimeout);
}

void
DsrRouting::ScheduleNetworkPacketRetry(DsrMaintainBuffEntry& mb, bool isFirst, uint8_t protocol)
{
	Ptr<Packet> p = Create<Packet>();
	Ptr<Packet> dsrP = Create<Packet>();
	// The new entry will be used for retransmission
	NetworkKey networkKey;
	Ipv4Address nextHop = mb.GetNextHop();
	NS_LOG_DEBUG("is the first retry or not " << isFirst);
	if (isFirst)
	{
		// This is the very first network packet retry
		p = mb.GetPacket()->Copy();
		// Here we add the ack request header to the data packet for network acknowledgement
		uint16_t ackId = AddAckReqHeader(p, nextHop);

		Ipv4Address source = mb.GetSrc();
		Ipv4Address nextHop = mb.GetNextHop();
		// Send the data packet out before schedule the next packet transmission
		SendPacket(p, source, nextHop, protocol);

		dsrP = p->Copy();
		DsrMaintainBuffEntry newEntry = mb;
		// The function AllEqual will find the exact entry and delete it if found
		m_maintainBuffer.AllEqual(mb);
		newEntry.SetPacket(dsrP);
		newEntry.SetAckId(ackId);
		newEntry.SetExpireTime(m_maxMaintainTime);

		networkKey.m_ackId = newEntry.GetAckId();
		networkKey.m_ourAdd = newEntry.GetOurAdd();
		networkKey.m_nextHop = newEntry.GetNextHop();
		networkKey.m_source = newEntry.GetSrc();
		networkKey.m_destination = newEntry.GetDst();

		m_addressForwardCnt[networkKey] = 0;
		if (!m_maintainBuffer.Enqueue(newEntry))
		{
			NS_LOG_ERROR("Failed to enqueue packet retry");
		}

		if (m_addressForwardTimer.find(networkKey) == m_addressForwardTimer.end())
		{
			Timer timer(Timer::CANCEL_ON_DESTROY);
			m_addressForwardTimer[networkKey] = timer;
		}

		// After m_tryPassiveAcks, schedule the packet retransmission using network acknowledgment
		// option
		m_addressForwardTimer[networkKey].SetFunction(&DsrRouting::NetworkScheduleTimerExpire, this);
		m_addressForwardTimer[networkKey].Cancel();
		m_addressForwardTimer[networkKey].SetArguments(newEntry, protocol);
		NS_LOG_DEBUG("The packet retries time for " << newEntry.GetAckId() << " is " << m_sendRetries
													<< " and the delay time is "
													<< Time(2 * m_nodeTraversalTime).As(Time::S));
		// Back-off mechanism
		m_addressForwardTimer[networkKey].Schedule(Time(2 * m_nodeTraversalTime));
	}
	else
	{
		networkKey.m_ackId = mb.GetAckId();
		networkKey.m_ourAdd = mb.GetOurAdd();
		networkKey.m_nextHop = mb.GetNextHop();
		networkKey.m_source = mb.GetSrc();
		networkKey.m_destination = mb.GetDst();
		/*
		 * Here we have found the entry for send retries, so we get the value and increase it by one
		 */
		m_sendRetries = m_addressForwardCnt[networkKey];
		NS_LOG_DEBUG("The packet retry we have done " << m_sendRetries);

		p = mb.GetPacket()->Copy();
		dsrP = mb.GetPacket()->Copy();

		Ipv4Address source = mb.GetSrc();
		Ipv4Address nextHop = mb.GetNextHop();
		// Send the data packet out before schedule the next packet transmission
		SendPacket(p, source, nextHop, protocol);

		NS_LOG_DEBUG("The packet with dsr header " << dsrP->GetSize());
		networkKey.m_ackId = mb.GetAckId();
		networkKey.m_ourAdd = mb.GetOurAdd();
		networkKey.m_nextHop = mb.GetNextHop();
		networkKey.m_source = mb.GetSrc();
		networkKey.m_destination = mb.GetDst();
		/*
		 *  If a data packet has been attempted SendRetries times at the maximum TTL without
		 *  receiving any ACK, all data packets destined for the corresponding destination SHOULD be
		 *  dropped from the send buffer
		 *
		 *  The maxMaintRexmt also needs to decrease one for the passive ack packet
		 */
		/*
		 * Check if the send retry time for a certain packet has already passed max maintenance
		 * retransmission time or not
		 */

		// After m_tryPassiveAcks, schedule the packet retransmission using network acknowledgment
		// option
		m_addressForwardTimer[networkKey].SetFunction(&DsrRouting::NetworkScheduleTimerExpire, this);
		m_addressForwardTimer[networkKey].Cancel();
		m_addressForwardTimer[networkKey].SetArguments(mb, protocol);
		NS_LOG_DEBUG("The packet retries time for " << mb.GetAckId() << " is " << m_sendRetries
													<< " and the delay time is "
													<< Time(2 * m_sendRetries * m_nodeTraversalTime).As(Time::S));
		// Back-off mechanism
		m_addressForwardTimer[networkKey].Schedule(Time(2 * m_sendRetries * m_nodeTraversalTime));
	}
}

void
DsrRouting::LinkScheduleTimerExpire(DsrMaintainBuffEntry& mb, uint8_t protocol)
{
	NS_LOG_FUNCTION(this << (uint32_t)protocol);
	Ipv4Address nextHop = mb.GetNextHop();
	Ptr<const Packet> packet = mb.GetPacket();
	// SetRoute(nextHop, m_mainAddress);
	SetRoute(nextHop, get_same_channel_Ipv4Address(nextHop, m_mainAddress));
	Ptr<Packet> p = packet->Copy();

	LinkKey lk;
	lk.m_source = mb.GetSrc();
	lk.m_destination = mb.GetDst();
	lk.m_ourAdd = mb.GetOurAdd();
	lk.m_nextHop = mb.GetNextHop();

	// Cancel passive ack timer
	m_linkAckTimer[lk].Cancel();
	if (m_linkAckTimer[lk].IsRunning())
	{
		NS_LOG_DEBUG("Timer not canceled");
	}
	m_linkAckTimer.erase(lk);

	// Increase the send retry times
	m_linkRetries = m_linkCnt[lk];
	if (m_linkRetries < m_tryLinkAcks)
	{
		m_linkCnt[lk] = ++m_linkRetries;
		ScheduleLinkPacketRetry(mb, protocol);
	}
	else
	{
		NS_LOG_INFO("We need to send error messages now");

		// Delete all the routes including the links
		m_routeCache->DeleteAllRoutesIncludeLink(m_mainAddress, nextHop, m_mainAddress);
		/*
		 * here we cancel the packet retransmission time for all the packets have next hop address
		 * as nextHop Also salvage the packet for the all the packet destined for the nextHop
		 * address this is also responsible for send unreachable error back to source
		 */
		CancelPacketTimerNextHop(nextHop, protocol);
	}
}

void
DsrRouting::PassiveScheduleTimerExpire(DsrMaintainBuffEntry& mb, uint8_t protocol)
{
	NS_LOG_FUNCTION(this << (uint32_t)protocol);
	Ipv4Address nextHop = mb.GetNextHop();
	Ptr<const Packet> packet = mb.GetPacket();
	SetRoute(nextHop, m_mainAddress);
	Ptr<Packet> p = packet->Copy();

	PassiveKey pk;
	pk.m_ackId = 0;
	pk.m_source = mb.GetSrc();
	pk.m_destination = mb.GetDst();
	pk.m_segsLeft = mb.GetSegsLeft();

	// Cancel passive ack timer
	m_passiveAckTimer[pk].Cancel();
	if (m_passiveAckTimer[pk].IsRunning())
	{
		NS_LOG_DEBUG("Timer not canceled");
	}
	m_passiveAckTimer.erase(pk);

	// Increase the send retry times
	m_passiveRetries = m_passiveCnt[pk];
	if (m_passiveRetries < m_tryPassiveAcks)
	{
		m_passiveCnt[pk] = ++m_passiveRetries;
		SchedulePassivePacketRetry(mb, protocol);
	}
	else
	{
		// This is the first network acknowledgement retry
		// Cancel the passive packet timer now and remove maintenance buffer entry for it
		CancelPassivePacketTimer(mb);
		ScheduleNetworkPacketRetry(mb, true, protocol);
	}
}

int64_t
DsrRouting::AssignStreams(int64_t stream)
{
	NS_LOG_FUNCTION(this << stream);
	m_uniformRandomVariable->SetStream(stream);
	return 1;
}

void
DsrRouting::NetworkScheduleTimerExpire(DsrMaintainBuffEntry& mb, uint8_t protocol)
{
	Ptr<Packet> p = mb.GetPacket()->Copy();
	Ipv4Address source = mb.GetSrc();
	Ipv4Address nextHop = mb.GetNextHop();
	Ipv4Address dst = mb.GetDst();

	NetworkKey networkKey;
	networkKey.m_ackId = mb.GetAckId();
	networkKey.m_ourAdd = mb.GetOurAdd();
	networkKey.m_nextHop = nextHop;
	networkKey.m_source = source;
	networkKey.m_destination = dst;

	// Increase the send retry times
	m_sendRetries = m_addressForwardCnt[networkKey];

	if (m_sendRetries >= m_maxMaintRexmt)
	{
		// Delete all the routes including the links
		m_routeCache->DeleteAllRoutesIncludeLink(m_mainAddress, nextHop, m_mainAddress);
		/*
		 * here we cancel the packet retransmission time for all the packets have next hop address
		 * as nextHop Also salvage the packet for the all the packet destined for the nextHop
		 * address
		 */
		CancelPacketTimerNextHop(nextHop, protocol);
	}
	else
	{
		m_addressForwardCnt[networkKey] = ++m_sendRetries;
		ScheduleNetworkPacketRetry(mb, false, protocol);
	}
}

void
DsrRouting::ForwardPacket(Ptr<const Packet> packet,
						  DsrOptionSRHeader& sourceRoute,
						  const Ipv4Header& ipv4Header,
						  Ipv4Address source,
						  Ipv4Address nextHop,
						  Ipv4Address targetAddress,
						  uint8_t protocol,
						  Ptr<Ipv4Route> route)
{
	NS_LOG_FUNCTION(this << packet << sourceRoute << source << nextHop << targetAddress << (uint32_t)protocol << route);
	NS_ASSERT_MSG(!m_downTarget.IsNull(), "Error, DsrRouting cannot send downward");

	DsrRoutingHeader dsrRoutingHeader;
	dsrRoutingHeader.SetNextHeader(protocol);
	dsrRoutingHeader.SetMessageType(2);
	dsrRoutingHeader.SetSourceId(GetIDfromIP(source));
	dsrRoutingHeader.SetDestId(GetIDfromIP(targetAddress));

	// We get the salvage value in sourceRoute header and set it to route error header if triggered
	// error
	Ptr<Packet> p = packet->Copy();
	uint8_t length = sourceRoute.GetLength();
	dsrRoutingHeader.SetPayloadLength(uint16_t(length) + 2);
	dsrRoutingHeader.AddDsrOption(sourceRoute);
	p->AddHeader(dsrRoutingHeader);

	Ptr<const Packet> mtP = p->Copy();

	Ptr<Ipv4> ipv4 = GetNodeWithAddress(nextHop)->GetObject<Ipv4>();
	uint32_t interface_id = ipv4->GetInterfaceForAddress(nextHop);
	ipv4 = m_node->GetObject<Ipv4>();
	Ipv4Address ourAddress = ipv4->GetAddress(interface_id, 0).GetLocal();
	ipv4 = GetNodeWithAddress(targetAddress)->GetObject<Ipv4>();
	Ipv4Address target_addr = ipv4->GetAddress(interface_id, 0).GetLocal();

	DsrMaintainBuffEntry newEntry(
		/*Packet=*/mtP,
		/*ourAddress=*/ourAddress,
		/*nextHop=*/nextHop,
		/*source=*/source,
		/*destination=*/target_addr, // targetAddress,
		/*ackId=*/m_ackId,
		/*SegsLeft=*/sourceRoute.GetSegmentsLeft(),
		/*expire time=*/m_maxMaintainTime);

	NS_LOG_DEBUG("In Forwordpacket, add DsrMaintainBuffEntry, ourAddress:" << ourAddress << ", nextHop:" << nextHop
																		   << ", source:" << source
																		   << ", destination:" << targetAddress);

	bool result = m_maintainBuffer.Enqueue(newEntry);

	if (result)
	{
		NetworkKey networkKey;
		networkKey.m_ackId = newEntry.GetAckId();
		networkKey.m_ourAdd = newEntry.GetOurAdd();
		networkKey.m_nextHop = newEntry.GetNextHop();
		networkKey.m_source = newEntry.GetSrc();
		networkKey.m_destination = newEntry.GetDst();

		PassiveKey passiveKey;
		passiveKey.m_ackId = 0;
		passiveKey.m_source = newEntry.GetSrc();
		passiveKey.m_destination = newEntry.GetDst();
		passiveKey.m_segsLeft = newEntry.GetSegsLeft();

		LinkKey linkKey;
		linkKey.m_source = newEntry.GetSrc();
		linkKey.m_destination = newEntry.GetDst();
		linkKey.m_ourAdd = newEntry.GetOurAdd();
		linkKey.m_nextHop = newEntry.GetNextHop();

		m_addressForwardCnt[networkKey] = 0;
		m_passiveCnt[passiveKey] = 0;
		m_linkCnt[linkKey] = 0;

		if (m_linkAck)
		{
			ScheduleLinkPacketRetry(newEntry, protocol);
		}
		else
		{
			NS_LOG_LOGIC("Not using link acknowledgment");
			if (nextHop != targetAddress)
			{
				SchedulePassivePacketRetry(newEntry, protocol);
			}
			else
			{
				// This is the first network retry
				ScheduleNetworkPacketRetry(newEntry, true, protocol);
			}
		}
	}
}

void
DsrRouting::SendInitialRequest(Ipv4Address source, Ipv4Address destination, uint8_t protocol)
{
	NS_LOG_FUNCTION(this << source << destination << (uint32_t)protocol);
	NS_ASSERT_MSG(!m_downTarget.IsNull(), "Error, DsrRouting cannot send downward");
	Ptr<Packet> packet = Create<Packet>();
	// Create an empty Ipv4 route ptr
	Ptr<Ipv4Route> route;

	// Construct the route request option header
	DsrRoutingHeader dsrRoutingHeader;
	dsrRoutingHeader.SetNextHeader(protocol);
	dsrRoutingHeader.SetMessageType(1);
	dsrRoutingHeader.SetSourceId(GetIDfromIP(source));
	dsrRoutingHeader.SetDestId(255);

	DsrOptionRreqHeader rreqHeader; // has an alignment of 4n+0
	// 因為要讓 cache 可以 work，所以 source send initial rreq 的時候要把自己的 ip 退回一個
	// 這樣 route 才會變為 .3 -> .1 -> .2 -> .3 的格式，
	// 沒有退的話 route 會是 .1 -> .1 -> .2 -> .3 的格式，用 cache 就會出問題。
    //*
	Ptr<Ipv4> src_ipv4 = m_node->GetObject<Ipv4>();
	int32_t src_int = src_ipv4->GetInterfaceForAddress(source);
	if (--src_int <= 0)
	{
		unsigned int net_card_num = m_node->GetNDevices() - 1;
		src_int = net_card_num;
	}
	Ipv4Address source_hop = src_ipv4->GetAddress(src_int, 0).GetLocal();
	rreqHeader.AddNodeAddress(source_hop);
    //*/
	//rreqHeader.AddNodeAddress(source); // Add our own address in the header
	rreqHeader.SetTarget(destination);
	m_requestId = m_rreqTable->CheckUniqueRreqId(destination); // Check the Id cache for duplicate ones
	rreqHeader.SetId(m_requestId);

	dsrRoutingHeader.AddDsrOption(rreqHeader); // Add the rreqHeader to the dsr extension header
	uint8_t length = rreqHeader.GetLength();
	dsrRoutingHeader.SetPayloadLength(uint16_t(length) + 2);
	packet->AddHeader(dsrRoutingHeader);

	// Schedule the route requests retry with non-propagation set true
	bool nonProp = true;
	std::vector<Ipv4Address> address;
	address.push_back(source);
	address.push_back(destination);
	/*
	 * Add the socket ip ttl tag to the packet to limit the scope of route requests
	 */
	SocketIpTtlTag tag;
	tag.SetTtl(0);
	Ptr<Packet> nonPropPacket = packet->Copy();
	nonPropPacket->AddPacketTag(tag);
	// Increase the request count
	m_rreqTable->FindAndUpdate(destination);
	SendRequest(nonPropPacket, source);
	// Schedule the next route request
	ScheduleRreqRetry(packet, address, nonProp, m_requestId, protocol);
}

void
DsrRouting::SendErrorRequest(DsrOptionRerrUnreachHeader& rerr, uint8_t protocol)
{
	NS_LOG_FUNCTION(this << (uint32_t)protocol);
	NS_ASSERT_MSG(!m_downTarget.IsNull(), "Error, DsrRouting cannot send downward");
	uint8_t salvage = rerr.GetSalvage();
	Ipv4Address dst = rerr.GetOriginalDst();
	NS_LOG_DEBUG("our own address here " << m_mainAddress << " error source " << rerr.GetErrorSrc()
										 << " error destination " << rerr.GetErrorDst() << " error next hop "
										 << rerr.GetUnreachNode() << " original dst " << rerr.GetOriginalDst());
	DsrRouteCacheEntry toDst;
	auto [findRoute, matched_dst_addr] = Lookup_destination_route_with_all_ip(dst, toDst);
	// if (m_routeCache->LookupRoute(dst, toDst))
	if (findRoute)
	{
		/*
		 * Found a route the dst, construct the source route option header
		 */
		DsrOptionSRHeader sourceRoute;
		std::vector<Ipv4Address> ip = toDst.GetVector();
		sourceRoute.SetNodesAddress(ip);
		/// When found a route and use it, UseExtends to the link cache
		if (m_routeCache->IsLinkCache())
		{
			m_routeCache->UseExtends(ip);
		}
		sourceRoute.SetSegmentsLeft((ip.size() - 2));
		sourceRoute.SetSalvage(salvage);
		Ipv4Address nextHop = SearchNextHop_with_all_ip(m_mainAddress, ip); // Get the next hop address
		NS_LOG_DEBUG("The nextHop address " << nextHop);
		Ptr<Packet> packet = Create<Packet>();
		if (nextHop == "0.0.0.0")
		{
			NS_LOG_DEBUG("Error next hop address");
			PacketNewRoute(packet, m_mainAddress, dst, protocol);
			return;
		}
		SetRoute(nextHop, m_mainAddress);
		CancelRreqTimer(dst, true);
		/// Try to send out the packet from the buffer once we found one route
		if (m_sendBuffer.GetSize() != 0 && m_sendBuffer.Find(dst))
		{
			SendPacketFromBuffer(sourceRoute, nextHop, protocol);
		}
		NS_LOG_LOGIC("Route to " << dst << " found");
		return;
	}
	else
	{
		NS_LOG_INFO("No route found, initiate route error request");
		Ptr<Packet> packet = Create<Packet>();
		// Ipv4Address originalDst = rerr.GetOriginalDst();
		//  Create an empty route ptr
		Ptr<Ipv4Route> route = nullptr;
		/*
		 * Construct the route request option header
		 */
		DsrRoutingHeader dsrRoutingHeader;
		dsrRoutingHeader.SetNextHeader(protocol);
		dsrRoutingHeader.SetMessageType(1);
		dsrRoutingHeader.SetSourceId(m_node->GetId());
		dsrRoutingHeader.SetDestId(255);

		Ptr<Packet> dstP = Create<Packet>();
		DsrOptionRreqHeader rreqHeader; // has an alignment of 4n+0
		Ptr<Ipv4> src_ipv4 = m_node->GetObject<Ipv4>();
		int32_t src_int = src_ipv4->GetInterfaceForAddress(rerr.GetErrorDst());
		{
			unsigned int net_card_num = m_node->GetNDevices() - 1;
			src_int = src_int % net_card_num + 1;
		}
		Ipv4Address source = src_ipv4->GetAddress(src_int, 0).GetLocal();
		Ipv4Address originalDst = get_same_channel_Ipv4Address(source, rerr.GetOriginalDst());
		rreqHeader.AddNodeAddress(rerr.GetErrorDst()); // Add our own address in the header
		rreqHeader.SetTarget(originalDst);
		m_requestId = m_rreqTable->CheckUniqueRreqId(originalDst); // Check the Id cache for duplicate ones
		rreqHeader.SetId(m_requestId);

		dsrRoutingHeader.AddDsrOption(rreqHeader); // Add the rreqHeader to the dsr extension header
		dsrRoutingHeader.AddDsrOption(rerr);
		uint8_t length = rreqHeader.GetLength() + rerr.GetLength();
		dsrRoutingHeader.SetPayloadLength(uint16_t(length) + 4);
		dstP->AddHeader(dsrRoutingHeader);
		// Schedule the route requests retry, propagate the route request message as it contains
		// error
		bool nonProp = false;
		std::vector<Ipv4Address> address;
		// address.push_back(m_mainAddress);
		address.push_back(source);
		address.push_back(originalDst);
		/*
		 * Add the socket ip ttl tag to the packet to limit the scope of route requests
		 */
		SocketIpTtlTag tag;
		tag.SetTtl((uint8_t)m_discoveryHopLimit);
		Ptr<Packet> propPacket = dstP->Copy();
		propPacket->AddPacketTag(tag);

		if ((m_addressReqTimer.find(originalDst) == m_addressReqTimer.end()) &&
			(m_nonPropReqTimer.find(originalDst) == m_nonPropReqTimer.end()))
		{
			NS_LOG_INFO("Only when there is no existing route request time when the initial route "
						"request is scheduled");
			SendRequest(propPacket, source);
			ScheduleRreqRetry(dstP, address, nonProp, m_requestId, protocol);
		}
		else
		{
			NS_LOG_INFO("There is existing route request, find the existing route request entry");
			/*
			 * Cancel the route request timer first before scheduling the route request
			 * in this case, we do not want to remove the route request entry, so the isRemove value
			 * is false
			 */
			CancelRreqTimer(originalDst, false);
			ScheduleRreqRetry(dstP, address, nonProp, m_requestId, protocol);
		}
	}
}

void
DsrRouting::CancelRreqTimer_with_all_ip(Ipv4Address dst, bool isRemove)
{
	NS_LOG_FUNCTION(this << dst << isRemove);
	// Cancel the non propagation request timer if found
	bool find_flag = false;
	Ptr<Node> dst_node = GetNodeWithAddress(dst);
	Ptr<Ipv4> dst_ipv4 = dst_node->GetObject<Ipv4>();
	unsigned int net_card_num = dst_node->GetNDevices() - 1;
	for (unsigned int i = 1; i <= net_card_num; ++i)
	{
		Ipv4Address dst_addr = dst_ipv4->GetAddress(i, 0).GetLocal();
		if (m_nonPropReqTimer.find(dst_addr) != m_nonPropReqTimer.end())
		{
			find_flag = true;
			dst = dst_addr;
			break;
		}
	}

	if (!find_flag)
	{
		NS_LOG_DEBUG("Did not find the non-propagation timer");
	}
	else
	{
		NS_LOG_DEBUG("did find the non-propagation timer");
	}
	m_nonPropReqTimer[dst].Cancel();

	if (m_nonPropReqTimer[dst].IsRunning())
	{
		NS_LOG_DEBUG("Timer not canceled");
	}
	m_nonPropReqTimer.erase(dst);

	// Cancel the address request timer if found
	find_flag = false;
	for (unsigned int i = 1; i <= net_card_num; ++i)
	{
		Ipv4Address dst_addr = dst_ipv4->GetAddress(i, 0).GetLocal();
		if (m_addressReqTimer.find(dst_addr) != m_addressReqTimer.end())
		{
			find_flag = true;
			dst = dst_addr;
			break;
		}
	}
	if (!find_flag)
	{
		NS_LOG_DEBUG("Did not find the propagation timer");
	}
	else
	{
		NS_LOG_DEBUG("did find the propagation timer");
	}
	m_addressReqTimer[dst].Cancel();
	if (m_addressReqTimer[dst].IsRunning())
	{
		NS_LOG_DEBUG("Timer not canceled");
	}
	m_addressReqTimer.erase(dst);
	/*
	 * If the route request is scheduled to remove the route request entry
	 * Remove the route request entry with the route retry times done for certain destination
	 */
	if (isRemove)
	{
		// remove the route request entry from route request table
		m_rreqTable->RemoveRreqEntry(dst);
	}
}

void
DsrRouting::CancelRreqTimer(Ipv4Address dst, bool isRemove)
{
	NS_LOG_FUNCTION(this << dst << isRemove);
	// Cancel the non propagation request timer if found
	if (m_nonPropReqTimer.find(dst) == m_nonPropReqTimer.end())
	{
		NS_LOG_DEBUG("Did not find the non-propagation timer");
	}
	else
	{
		NS_LOG_DEBUG("did find the non-propagation timer");
	}
	m_nonPropReqTimer[dst].Cancel();

	if (m_nonPropReqTimer[dst].IsRunning())
	{
		NS_LOG_DEBUG("Timer not canceled");
	}
	m_nonPropReqTimer.erase(dst);

	// Cancel the address request timer if found
	if (m_addressReqTimer.find(dst) == m_addressReqTimer.end())
	{
		NS_LOG_DEBUG("Did not find the propagation timer");
	}
	else
	{
		NS_LOG_DEBUG("did find the propagation timer");
	}
	m_addressReqTimer[dst].Cancel();
	if (m_addressReqTimer[dst].IsRunning())
	{
		NS_LOG_DEBUG("Timer not canceled");
	}
	m_addressReqTimer.erase(dst);
	/*
	 * If the route request is scheduled to remove the route request entry
	 * Remove the route request entry with the route retry times done for certain destination
	 */
	if (isRemove)
	{
		// remove the route request entry from route request table
		m_rreqTable->RemoveRreqEntry(dst);
	}
}

void
DsrRouting::ScheduleRreqRetry(Ptr<Packet> packet,
							  std::vector<Ipv4Address> address,
							  bool nonProp,
							  uint32_t requestId,
							  uint8_t protocol)
{
	NS_LOG_FUNCTION(this << packet << nonProp << requestId << (uint32_t)protocol);
	Ipv4Address source = address[0];
	Ipv4Address dst = address[1];
	if (nonProp)
	{
		// The nonProp route request is only sent out only and is already used
		if (m_nonPropReqTimer.find(dst) == m_nonPropReqTimer.end())
		{
			Timer timer(Timer::CANCEL_ON_DESTROY);
			m_nonPropReqTimer[dst] = timer;
		}
		std::vector<Ipv4Address> address;
		address.push_back(source);
		address.push_back(dst);
		m_nonPropReqTimer[dst].SetFunction(&DsrRouting::RouteRequestTimerExpire, this);
		m_nonPropReqTimer[dst].Cancel();
		m_nonPropReqTimer[dst].SetArguments(packet, address, requestId, protocol);
		m_nonPropReqTimer[dst].Schedule(m_nonpropRequestTimeout);
	}
	else
	{
		// Cancel the non propagation request timer if found
		m_nonPropReqTimer[dst].Cancel();
		if (m_nonPropReqTimer[dst].IsRunning())
		{
			NS_LOG_DEBUG("Timer not canceled");
		}
		m_nonPropReqTimer.erase(dst);

		if (m_addressReqTimer.find(dst) == m_addressReqTimer.end())
		{
			Timer timer(Timer::CANCEL_ON_DESTROY);
			m_addressReqTimer[dst] = timer;
		}
		std::vector<Ipv4Address> address;
		address.push_back(source);
		address.push_back(dst);
		m_addressReqTimer[dst].SetFunction(&DsrRouting::RouteRequestTimerExpire, this);
		m_addressReqTimer[dst].Cancel();
		m_addressReqTimer[dst].SetArguments(packet, address, requestId, protocol);
		Time rreqDelay;
		// back off mechanism for sending route requests
		if (m_rreqTable->GetRreqCnt(dst))
		{
			// When the route request count is larger than 0
			// This is the exponential back-off mechanism for route request
			rreqDelay = Time(std::pow(static_cast<double>(m_rreqTable->GetRreqCnt(dst)), 2.0) * m_requestPeriod);
		}
		else
		{
			// This is the first route request retry
			rreqDelay = m_requestPeriod;
		}
		NS_LOG_LOGIC("Request count for " << dst << " " << m_rreqTable->GetRreqCnt(dst) << " with delay time "
										  << rreqDelay.As(Time::S));
		if (rreqDelay > m_maxRequestPeriod)
		{
			// use the max request period
			NS_LOG_LOGIC("The max request delay time " << m_maxRequestPeriod.As(Time::S));
			m_addressReqTimer[dst].Schedule(m_maxRequestPeriod);
		}
		else
		{
			NS_LOG_LOGIC("The request delay time " << rreqDelay.As(Time::S));
			m_addressReqTimer[dst].Schedule(rreqDelay);
		}
	}
}

void
DsrRouting::RouteRequestTimerExpire(Ptr<Packet> packet,
									std::vector<Ipv4Address> address,
									uint32_t requestId,
									uint8_t protocol)
{
	NS_LOG_FUNCTION(this << packet << requestId << (uint32_t)protocol);
	NS_LOG_DEBUG("Expired at time: " << Simulator::Now().As(Time::S));
	// Get a clean packet without dsr header
	Ptr<Packet> dsrP = packet->Copy();
	DsrRoutingHeader dsrRoutingHeader;
	dsrP->RemoveHeader(dsrRoutingHeader); // Remove the dsr header in whole

	Ipv4Address source = address[0];
	Ipv4Address dst = address[1];
	DsrRouteCacheEntry toDst;
	if (m_routeCache->LookupRoute(dst, toDst))
	{
		/*
		 * Found a route the dst, construct the source route option header
		 */
		DsrOptionSRHeader sourceRoute;
		std::vector<Ipv4Address> ip = toDst.GetVector();
		sourceRoute.SetNodesAddress(ip);
		// When we found the route and use it, UseExtends for the link cache
		if (m_routeCache->IsLinkCache())
		{
			m_routeCache->UseExtends(ip);
		}
		sourceRoute.SetSegmentsLeft((ip.size() - 2));
		/// Set the salvage value to 0
		sourceRoute.SetSalvage(0);
		// Ipv4Address nextHop = SearchNextHop(m_mainAddress, ip); // Get the next hop address
		Ipv4Address nextHop = SearchNextHop_with_all_ip(m_mainAddress, ip); // Get the next hop address
		NS_LOG_INFO("The nextHop address is " << nextHop);
		if (nextHop == "0.0.0.0")
		{
			NS_LOG_DEBUG("Error next hop address");
			PacketNewRoute(dsrP, source, dst, protocol);
			return;
		}
		SetRoute(nextHop, m_mainAddress);
		CancelRreqTimer(dst, true);
		/// Try to send out data packet from the send buffer if found
		if (m_sendBuffer.GetSize() != 0 && m_sendBuffer.Find(dst))
		{
			SendPacketFromBuffer(sourceRoute, nextHop, protocol);
		}
		NS_LOG_LOGIC("Route to " << dst << " found");
		return;
	}
	/*
	 *  If a route discovery has been attempted m_rreqRetries times at the maximum TTL without
	 *  receiving any RREP, all data packets destined for the corresponding destination SHOULD be
	 *  dropped from the buffer and a Destination Unreachable message SHOULD be delivered to the
	 * application.
	 */
	NS_LOG_LOGIC("The new request count for " << dst << " is " << m_rreqTable->GetRreqCnt(dst) << " the max "
											  << m_rreqRetries);
	if (m_rreqTable->GetRreqCnt(dst) >= m_rreqRetries)
	{
		NS_LOG_LOGIC("Route discovery to " << dst << " has been attempted " << m_rreqRetries << " times");
		CancelRreqTimer(dst, true);
		NS_LOG_DEBUG("Route not found. Drop packet with dst " << dst);
		m_sendBuffer.DropPacketWithDst(dst);
	}
	else
	{
		SocketIpTtlTag tag;
		tag.SetTtl((uint8_t)m_discoveryHopLimit);
		Ptr<Packet> propPacket = packet->Copy();
		propPacket->AddPacketTag(tag);
		// Increase the request count
		m_rreqTable->FindAndUpdate(dst);
		SendRequest(propPacket, source);
		NS_LOG_DEBUG("Check the route request entry " << source << " " << dst);
		ScheduleRreqRetry(packet, address, false, requestId, protocol);
	}
}

void
DsrRouting::SendRequest(Ptr<Packet> packet, Ipv4Address source)
{
	NS_LOG_FUNCTION(this << packet << source);

	NS_ASSERT_MSG(!m_downTarget.IsNull(), "Error, DsrRouting cannot send downward");
	/*
	 * The destination address here is directed broadcast address
	 */
	uint32_t priority = GetPriority(DSR_CONTROL_PACKET);
	std::map<uint32_t, Ptr<dsr::DsrNetworkQueue>>::iterator i = m_priorityQueue.find(priority);
	Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
	NS_LOG_LOGIC("Inserting into priority queue number: " << priority);

	// m_downTarget (packet, source, m_broadcast, GetProtocolNumber (), 0);
	// find sub-channel broadcast address
	Ptr<Ipv4> ipv4 = GetNodeWithAddress(source)->GetObject<Ipv4>();
	int32_t src_int = ipv4->GetInterfaceForAddress(source);
	m_broadcast = ipv4->GetAddress(src_int, 0).GetBroadcast();
	NS_LOG_DEBUG("SendRequest source: " << source << ", broadcast: " << m_broadcast);

	/// \todo New DsrNetworkQueueEntry
	DsrNetworkQueueEntry newEntry(packet, source, m_broadcast, Simulator::Now(), nullptr);
	if (dsrNetworkQueue->Enqueue(newEntry))
	{
		Scheduler(priority);
	}
	else
	{
		NS_LOG_INFO("Packet dropped as dsr network queue is full");
	}
}

void
DsrRouting::ScheduleInterRequest(Ptr<Packet> packet)
{
	NS_LOG_FUNCTION(this << packet);
	/*
	 * This is a forwarding case when sending route requests, a random delay time [0,
	 * m_broadcastJitter] used before forwarding as link-layer broadcast
	 */
	NS_ASSERT_MSG(send_interface != 0, "send_interface cannot be zero");
	Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4>();
	Ipv4Address channel_hop_addr = ipv4->GetAddress(send_interface, 0).GetLocal();
	NS_LOG_DEBUG("In ScheduleInterRequest, send_interface: " << send_interface
															 << " channel_hop_addr: " << channel_hop_addr);
	Simulator::Schedule(MilliSeconds(m_uniformRandomVariable->GetInteger(0, m_broadcastJitter)),
						&DsrRouting::SendRequest,
						this,
						packet,
						channel_hop_addr);
}

void
DsrRouting::SendGratuitousReply(Ipv4Address source,
								Ipv4Address srcAddress,
								std::vector<Ipv4Address>& nodeList,
								uint8_t protocol)
{
	NS_LOG_FUNCTION(this << source << srcAddress << (uint32_t)protocol);
	if (!(m_graReply.FindAndUpdate(source, srcAddress,
								   m_gratReplyHoldoff))) // Find the gratuitous reply entry
	{
		NS_LOG_LOGIC("Update gratuitous reply " << source);
		GraReplyEntry graReplyEntry(source, srcAddress, m_gratReplyHoldoff + Simulator::Now());
		m_graReply.AddEntry(graReplyEntry);
		/*
		 * Automatic route shortening
		 */
		m_finalRoute.clear(); // Clear the final route vector
							  /**
							   * Push back the node addresses other than those between srcAddress and our own ip address
							   */
		// FIXME: push_back() 的 address 有可能會造成 rrep packet 的 route 不按照跳頻順序
		// NS_LOG_DEBUG("print nodeList:");
		// PrintVector(nodeList);
		int id = GetIDfromIP(srcAddress);
		std::vector<Ipv4Address>::iterator before =
			find_if(nodeList.begin(), nodeList.end(), [this, id](const Ipv4Address& ip_addr) -> bool {
				if (id == GetIDfromIP(ip_addr))
					return true;
				return false;
			});
		for (std::vector<Ipv4Address>::iterator i = nodeList.begin(); i != before; ++i)
		{
			m_finalRoute.push_back(*i);
		}
		m_finalRoute.push_back(srcAddress);
		id = m_node->GetId();
		std::vector<Ipv4Address>::iterator after =
			find_if(nodeList.begin(), nodeList.end(), [this, id](const Ipv4Address& ip_addr) -> bool {
				if (id == GetIDfromIP(ip_addr))
					return true;
				return false;
			});
		for (std::vector<Ipv4Address>::iterator j = after; j != nodeList.end(); ++j)
		{
			m_finalRoute.push_back(*j);
		}
		NS_LOG_DEBUG("print final route");
		PrintVector(m_finalRoute);
		NS_ASSERT_MSG(false, "DEBUG");

		DsrOptionRrepHeader rrep;
		rrep.SetNodesAddress(m_finalRoute); // Set the node addresses in the route reply header
		// Get the real reply source and destination
		Ipv4Address replySrc = m_finalRoute.back();
		Ipv4Address replyDst = m_finalRoute.front();
		/*
		 * Set the route and use it in send back route reply
		 */
		m_ipv4Route = SetRoute(srcAddress, m_mainAddress);
		/*
		 * This part adds DSR header to the packet and send reply
		 */
		DsrRoutingHeader dsrRoutingHeader;
		dsrRoutingHeader.SetNextHeader(protocol);
		dsrRoutingHeader.SetMessageType(1);
		dsrRoutingHeader.SetSourceId(GetIDfromIP(replySrc));
		dsrRoutingHeader.SetDestId(GetIDfromIP(replyDst));

		uint8_t length = rrep.GetLength(); // Get the length of the rrep header excluding the type header
		dsrRoutingHeader.SetPayloadLength(uint16_t(length) + 2);
		dsrRoutingHeader.AddDsrOption(rrep);
		Ptr<Packet> newPacket = Create<Packet>();
		newPacket->AddHeader(dsrRoutingHeader);
		/*
		 * Send gratuitous reply
		 */
		NS_LOG_INFO("Send back gratuitous route reply");
		SendReply(newPacket, m_mainAddress, srcAddress, m_ipv4Route);
	}
	else
	{
		NS_LOG_INFO("The same gratuitous route reply has already sent");
	}
}

void
DsrRouting::SendReply(Ptr<Packet> packet, Ipv4Address source, Ipv4Address nextHop, Ptr<Ipv4Route> route)
{
	NS_LOG_FUNCTION(this << packet << source << nextHop);
	NS_ASSERT_MSG(!m_downTarget.IsNull(), "Error, DsrRouting cannot send downward");

	Ptr<Ipv4> ipv4 = GetNodeWithAddress(source)->GetObject<Ipv4>();
	int32_t interface_id = ipv4->GetInterfaceForAddress(source);
	Ptr<NetDevice> dev = ipv4->GetNetDevice(interface_id);
	Ipv4Address output_addr = ipv4->GetAddress(interface_id, 0).GetLocal();
	NS_LOG_DEBUG("In SendReply, send RREP source: " << source << " nextHop: " << nextHop
													<< " output device addr: " << output_addr);

	route->SetOutputDevice(dev);
	// NS_LOG_INFO("The output device " << dev << " packet is: " << *packet);

	uint32_t priority = GetPriority(DSR_CONTROL_PACKET);
	std::map<uint32_t, Ptr<dsr::DsrNetworkQueue>>::iterator i = m_priorityQueue.find(priority);
	Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;
	NS_LOG_INFO("Inserting into priority queue number: " << priority);

	// m_downTarget (packet, source, nextHop, GetProtocolNumber (), route);

	/// \todo New DsrNetworkQueueEntry
	DsrNetworkQueueEntry newEntry(packet, source, nextHop, Simulator::Now(), route);
	if (dsrNetworkQueue->Enqueue(newEntry))
	{
		Scheduler(priority);
	}
	else
	{
		NS_LOG_INFO("Packet dropped as dsr network queue is full");
	}
}

void
DsrRouting::ScheduleInitialReply(Ptr<Packet> packet, Ipv4Address source, Ipv4Address nextHop, Ptr<Ipv4Route> route)
{
	NS_LOG_FUNCTION(this << packet << source << nextHop);
	Simulator::ScheduleNow(&DsrRouting::SendReply, this, packet, source, nextHop, route);
}

void
DsrRouting::ScheduleCachedReply(Ptr<Packet> packet,
								Ipv4Address source,
								Ipv4Address destination,
								Ptr<Ipv4Route> route,
								double hops)
{
	NS_LOG_FUNCTION(this << packet << source << destination);
	Simulator::Schedule(Time(2 * m_nodeTraversalTime * (hops - 1 + m_uniformRandomVariable->GetValue(0, 1))),
						&DsrRouting::SendReply,
						this,
						packet,
						source,
						destination,
						route);
}

void
DsrRouting::SendAck(uint16_t ackId,
					Ipv4Address destination,
					Ipv4Address realSrc,
					Ipv4Address realDst,
					uint8_t protocol,
					Ptr<Ipv4Route> route)
{
	NS_LOG_FUNCTION(this << ackId << destination << realSrc << realDst << (uint32_t)protocol << route);
	NS_ASSERT_MSG(!m_downTarget.IsNull(), "Error, DsrRouting cannot send downward");

	// This is a route reply option header
	DsrRoutingHeader dsrRoutingHeader;
	dsrRoutingHeader.SetNextHeader(protocol);
	dsrRoutingHeader.SetMessageType(1);
	dsrRoutingHeader.SetSourceId(GetIDfromIP(m_mainAddress));
	dsrRoutingHeader.SetDestId(GetIDfromIP(destination));

	DsrOptionAckHeader ack;
	/*
	 * Set the ack Id and set the ack source address and destination address
	 */
	ack.SetAckId(ackId);
	ack.SetRealSrc(realSrc);
	ack.SetRealDst(realDst);

	uint8_t length = ack.GetLength();
	dsrRoutingHeader.SetPayloadLength(uint16_t(length) + 2);
	dsrRoutingHeader.AddDsrOption(ack);

	Ptr<Packet> packet = Create<Packet>();
	packet->AddHeader(dsrRoutingHeader);
	Ptr<NetDevice> dev = m_ip->GetNetDevice(m_ip->GetInterfaceForAddress(m_mainAddress));
	route->SetOutputDevice(dev);

	uint32_t priority = GetPriority(DSR_CONTROL_PACKET);
	std::map<uint32_t, Ptr<dsr::DsrNetworkQueue>>::iterator i = m_priorityQueue.find(priority);
	Ptr<dsr::DsrNetworkQueue> dsrNetworkQueue = i->second;

	NS_LOG_LOGIC("Will be inserting into priority queue " << dsrNetworkQueue << " number: " << priority);

	// m_downTarget (packet, m_mainAddress, destination, GetProtocolNumber (), route);

	/// \todo New DsrNetworkQueueEntry
	DsrNetworkQueueEntry newEntry(packet, m_mainAddress, destination, Simulator::Now(), route);
	if (dsrNetworkQueue->Enqueue(newEntry))
	{
		Scheduler(priority);
	}
	else
	{
		NS_LOG_INFO("Packet dropped as dsr network queue is full");
	}
}

enum IpL4Protocol::RxStatus
DsrRouting::Receive(Ptr<Packet> p, const Ipv4Header& ip, Ptr<Ipv4Interface> incomingInterface)
{
	NS_LOG_FUNCTION(this << p << ip << incomingInterface);

	NS_LOG_INFO("Our own IP address " << m_mainAddress << " The incoming interface address " << incomingInterface);
	m_node = GetNode();				// Get the node
	Ptr<Packet> packet = p->Copy(); // Save a copy of the received packet
	/*
	 * When forwarding or local deliver packets, this one should be used always!!
	 */
	DsrRoutingHeader dsrRoutingHeader;
	packet->RemoveHeader(dsrRoutingHeader); // Remove the DSR header in whole
	Ptr<Packet> copy = packet->Copy();

	uint8_t protocol = dsrRoutingHeader.GetNextHeader();
	uint32_t sourceId = dsrRoutingHeader.GetSourceId();
	Ipv4Address source = GetIPfromID(sourceId);
	// NS_LOG_INFO("The source address " << source << " with source id " << sourceId);
	NS_LOG_INFO("The source id " << sourceId);
	/*
	 * Get the IP source and destination address
	 */
	Ipv4Address src = ip.GetSource();

	Ipv4Address dst = ip.GetDestination();
	NS_LOG_INFO("Received packet from src: " << src << " to dst: " << dst);
	NS_LOG_INFO("protocol: " << (int)protocol);
	if (dst == Ipv4Address::GetBroadcast())
	{
		// received a global broadcast packet
		NS_LOG_INFO("Received a global broadcast packet.");
		int broadcast_protocol = m_node->GetObject<UdpL4Protocol>()->GetProtocolNumber();
	}

	bool isPromisc = false;
	uint32_t offset = dsrRoutingHeader.GetDsrOptionsOffset(); // Get the offset for option header, 8 bytes in this case

	// This packet is used to peek option type
	p->RemoveAtStart(offset);

	Ptr<dsr::DsrOptions> dsrOption;
	DsrOptionHeader dsrOptionHeader;
	/*
	 * Peek data to get the option type as well as length and segmentsLeft field
	 */
	uint32_t size = p->GetSize();
	uint8_t* data = new uint8_t[size];
	p->CopyData(data, size);

	uint8_t optionType = 0;
	uint8_t optionLength = 0;
	uint8_t segmentsLeft = 0;

	optionType = *(data);
	NS_LOG_LOGIC("The option type value " << (uint32_t)optionType << " with packet id " << p->GetUid());
	dsrOption = GetOption(optionType); // Get the relative dsr option and demux to the process function
	Ipv4Address promiscSource;		   /// this is just here for the sake of passing in the promisc source
	Ipv4Address our_addr = incomingInterface->GetAddress(0).GetLocal();
	if (optionType == 1) // This is the request option
	{
		BlackList* blackList = m_rreqTable->FindUnidirectional(src);
		if (blackList)
		{
			NS_LOG_INFO("Discard this packet due to unidirectional link");
			m_dropTrace(p);
		}

		NS_LOG_DEBUG("receive RREQ, our_addr: " << our_addr << ", dst IP: " << dst << ", src IP: " << src);
		dsrOption = GetOption(optionType);
		optionLength = dsrOption->Process(p, packet, our_addr, src, ip, protocol, isPromisc, promiscSource);

		if (optionLength == 0)
		{
			NS_LOG_INFO("Discard this packet");
			m_dropTrace(p);
		}
	}
	else if (optionType == 2) // This is route reply option
	{
		dsrOption = GetOption(optionType);
		NS_LOG_DEBUG("receive RREP, our_addr: " << our_addr << ", dst IP: " << ip.GetDestination()
												<< ", src IP: " << src);
		// optionLength =
		//     dsrOption->Process(p, packet, our_addr, src, ip, protocol, isPromisc, promiscSource);
		optionLength = dsrOption->Process(p, packet, our_addr, source, ip, protocol, isPromisc, promiscSource);

		if (optionLength == 0)
		{
			NS_LOG_INFO("Discard this packet");
			m_dropTrace(p);
		}
	}

	else if (optionType == 32) // This is the ACK option
	{
		NS_LOG_INFO("This is the ack option");
		dsrOption = GetOption(optionType);
		optionLength = dsrOption->Process(p, packet, m_mainAddress, source, ip, protocol, isPromisc, promiscSource);

		if (optionLength == 0)
		{
			NS_LOG_INFO("Discard this packet");
			m_dropTrace(p);
		}
	}

	else if (optionType == 3) // This is a route error header
	{
		// populate this route error
		NS_LOG_DEBUG("receive RERR, our_addr: " << our_addr << ", dst IP: " << dst << ", src IP: " << src);
		NS_LOG_INFO("The option type value " << (uint32_t)optionType);

		dsrOption = GetOption(optionType);
		optionLength = dsrOption->Process(p, packet, m_mainAddress, source, ip, protocol, isPromisc, promiscSource);

		if (optionLength == 0)
		{
			NS_LOG_INFO("Discard this packet");
			m_dropTrace(p);
		}
		NS_LOG_INFO("The option Length " << (uint32_t)optionLength);
	}

	else if (optionType == 96) // This is the source route option
	{
		dsrOption = GetOption(optionType);
		NS_LOG_DEBUG("receive SR at time: " << Simulator::Now().As(Time::S));
		NS_LOG_DEBUG("our_addr: " << our_addr << ", dst IP: " << ip.GetDestination() << ", src IP: " << src);
		optionLength = dsrOption->Process(p, packet, our_addr, src, ip, protocol, isPromisc, promiscSource);
		segmentsLeft = *(data + 3);
		if (optionLength == 0)
		{
			NS_LOG_INFO("Discard this packet");
			m_dropTrace(p);
		}
		else
		{
			if (segmentsLeft == 0)
			{
				// / Get the next header
				uint8_t nextHeader = dsrRoutingHeader.GetNextHeader();
				Ptr<Ipv4L3Protocol> l3proto = m_node->GetObject<Ipv4L3Protocol>();
				Ptr<IpL4Protocol> nextProto = l3proto->GetProtocol(nextHeader);
				if (nextProto)
				{
					// we need to make a copy in the unlikely event we hit the
					// RX_ENDPOINT_UNREACH code path
					// Here we can use the packet that has been get off whole DSR header
					enum IpL4Protocol::RxStatus status = nextProto->Receive(copy, ip, incomingInterface);
					NS_LOG_DEBUG("The receive status " << status);
					switch (status)
					{
					case IpL4Protocol::RX_OK:
					// fall through
					case IpL4Protocol::RX_ENDPOINT_CLOSED:
					// fall through
					case IpL4Protocol::RX_CSUM_FAILED:
						break;
					case IpL4Protocol::RX_ENDPOINT_UNREACH:
						if (ip.GetDestination().IsBroadcast() == true || ip.GetDestination().IsMulticast() == true)
						{
							break; // Do not reply to broadcast or multicast
						}
						// Another case to suppress ICMP is a subnet-directed broadcast
					}
					return status;
				}
				else
				{
					NS_FATAL_ERROR("Should not have 0 next protocol value");
				}
			}
			else
			{
				NS_LOG_INFO("This is not the final destination, the packet has already been "
							"forward to next hop");
			}
		}
	}
	else
	{
		NS_LOG_LOGIC("Unknown Option. Drop!");
		/*
		 * Initialize the salvage value to 0
		 */
		uint8_t salvage = 0;

		DsrOptionRerrUnsupportHeader rerrUnsupportHeader;
		rerrUnsupportHeader.SetErrorType(3);			// The error type 3 means Option not supported
		rerrUnsupportHeader.SetErrorSrc(m_mainAddress); // The error source address is our own address
		rerrUnsupportHeader.SetUnsupported(optionType); // The unsupported option type number
		rerrUnsupportHeader.SetErrorDst(src);	 // Error destination address is the destination of the data packet
		rerrUnsupportHeader.SetSalvage(salvage); // Set the value about whether to salvage a packet or not

		/*
		 * The unknown option error is not supported currently in this implementation, and it's also
		 * not likely to happen in simulations
		 */
		//            SendError (rerrUnsupportHeader, 0, protocol); // Send the error packet
	}
	return IpL4Protocol::RX_OK;
}

enum IpL4Protocol::RxStatus
DsrRouting::Receive(Ptr<Packet> p, const Ipv6Header& ip, Ptr<Ipv6Interface> incomingInterface)
{
	NS_LOG_FUNCTION(this << p << ip.GetSource() << ip.GetDestination() << incomingInterface);
	return IpL4Protocol::RX_ENDPOINT_UNREACH;
}

void
DsrRouting::SetDownTarget(DownTargetCallback callback)
{
	m_downTarget = callback;
}

void
DsrRouting::SetDownTarget6(DownTargetCallback6 callback)
{
	NS_FATAL_ERROR("Unimplemented");
}

IpL4Protocol::DownTargetCallback
DsrRouting::GetDownTarget() const
{
	return m_downTarget;
}

IpL4Protocol::DownTargetCallback6
DsrRouting::GetDownTarget6() const
{
	NS_FATAL_ERROR("Unimplemented");
	return MakeNullCallback<void, Ptr<Packet>, Ipv6Address, Ipv6Address, uint8_t, Ptr<Ipv6Route>>();
}

void
DsrRouting::Insert(Ptr<dsr::DsrOptions> option)
{
	m_options.push_back(option);
}

Ptr<dsr::DsrOptions>
DsrRouting::GetOption(int optionNumber)
{
	for (DsrOptionList_t::iterator i = m_options.begin(); i != m_options.end(); ++i)
	{
		if ((*i)->GetOptionNumber() == optionNumber)
		{
			return *i;
		}
	}
	return nullptr;
}
} /* namespace dsr */
} /* namespace ns3 */
