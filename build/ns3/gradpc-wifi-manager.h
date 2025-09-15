#pragma once
#include "neighbor-tag.h"
#include "wifi-mac-header.h"
#include "wifi-remote-station-manager.h"

// same as constantratewifimanager, only difference is changing the power
// when returning rrep when next hop is not in the neighbor
namespace ns3
{

class WifiPhy;

class GradPCWifiManager : public WifiRemoteStationManager
{
  public:
	static TypeId GetTypeId(void);
	GradPCWifiManager();
	virtual ~GradPCWifiManager();
	virtual WifiTxVector GetDataTxVector(Mac48Address address,
										 const WifiMacHeader* header,
										 Ptr<const Packet> packet,
										 uint32_t fullPacketSize);

  private:
	// overriden from base class
	virtual WifiRemoteStation* DoCreateStation(void) const;
	virtual void DoReportRxOk(WifiRemoteStation* station, double rxSnr, WifiMode txMode);
	virtual void DoReportRtsFailed(WifiRemoteStation* station);
	virtual void DoReportDataFailed(WifiRemoteStation* station);
	virtual void DoReportRtsOk(WifiRemoteStation* station, double ctsSnr, WifiMode ctsMode, double rtsSnr);
	virtual void DoReportDataOk(WifiRemoteStation* station, double ackSnr, WifiMode ackMode, double dataSnr);
	virtual void DoReportFinalRtsFailed(WifiRemoteStation* station);
	virtual void DoReportFinalDataFailed(WifiRemoteStation* station);
	virtual WifiTxVector DoGetDataTxVector(WifiRemoteStation* station, uint32_t size, const unsigned int power_level);
	virtual WifiTxVector DoGetDataTxVector(WifiRemoteStation* station, uint32_t size);
	virtual WifiTxVector DoGetRtsTxVector(WifiRemoteStation* station);
	virtual bool IsLowLatency(void) const;

	WifiMode m_dataMode; //!< Wifi mode for unicast DATA frames
	WifiMode m_ctlMode;	 //!< Wifi mode for RTS frames
};
} // namespace ns3
