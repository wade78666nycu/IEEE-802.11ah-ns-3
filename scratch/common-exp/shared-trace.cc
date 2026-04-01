#include "shared-trace.h"

#include "ns3/hello-beacon.h"
#include "ns3/yans-wifi-phy.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>

using namespace ns3;

static std::ofstream g_tx_rate_file;
static std::ofstream g_tx_energy_file;
static std::map<uint32_t, double> g_node_tx_energy;
static std::string g_output_prefix = "exp";
static uint64_t g_mac_short_retry_count = 0;
static uint64_t g_mac_long_retry_count = 0;
static uint64_t g_mac_final_rts_failed_count = 0;
static uint64_t g_mac_final_data_failed_count = 0;

static std::string
PrefixFile(const std::string& suffix)
{
    return "output_file/" + g_output_prefix + "/" + g_output_prefix + "_" + suffix;
}

void
SetTraceOutputPrefix(const std::string& prefix)
{
    g_output_prefix = prefix.empty() ? "exp" : prefix;
}

void
InitializeTracing(bool enable_tx_rate_trace, bool enable_energy_trace)
{
    if (enable_tx_rate_trace)
    {
        g_tx_rate_file.open(PrefixFile("tx_rate.txt"), std::ios::trunc);
    }   
    if (enable_energy_trace)
    {
        g_tx_energy_file.open(PrefixFile("tx_energy.txt"), std::ios::trunc);
    }
    g_node_tx_energy.clear();
    g_mac_short_retry_count = 0;
    g_mac_long_retry_count = 0;
    g_mac_final_rts_failed_count = 0;
    g_mac_final_data_failed_count = 0;
}

void
TraceWifiTxRate(std::string context,
                const Ptr<const Packet> packet,
                uint16_t channelFreqMhz,
                uint16_t channelNumber,
                uint32_t rate,
                bool isShortPreamble,
                WifiTxVector txvector)
{
    (void)channelFreqMhz;
    (void)rate;

    if (packet->GetSize() < 200 || !g_tx_rate_file.is_open())
    {
        return;
    }

    const double mbps = static_cast<double>(txvector.GetMode().GetDataRate()) / 1e6;
    g_tx_rate_file << "[tx-rate] " << context << " ch=" << channelNumber
                   << " mode=" << txvector.GetMode().GetUniqueName()
                   << " rate=" << std::fixed << std::setprecision(3) << mbps << "Mbps"
                   << " size=" << packet->GetSize() << "B"
                   << " preamble=" << (isShortPreamble ? "short" : "long") << '\n';
    g_tx_rate_file.flush();
}

void
TraceTxEnergy(std::string context,
              const Ptr<const Packet> packet,
              uint16_t channelFreqMhz,
              uint16_t channelNumber,
              uint32_t rate,
              bool isShortPreamble,
              WifiTxVector txvector)
{
    (void)channelFreqMhz;
    (void)isShortPreamble;

    if (!g_tx_energy_file.is_open())
    {
        return;
    }

    size_t start = context.find("/NodeList/");
    if (start == std::string::npos)
    {
        return;
    }
    start += 10;
    size_t end = context.find("/", start);
    if (end == std::string::npos)
    {
        return;
    }

    uint32_t node_id = static_cast<uint32_t>(std::stoul(context.substr(start, end - start)));

    // Extract device index from context: /NodeList/N/DeviceList/M/...
    size_t dev_start = context.find("/DeviceList/");
    uint32_t dev_idx = 0;
    if (dev_start != std::string::npos)
    {
        dev_start += 12;
        size_t dev_end = context.find("/", dev_start);
        if (dev_end != std::string::npos)
        {
            dev_idx = static_cast<uint32_t>(std::stoul(context.substr(dev_start, dev_end - dev_start)));
        }
    }

    // Convert power level index to actual dBm via the PHY parameters
    uint8_t power_level = txvector.GetTxPowerLevel();
    double power_dbm = 0.0;
    Ptr<Node> node = NodeList::GetNode(node_id);
    if (node && dev_idx < node->GetNDevices())
    {
        Ptr<WifiNetDevice> wifi_dev = DynamicCast<WifiNetDevice>(node->GetDevice(dev_idx));
        if (wifi_dev)
        {
            Ptr<YansWifiPhy> phy = DynamicCast<YansWifiPhy>(wifi_dev->GetPhy());
            if (phy)
            {
                double txStart = phy->GetTxPowerStart();
                double txEnd = phy->GetTxPowerEnd();
                uint32_t nLevels = phy->GetNTxPower();
                if (nLevels > 1)
                {
                    power_dbm = txStart + power_level * (txEnd - txStart) / (nLevels - 1);
                }
                else
                {
                    power_dbm = txStart;
                }
            }
        }
    }
    double power_watts = std::pow(10.0, (power_dbm - 30.0) / 10.0);

    uint32_t packet_size = packet->GetSize();
    double data_rate_bps = static_cast<double>(rate);
    if (data_rate_bps <= 0.0)
    {
        return;
    }
    double tx_duration_s = (packet_size * 8.0) / data_rate_bps;
    double tx_energy_joules = power_watts * tx_duration_s;

    g_node_tx_energy[node_id] += tx_energy_joules;

    g_tx_energy_file << std::fixed << std::setprecision(9)
                     << "node=" << node_id << " ch=" << channelNumber
                     << " power=" << power_dbm << "dBm"
                     << " pwr_w=" << power_watts << " pkt=" << packet_size << "B"
                     << " rate=" << (data_rate_bps / 1e6) << "Mbps"
                     << " duration=" << tx_duration_s << "s"
                     << " energy=" << tx_energy_joules << "J\n";
    g_tx_energy_file.flush();
}

