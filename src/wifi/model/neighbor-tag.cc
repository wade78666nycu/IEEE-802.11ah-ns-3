#include "ns3/neighbor-tag.h"
#include "ns3/core-module.h"

namespace ns3 {


// ---------- HelloBeaconTag ----------

HelloBeaconTag::HelloBeaconTag() {}

TypeId
HelloBeaconTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::HelloBeaconTag")
                            .SetParent<Tag>()
                            .AddConstructor<HelloBeaconTag>();
    return tid;
}

TypeId
HelloBeaconTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
HelloBeaconTag::GetSerializedSize() const
{
    return 0;
}

void
HelloBeaconTag::Serialize(TagBuffer) const
{
}

void
HelloBeaconTag::Deserialize(TagBuffer)
{
}

void
HelloBeaconTag::Print(std::ostream& os) const
{
    os << "HelloBeaconTag";
}

// ---------- IsNeighborTag ----------

IsNeighborTag::IsNeighborTag(): m_is_neighbor(false) {}
IsNeighborTag::IsNeighborTag(bool is_neighbor): m_is_neighbor(is_neighbor) {}

TypeId
IsNeighborTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::IsNeighborTag")
                            .SetParent<Tag>()
                            .AddConstructor<IsNeighborTag>()
                            .AddAttribute("IsNeighbor",
                                          "set to true if the sender node of the packet is a neighbor",
                                          BooleanValue(true),
                                          MakeBooleanAccessor(&IsNeighborTag::GetIsNeighbor),
                                          MakeBooleanChecker());
    return tid;
}

TypeId
IsNeighborTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
IsNeighborTag::GetSerializedSize() const
{
    return 1;
}

void
IsNeighborTag::Serialize(TagBuffer i) const
{
    i.WriteU8(m_is_neighbor);
}

void
IsNeighborTag::Deserialize(TagBuffer i)
{
    m_is_neighbor = i.ReadU8();
}

void
IsNeighborTag::Print(std::ostream& os) const
{
    os << "is_neighbor: " << (bool)m_is_neighbor;
}

void
IsNeighborTag::SetIsNeighbor(bool is_neighbor)
{
    m_is_neighbor = is_neighbor;
}

bool
IsNeighborTag::GetIsNeighbor() const
{
    return m_is_neighbor;
}

}
