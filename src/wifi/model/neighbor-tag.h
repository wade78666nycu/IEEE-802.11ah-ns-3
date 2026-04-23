#pragma once
#include "ns3/tag.h"

namespace ns3 {

class HelloBeaconTag : public Tag
{
  public:
    HelloBeaconTag();
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer i) const override;
    void Deserialize(TagBuffer i) override;
    void Print(std::ostream& os) const override;
};

class IsNeighborTag : public Tag
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    IsNeighborTag();
    IsNeighborTag(bool is_neighbor);
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer i) const override;
    void Deserialize(TagBuffer i) override;
    void Print(std::ostream& os) const override;

    // these are our accessors to our tag structure
    /**
     * Set the tag value
     * \param value The tag value.
     */
    void SetIsNeighbor(bool is_neighbor);

    /**
     * Get the tag value
     * \return the tag value.
     */
    bool GetIsNeighbor() const;

  private:
    bool m_is_neighbor; //!< tag value
};

}