void
TraceMacTxRtsFailed(std::string context, Mac48Address address)
{
    (void)context;
    (void)address;
    ++g_mac_short_retry_count;
}

void
TraceMacTxDataFailed(std::string context, Mac48Address address)
{
    (void)context;
    (void)address;
    ++g_mac_long_retry_count;
}

void
TraceMacTxFinalRtsFailed(std::string context, Mac48Address address)
{
    (void)context;
    (void)address;
    ++g_mac_final_rts_failed_count;
}

void
TraceMacTxFinalDataFailed(std::string context, Mac48Address address)
{
    (void)context;
    (void)address;
    ++g_mac_final_data_failed_count;
}

void
ExportNodePosition(const NodeContainer& node_container)
{
    std::ofstream export_file(PrefixFile("node_position.txt"));
    if (!export_file.is_open())
    {
        return;
    }

    const unsigned int container_size = node_container.GetN();
    export_file << "total nodes: " << container_size << "\n";
    for (unsigned int i = 0; i < container_size; ++i)
    {
        Ptr<Node> node = node_container.Get(i);
        Vector position = node->GetObject<MobilityModel>()->GetPosition();
        export_file << "node " << std::setw(2) << i << ": " << std::setw(8) << position.x << "  "
                    << std::setw(8) << position.y << "\n";
    }
}

void
ExportNodePower(const NodeContainer& node_container)
{
    std::ofstream export_file(PrefixFile("node_power.txt"));
    if (!export_file.is_open())
    {
        return;
    }

    auto get_tx_power = [](Ptr<YansWifiPhy> wifi_phy) {
        return std::round(wifi_phy->GetTxPowerStart() * 100.0) / 100.0;
    };

    const unsigned int container_size = node_container.GetN();
    export_file << "total nodes: " << container_size << "\n";
    for (unsigned int i = 0; i < container_size; ++i)
    {
        Ptr<Node> node = node_container.Get(i);
        const unsigned int total_devices = node->GetNDevices() - 1;
        export_file << "node " << std::setw(2) << i << ": ";

        for (unsigned int device_idx = 0; device_idx < total_devices; ++device_idx)
        {
            Ptr<WifiNetDevice> wifi_device = DynamicCast<WifiNetDevice>(node->GetDevice(device_idx));
            Ptr<YansWifiPhy> wifi_phy = DynamicCast<YansWifiPhy>(wifi_device->GetPhy());
            export_file << std::setw(5) << get_tx_power(wifi_phy) << "  ";
        }
        export_file << "\n";
    }
    export_file << "output power unit: dBm\n";
}

