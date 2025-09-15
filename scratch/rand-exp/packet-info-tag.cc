#include "packet-info-tag.h"

TypeId
PacketInfoTag::GetTypeId()
{
	static TypeId tid = TypeId("ns3::PacketInfoTag").SetParent<Tag>().AddConstructor<PacketInfoTag>();
	return tid;
}

TypeId
PacketInfoTag::GetInstanceTypeId() const
{
	return GetTypeId();
}

uint32_t
PacketInfoTag::GetSerializedSize() const
{
	return sizeof(m_src_node_id) + sizeof(m_packet_id) + sizeof(double);
}

void
PacketInfoTag::Serialize(TagBuffer i) const
{
	i.WriteU16(m_src_node_id);
	i.WriteU16(m_packet_id);
	i.WriteDouble(m_start_time.GetDouble());
}

void
PacketInfoTag::Deserialize(TagBuffer i)
{
	m_src_node_id = i.ReadU16();
	m_packet_id = i.ReadU16();
	m_start_time = Time(i.ReadDouble());
}

void
PacketInfoTag::Print(std::ostream& os) const
{
	os << "source node id: " << m_src_node_id << '\n'
	   << "packet id: " << m_packet_id << '\n'
	   << "start time: " << m_start_time.GetDouble() << '\n';
}

void
PacketInfoTag::SetStartTime(Time start)
{
	m_start_time = start;
}

void
PacketInfoTag::SetSrcId(const unsigned int src_id)
{
	m_src_node_id = src_id;
}

void
PacketInfoTag::SetPacketId(const unsigned int pkt_id)
{
	m_packet_id = pkt_id;
}

unsigned int
PacketInfoTag::GetSrcId() const
{
	return m_src_node_id;
}

unsigned int
PacketInfoTag::GetPacketId() const
{
	return m_packet_id;
}

Time
PacketInfoTag::GetStartTime() const
{
	return m_start_time;
}
