#ifndef SHARED_TRACE_H
#define SHARED_TRACE_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <string>

using namespace ns3;

void SetTraceOutputPrefix(const std::string& prefix);

void InitializeTracing(bool enable_tx_rate_trace, bool enable_energy_trace);

void TraceWifiTxRate(std::string context,
                     const Ptr<const Packet> packet,
                     uint16_t channelFreqMhz,
                     uint16_t channelNumber,
                     uint32_t rate,
                     bool isShortPreamble,
                     WifiTxVector txvector);

void TraceTxEnergy(std::string context,
                   const Ptr<const Packet> packet,
                   uint16_t channelFreqMhz,
                   uint16_t channelNumber,
                   uint32_t rate,
                   bool isShortPreamble,
                   WifiTxVector txvector);

void TraceMacTxRtsFailed(std::string context, Mac48Address address);
void TraceMacTxDataFailed(std::string context, Mac48Address address);
void TraceMacTxFinalRtsFailed(std::string context, Mac48Address address);
void TraceMacTxFinalDataFailed(std::string context, Mac48Address address);

void ExportNodePosition(const NodeContainer& node_container);
void ExportNodePower(const NodeContainer& node_container);
void ExportEttMatrix(const NodeContainer& node_container);
void ExportEnergyConsumption(const NodeContainer& node_container);
void ExportMacRetrySummary();

void CloseTraceFiles();

#endif // SHARED_TRACE_H
