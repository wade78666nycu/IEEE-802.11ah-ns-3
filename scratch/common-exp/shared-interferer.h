#pragma once

// NOTE: This header must be included AFTER NS_LOG_COMPONENT_DEFINE and
// scenario-core.h in the translation unit (same pattern as other shared-*.h).

#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/wifi-module.h"

#include <functional>
#include <vector>

namespace ns3
{
// ---------------------------------------------------------------------------
// MakeMultiJammerHook
// Creates one SEPARATE jammer node per channel in channel_indices.
// Each jammer is co-located with near_node_id and shares that channel's
// YansWifiChannel object → causes CSMA/CA contention for nearby nodes.
//
// near_node_id    : reference node to co-locate jammers with
// channel_indices : list of 0-based channel indices to jam (e.g. {0,1,2})
// data_rate_kbps  : app-layer rate per jammer. Set close to PHY rate (300 Kbps)
//                   for maximum duty cycle. Default 280 Kbps ≈ 93% duty cycle.
// ---------------------------------------------------------------------------
inline std::function<void(NodeContainer&, std::vector<Ptr<YansWifiChannel>>&, const ScenarioConfig&)>
MakeMultiJammerHook(const uint32_t near_node_id,
                    const std::vector<uint32_t> channel_indices = {0},
                    const double data_rate_kbps = 200.0)
{
    return [near_node_id, channel_indices, data_rate_kbps](
               NodeContainer& nodes,
               std::vector<Ptr<YansWifiChannel>>& channels,
               const ScenarioConfig& cfg)
    {
        Ptr<MobilityModel> ref_mob = nodes.Get(near_node_id)->GetObject<MobilityModel>();
        NS_ASSERT_MSG(ref_mob, "Reference node has no MobilityModel");
        Vector pos = ref_mob->GetPosition();

        // ETT re-measurement Phase 3: t=12s-20s (power_control=on), t=1s-9s (off)
        double start_s = cfg.enable_hello ? (cfg.enable_power_control ? 12.0 : 1.0) : 0.5;

        // Stagger jammer start times so their TX windows tile the packet
        // interval with minimal overlap. TX time ≈ pkt_bits / 300 Kbps.
        // Offset each by TX_time so they form back-to-back transmissions.
        double tx_time_s = (256.0 * 8.0) / 300000.0;  // ≈ 6.83 ms
        uint32_t ch_order = 0;

        for (uint32_t channel_idx : channel_indices)
        {
            NS_ASSERT_MSG(channel_idx < channels.size(), "channel_idx out of range");

            NodeContainer jammer;
            jammer.Create(1);

            MobilityHelper mobility;
            Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
            posAlloc->Add(pos);
            mobility.SetPositionAllocator(posAlloc);
            mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
            mobility.Install(jammer);

            WifiHelper wifi;
            wifi.SetStandard(WIFI_PHY_STANDARD_80211ah);
            wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                         "DataMode",    StringValue("OfdmRate300KbpsBW1MHz"),
                                         "ControlMode", StringValue("OfdmRate300KbpsBW1MHz"));

            YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();
            wifiPhy.Set("ShortGuardEnabled",        BooleanValue(false));
            wifiPhy.Set("ChannelWidth",             UintegerValue(1));
            wifiPhy.Set("TxPowerStart",             DoubleValue(8));
            wifiPhy.Set("TxPowerEnd",               DoubleValue(8));
            wifiPhy.Set("TxPowerLevels",            UintegerValue(2));
            wifiPhy.Set("RxNoiseFigure",            DoubleValue(cfg.rx_noise_figure));
            wifiPhy.Set("LdpcEnabled",              BooleanValue(false));
            wifiPhy.Set("S1g1MfieldEnabled",        BooleanValue(true));
            wifiPhy.Set("Frequency",                UintegerValue(920));
            // Set CCA threshold very high so jammer never defers to others.
            // This makes it a "rude" transmitter that ignores CSMA/CA.
            wifiPhy.Set("EnergyDetectionThreshold", DoubleValue(100.0));
            wifiPhy.Set("CcaMode1Threshold",        DoubleValue(100.0));
            wifiPhy.SetChannel(channels[channel_idx]);

            S1gWifiMacHelper wifiMac = S1gWifiMacHelper();
            wifiMac.SetType("ns3::AdhocWifiMac", "S1gSupported", BooleanValue(true));
            NetDeviceContainer jamDevs = wifi.Install(wifiPhy, wifiMac, jammer);

            // Eliminate all MAC-layer delays: set CW=0 and AIFSN=0
            // so jammer transmits back-to-back with zero backoff.
            Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(jamDevs.Get(0));
            Ptr<RegularWifiMac> mac = DynamicCast<RegularWifiMac>(wifiDev->GetMac());
            Ptr<DcaTxop> dca = mac->GetDcaTxopPublic();
            dca->SetMinCw(0);
            dca->SetMaxCw(0);
            dca->SetAifsn(0);

            Ptr<YansWifiPhy> jamPhy = DynamicCast<YansWifiPhy>(wifiDev->GetPhy());
            NS_LOG_UNCOND("[Jammer] ch_idx=" << channel_idx
                          << " jamPhy channel ptr=" << jamPhy->GetChannel()
                          << " expected=" << channels[channel_idx]);

            PacketSocketHelper psHelper;
            psHelper.Install(jammer);

            PacketSocketAddress psAddr;
            psAddr.SetAllDevices();
            psAddr.SetPhysicalAddress(Mac48Address::GetBroadcast());
            psAddr.SetProtocol(0x0800);

            OnOffHelper onoff("ns3::PacketSocketFactory", Address(psAddr));
            onoff.SetAttribute("DataRate",   DataRateValue(DataRate(static_cast<uint64_t>(data_rate_kbps * 1000))));
            onoff.SetAttribute("PacketSize", UintegerValue(256));
            onoff.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1e9]"));
            onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

            ApplicationContainer apps = onoff.Install(jammer.Get(0));
            double this_start = start_s + tx_time_s * ch_order;
            apps.Start(Seconds(this_start));
            apps.Stop(Seconds(20));
            ch_order++;

            // Enable pcap tracing to verify jammer is transmitting
            wifiPhy.EnablePcap("jammer-ch" + std::to_string(channel_idx), jamDevs.Get(0), true);

            NS_LOG_UNCOND("[Jammer] ch_idx=" << channel_idx
                          << " near_node=" << near_node_id
                          << " pos=(" << pos.x << "," << pos.y << ")"
                          << " start=" << this_start << "s"
                          << " rate=" << data_rate_kbps << "Kbps");
        }
    };
}

} // namespace ns3
