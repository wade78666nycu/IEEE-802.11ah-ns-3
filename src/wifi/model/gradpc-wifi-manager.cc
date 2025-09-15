#include "gradpc-wifi-manager.h"

#include "ns3/assert.h"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/tag.h"
#include "ns3/wifi-phy.h"

#define Min(a, b) ((a < b) ? a : b)

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("GradPCWifiManager");

NS_OBJECT_ENSURE_REGISTERED(GradPCWifiManager);

TypeId
GradPCWifiManager::GetTypeId(void)
{
	static TypeId tid = TypeId("ns3::GradPCWifiManager")
							.SetParent<WifiRemoteStationManager>()
							.SetGroupName("Wifi")
							.AddConstructor<GradPCWifiManager>()
							.AddAttribute("DataMode",
										  "The transmission mode to use for every data packet transmission",
										  StringValue("OfdmRate6Mbps"),
										  MakeWifiModeAccessor(&GradPCWifiManager::m_dataMode),
										  MakeWifiModeChecker())
							.AddAttribute("ControlMode",
										  "The transmission mode to use for every RTS packet transmission.",
										  StringValue("OfdmRate6Mbps"),
										  MakeWifiModeAccessor(&GradPCWifiManager::m_ctlMode),
										  MakeWifiModeChecker());
	return tid;
}

GradPCWifiManager::GradPCWifiManager()
{
	NS_LOG_FUNCTION(this);
}

GradPCWifiManager::~GradPCWifiManager()
{
	NS_LOG_FUNCTION(this);
}

WifiTxVector
GradPCWifiManager::GetDataTxVector(Mac48Address address,
								   const WifiMacHeader* header,
								   Ptr<const Packet> packet,
								   uint32_t fullPacketSize)
{
	NS_LOG_FUNCTION(this << address << *header << packet << fullPacketSize);
	// NS_LOG_UNCOND("GradPCWifiManager::GetDataTxVector called");
	// NS_LOG_UNCOND("m_defaultTxPowerLevel: " << (int)m_defaultTxPowerLevel);

	enum WifiMacType sigheader = WIFI_MAC_EXTENSION_S1G_BEACON;

	WifiTxVector v;
	if (address.IsGroup()) // use temporary, need change
	{
		if (header->GetType() == sigheader)
			v.SetMode(WifiPhy::GetOfdmRate300KbpsBW1MHz()); // maybe should be 150k
		else
			v.SetMode(GetNonUnicastMode());

		v.SetTxPowerLevel(m_defaultTxPowerLevel);
		v.SetShortGuardInterval(false);
		v.SetNss(1);
		v.SetNess(0);
		v.SetStbc(false);
		return v;
	}

	// m_defaultTxPowerLevel is 0
	IsNeighborTag neighbor_tag;
	if (packet->PeekPacketTag(neighbor_tag))
	{
		if (neighbor_tag.GetIsNeighbor() == false)
		{
			// next hop is not a neighbor, use default power to send packet
			uint32_t power_level = 1;
            //NS_LOG_DEBUG("power_level: " << power_level);
			return DoGetDataTxVector(Lookup(address, header), fullPacketSize, power_level);
		}
	}
	return DoGetDataTxVector(Lookup(address, header), fullPacketSize);
}

WifiRemoteStation*
GradPCWifiManager::DoCreateStation(void) const
{
	NS_LOG_FUNCTION(this);
	WifiRemoteStation* station = new WifiRemoteStation();
	return station;
}

void
GradPCWifiManager::DoReportRxOk(WifiRemoteStation* station, double rxSnr, WifiMode txMode)
{
	NS_LOG_FUNCTION(this << station << rxSnr << txMode);
}

void
GradPCWifiManager::DoReportRtsFailed(WifiRemoteStation* station)
{
	NS_LOG_FUNCTION(this << station);
}

void
GradPCWifiManager::DoReportDataFailed(WifiRemoteStation* station)
{
	NS_LOG_FUNCTION(this << station);
}

void
GradPCWifiManager::DoReportRtsOk(WifiRemoteStation* st, double ctsSnr, WifiMode ctsMode, double rtsSnr)
{
	NS_LOG_FUNCTION(this << st << ctsSnr << ctsMode << rtsSnr);
}

void
GradPCWifiManager::DoReportDataOk(WifiRemoteStation* st, double ackSnr, WifiMode ackMode, double dataSnr)
{
	NS_LOG_FUNCTION(this << st << ackSnr << ackMode << dataSnr);
}

void
GradPCWifiManager::DoReportFinalRtsFailed(WifiRemoteStation* station)
{
	NS_LOG_FUNCTION(this << station);
}

void
GradPCWifiManager::DoReportFinalDataFailed(WifiRemoteStation* station)
{
	NS_LOG_FUNCTION(this << station);
}

WifiTxVector
GradPCWifiManager::DoGetDataTxVector(WifiRemoteStation* st, uint32_t size)
{
	NS_LOG_FUNCTION(this << st << size);
	return WifiTxVector(m_dataMode,
						GetDefaultTxPowerLevel(),
						GetLongRetryCount(st),
						GetShortGuardInterval(st),
						Min(GetNumberOfReceiveAntennas(st), GetNumberOfTransmitAntennas()),
						GetNess(st),
						GetStbc(st));
}

WifiTxVector
GradPCWifiManager::DoGetDataTxVector(WifiRemoteStation* st, uint32_t size, const unsigned int power_level)
{
	NS_LOG_FUNCTION(this << st << size);
	// NS_LOG_UNCOND("DoGetDataTxVector power_level: " << power_level);
	return WifiTxVector(m_dataMode,
						power_level,
						GetLongRetryCount(st),
						GetShortGuardInterval(st),
						Min(GetNumberOfReceiveAntennas(st), GetNumberOfTransmitAntennas()),
						GetNess(st),
						GetStbc(st));
}

WifiTxVector
GradPCWifiManager::DoGetRtsTxVector(WifiRemoteStation* st)
{
	NS_LOG_FUNCTION(this << st);
	return WifiTxVector(m_ctlMode,
						GetDefaultTxPowerLevel(),
						GetShortRetryCount(st),
						GetShortGuardInterval(st),
						Min(GetNumberOfReceiveAntennas(st), GetNumberOfTransmitAntennas()),
						GetNess(st),
						GetStbc(st));
}

bool
GradPCWifiManager::IsLowLatency(void) const
{
	NS_LOG_FUNCTION(this);
	return true;
}

} // namespace ns3
