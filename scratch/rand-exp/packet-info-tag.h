#pragma once
#include "ns3/core-module.h"
#include "ns3/tag.h"

using namespace ns3;

class PacketInfoTag : public Tag
{
  public:
	/**
	 * \brief Get the type ID.
	 * \return the object TypeId
	 */
	static TypeId GetTypeId();
	TypeId GetInstanceTypeId() const override;
	uint32_t GetSerializedSize() const override;
	void Serialize(TagBuffer i) const override;
	void Deserialize(TagBuffer i) override;
	void Print(std::ostream& os) const override;

	void SetSrcId(const unsigned int src_id);
	void SetPacketId(const unsigned int pkt_id);
	void SetStartTime(const Time start);

	unsigned int GetSrcId() const;
	unsigned int GetPacketId() const;
	Time GetStartTime() const;

  private:
	uint16_t m_src_node_id;
	uint16_t m_packet_id;
	Time m_start_time; //!< tag value
};