void
ExportEttMatrix(const NodeContainer& node_container)
{
    const unsigned int num_nodes = node_container.GetN();
    if (num_nodes == 0)
    {
        return;
    }

    Ptr<Ipv4> ipv4 = node_container.Get(0)->GetObject<Ipv4>();
    if (!ipv4 || ipv4->GetNInterfaces() <= 1)
    {
        return;
    }

    uint32_t channel_count = ipv4->GetNInterfaces() - 1;
    for (uint32_t ifIndex = 1; ifIndex <= channel_count; ++ifIndex)
    {
        std::ofstream export_file(PrefixFile("ett_ch" + std::to_string(ifIndex) + ".csv"));
        if (!export_file.is_open())
        {
            continue;
        }

        export_file << "Sender/Receiver";
        for (unsigned int j = 0; j < num_nodes; ++j)
        {
            export_file << "," << j;
        }
        export_file << "\n";

        for (unsigned int i = 0; i < num_nodes; ++i)
        {
            export_file << i;
            Ptr<Node> sender_node = node_container.Get(i);

            Ptr<Hello_beacon_App> hello_app = nullptr;
            for (unsigned int app_idx = 0; app_idx < sender_node->GetNApplications(); ++app_idx)
            {
                hello_app = DynamicCast<Hello_beacon_App>(sender_node->GetApplication(app_idx));
                if (hello_app)
                {
                    break;
                }
            }

            for (unsigned int j = 0; j < num_nodes; ++j)
            {
                export_file << ",";
                if (hello_app != nullptr && i != j)
                {
                    double ett = hello_app->get_ett(j, ifIndex);
                    if (std::isinf(ett))
                    {
                        export_file << "INF";
                    }
                    else
                    {
                        export_file << std::fixed << std::setprecision(2) << ett;
                    }
                }
            }
            export_file << "\n";
        }
    }
}

void
ExportEnergyConsumption(const NodeContainer& node_container)
{
    std::ofstream export_file(PrefixFile("energy_consumption.txt"));
    if (!export_file.is_open())
    {
        return;
    }

    double total_energy = 0.0;
    const unsigned int container_size = node_container.GetN();
    export_file << "Node Energy Consumption Report\n";
    export_file << "==============================\n";
    export_file << "total nodes: " << container_size << "\n";
    export_file << "Calculation: Energy = sum(TX Power * TX Duration) for all packets\n";
    export_file << "==============================\n\n";

    for (unsigned int i = 0; i < container_size; ++i)
    {
        double node_energy = g_node_tx_energy[i];
        total_energy += node_energy;
        export_file << "node " << std::setw(2) << i << ": " << std::fixed << std::setprecision(9)
                    << node_energy << " J\n";
    }

    export_file << "\n==============================\n";
    export_file << "Total Network Energy: " << std::fixed << std::setprecision(4) << total_energy
                << " Joules\n";
    export_file << "Average per Node: " << std::fixed << std::setprecision(9)
                << (container_size ? total_energy / container_size : 0.0) << " Joules\n";

    std::cout << "Total Network Energy: " << std::fixed << std::setprecision(4) << total_energy
              << " Joules\n";
}

void
ExportMacRetrySummary()
{
    std::ofstream export_file(PrefixFile("mac_retry_summary.txt"));
    if (!export_file.is_open())
    {
        return;
    }

    export_file << "MAC Retry Summary\n";
    export_file << "=================\n";
    export_file << "short_retry_count: " << g_mac_short_retry_count << "\n";
    export_file << "long_retry_count: " << g_mac_long_retry_count << "\n";
    export_file << "final_rts_failed_count: " << g_mac_final_rts_failed_count << "\n";
    export_file << "final_data_failed_count: " << g_mac_final_data_failed_count << "\n";
    export_file << "total_retry_related_events: "
                << (g_mac_short_retry_count + g_mac_long_retry_count + g_mac_final_rts_failed_count +
                    g_mac_final_data_failed_count)
                << "\n";
}

void
CloseTraceFiles()
{
    if (g_tx_rate_file.is_open())
    {
        g_tx_rate_file.close();
    }
    if (g_tx_energy_file.is_open())
    {
        g_tx_energy_file.close();
    }
}
