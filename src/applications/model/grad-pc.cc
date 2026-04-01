#include "grad-pc.h"

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("GradPC");

static double DbmToW(double dBm) { return std::pow(10.0, 0.1 * (dBm - 30.0)); }

static double WToDbm(double w) { return 10.0 * std::log10(w) + 30.0; }

// passed in tx_power unit is Watts
/*
static int
round_tx_power_mW(float tx_power)
{
    if (tx_power < 0.009)
    {
        return 1; // tx_power is 1 mW
    }

    tx_power *= 1000;
    if (int(tx_power) % 10 >= 5)
        tx_power = int(tx_power / 10 + 1) * 10.0;
    else
        tx_power = int(tx_power / 10) * 10;
    tx_power /= 1000;
    return tx_power;
}
*/

// freq (MHz)
// ref_distance (meters)
// const_num calculation: refer to https://en.wikipedia.org/wiki/Free-space_path_loss
static float
calculate_reference_loss(const float freq, const float ref_distance)
{
    const float const_num = -27.55;
    return 20 * log10(ref_distance) + 20 * log10(freq) + const_num;
}

GradPC_App::~GradPC_App()
{
}

TypeId
GradPC_App::GetTypeId()
{
    static TypeId tid = TypeId("GradPC_App")
                            .SetParent<Application>()
                            .SetGroupName("GradPC")
                            .AddConstructor<GradPC_App>()
                            .AddAttribute("operating_freq",
                                    "value of the operating frequency",
                                    DoubleValue(920.0),
                                    MakeDoubleAccessor(&GradPC_App::operating_freq),
                                    MakeDoubleChecker<double>())
                            .AddAttribute("path_loss_exp",
                                    "value of path loss exponent",
                                    DoubleValue(2.5),
                                    MakeDoubleAccessor(&GradPC_App::path_loss_exp),
                                    MakeDoubleChecker<double>())
                            .AddAttribute("wait_interval",
                                    "interval to wait until next cycle starts. If value is zero, application only runs one time.",
                                    TimeValue(Seconds(0)),
                                    MakeTimeAccessor(&GradPC_App::wait_interval),
                                    MakeTimeChecker())
                            .AddAttribute("extra_tx_distance",
                                    "extra tx distance(meters) when reducing power",
                                    DoubleValue(0.0),
                                    MakeDoubleAccessor(&GradPC_App::m_extra_tx_distance),
                                    MakeDoubleChecker<double>())
                            .AddAttribute("reduce_default_power",
                                    "permission to reduce the default power",
                                    BooleanValue(false),
                                    MakeBooleanAccessor(&GradPC_App::m_reduce_default_power),
                                    MakeBooleanChecker())
                            .AddAttribute("location_obtainable",
                                    "Whether the location is obtainable",
                                    BooleanValue(true),
                                    MakeBooleanAccessor(&GradPC_App::m_location_obtainable),
                                    MakeBooleanChecker());
    return tid;
}

void
GradPC_App::init_neighbor_list()
{
    const int hello_beacon_app_idx = 0;
    // get original neighbor set
    auto original_set = DynamicCast<Hello_beacon_App>(m_node->GetApplication(hello_beacon_app_idx))
                            ->get_neighbor_set();
    const unsigned int total_channels = m_node->GetNDevices() - 1; // exclude loopback
    m_neighbor_list.assign(total_channels, original_set);
}

void
GradPC_App::init_distance_vec()
{
    const int hello_beacon_app_idx = 0;
    m_distance_vec = DynamicCast<Hello_beacon_App>(GetNode()->GetApplication(hello_beacon_app_idx))
                         ->get_distance_vec();
    std::sort(m_distance_vec.begin(), m_distance_vec.end());
}

float
GradPC_App::calculate_tx_power(const float max_distance)
{
    // TODO: change device_idx to be a variable
    static thread_local double cca_threshold {};
    if (cca_threshold == 0) {
        // initialize cca_threshold
        const int device_idx = 0;
        Ptr<WifiNetDevice> wifi_device = DynamicCast<WifiNetDevice>(m_node->GetDevice(device_idx));
        Ptr<YansWifiPhy> wifi_phy = DynamicCast<YansWifiPhy>(wifi_device->GetPhy());
        cca_threshold = wifi_phy->GetCcaMode1Threshold(); // dBm
    }
    //NS_LOG_DEBUG("cca_threshold: " << cca_threshold << " dBm");
    //NS_LOG_DEBUG("max dist: " << max_distance);

    const float ref_distance = 1.0; // meter
    const float reference_loss = calculate_reference_loss(operating_freq, ref_distance);
    const double tx_pow = 10.0 * path_loss_exp * log10(max_distance) + cca_threshold + reference_loss;
    //NS_LOG_DEBUG("tx power: " << tx_pow);
    return 10.0 * path_loss_exp * log10(max_distance) + cca_threshold + reference_loss;
}

std::unordered_set<uint32_t>
GradPC_App::update_neighbor_set(const int neighbor_upperbound, const float tx_distance) {
    std::unordered_set<uint32_t> neighbor_set;
    neighbor_set.reserve(neighbor_upperbound);
    for (const auto& pair_item : m_distance_vec)
    {
        if (pair_item.first <= tx_distance)
        {
            neighbor_set.emplace(pair_item.second);
        }
        else
        {
            break;
        }
    }
    return neighbor_set;
}

// GradPCIII
// gradpc_type 3
unsigned int
GradPC_App::gradPC_log(const unsigned int neighbor_size)
{
    return std::max(1.0, log(neighbor_size));
}

float
GradPC_App::gradPC_log_power(const int device_idx, const unsigned int neighbor_size)
{
    if (neighbor_size == 0) {
        if (static_cast<size_t>(device_idx) < m_neighbor_list.size()) {
            m_neighbor_list[device_idx].clear();
        }
        return 0.0;
    }
    const float k {m_gradpc_log_k};
    int neighbor_upperbound = std::max(1, static_cast<int>(round(k * log(neighbor_size))));
    neighbor_upperbound = std::min(static_cast<int>(neighbor_size), neighbor_upperbound);
    const float tx_distance = m_distance_vec[neighbor_upperbound - 1].first + m_extra_tx_distance;

    // update neighbors
    std::unordered_set<uint32_t> neighbor_set = update_neighbor_set(neighbor_upperbound, tx_distance);
    if (static_cast<size_t>(device_idx) >= m_neighbor_list.size())
    {
        m_neighbor_list.resize(device_idx + 1);
    }
    m_neighbor_list[device_idx] = std::move(neighbor_set);
    return calculate_tx_power(tx_distance);
}

// GradPCII
// gradpc_type 2
unsigned int
GradPC_App::gradPC_proportional(const unsigned int neighbor_size, const float delta)
{
    NS_ASSERT_MSG(delta < 1, "delta should be less than 1.");
    return std::max(static_cast<float>(1.0), (1 - delta) * neighbor_size);
}

float
GradPC_App::gradPC_proportional_power(const int device_idx, const unsigned int neighbor_size, const float delta)
{
    if (neighbor_size == 0) {
        // set power to zero if no neighbors found
        if (static_cast<size_t>(device_idx) < m_neighbor_list.size())
        {
            m_neighbor_list[device_idx].clear();
        }
        return 0.0;
    }
    int neighbor_upperbound = std::max(1, static_cast<int>((1 - delta) * neighbor_size));
    //neighbor_upperbound = std::min(static_cast<int>(neighbor_size), neighbor_upperbound);
    //NS_LOG_DEBUG("neighbor_size: " << neighbor_size << " neighbor_upperbound: " << neighbor_upperbound);
    const float selected_raw_distance = m_distance_vec[neighbor_upperbound - 1].first;
    const uint32_t selected_neighbor_id = m_distance_vec[neighbor_upperbound - 1].second;
    const float tx_distance = selected_raw_distance + m_extra_tx_distance;

    // update neighbors
    std::unordered_set<uint32_t> neighbor_set = update_neighbor_set(neighbor_upperbound, tx_distance);
    if (static_cast<size_t>(device_idx) >= m_neighbor_list.size())
    {
        m_neighbor_list.resize(device_idx + 1);
    }
    m_neighbor_list[device_idx] = std::move(neighbor_set);
    const float tx_power = calculate_tx_power(tx_distance);
    /*NS_LOG_UNCOND("[GradPCII] node=" << GetNode()->GetId()
                                      << " deviceIdx=" << device_idx
                                      << " inputNeighbors=" << neighbor_size
                                      << " upperbound=" << neighbor_upperbound
                                      << " selectedNeighbor=" << selected_neighbor_id
                                      << " rawDistance=" << selected_raw_distance
                                      << " extraDistance=" << m_extra_tx_distance
                                      << " txDistance=" << tx_distance
                                      << " resultNeighbors=" << m_neighbor_list[device_idx].size()
                                      << " txPower(dBm)=" << tx_power);*/
    return tx_power;
}

// GradPCI
float
GradPC_App::gradPC_torque_power(const int device_idx, const unsigned int neighbor_size)
{
    if (neighbor_size == 0) {
        if (static_cast<size_t>(device_idx) < m_neighbor_list.size())
        {
            m_neighbor_list[device_idx].clear();
        }
        return 0.0;
    }
    float total_distance{0.0};
    float d_tilde{0.0};
    std::vector<float> distance_vec;
    distance_vec.reserve(neighbor_size);
    for (unsigned int i = 0; i < neighbor_size; ++i)
    {
        distance_vec.emplace_back(m_distance_vec[i].first);
    }

    for (const auto& distance_item : distance_vec)
    {
        total_distance += distance_item;
        d_tilde += distance_item * distance_item;
    }
    d_tilde /= total_distance;
    d_tilde += m_extra_tx_distance;

    // add neighbor who's distances are smaller than d_tilde
    std::unordered_set<uint32_t> neighbor_set = update_neighbor_set(neighbor_size, d_tilde);
    if (static_cast<size_t>(device_idx) >= m_neighbor_list.size())
    {
        m_neighbor_list.resize(device_idx + 1);
    }
    m_neighbor_list[device_idx] = std::move(neighbor_set);
    return calculate_tx_power(d_tilde);
}

double
GradPC_App::get_device_power_dBm(const unsigned int device_idx)
{
    Ptr<WifiNetDevice> wifi_device = DynamicCast<WifiNetDevice>(m_node->GetDevice(device_idx));
    Ptr<YansWifiPhy> wifi_phy = DynamicCast<YansWifiPhy>(wifi_device->GetPhy());
    return wifi_phy->GetTxPowerStart();
}

double
GradPC_App::get_device_power_W(const unsigned int device_idx)
{
    return DbmToW(get_device_power_dBm(device_idx));
}

void
GradPC_App::set_reduce_default_power() {
    m_reduce_default_power = true;
}

void
GradPC_App::unset_reduce_default_power() {
    m_reduce_default_power = false;
}

void
GradPC_App::set_gradPC_func(const std::vector<short> gradPC_func_vec)
{
    m_gradPC_func_vec = gradPC_func_vec;
}


void
GradPC_App::set_extra_tx_distance(const float extra_distance) {
    NS_ASSERT_MSG(extra_distance >= 0, "extra distance cannot be a negative value");
    m_extra_tx_distance = extra_distance;
}

void
GradPC_App::set_gradpc_log_k(const float k) {
    m_gradpc_log_k = k;
}


float
GradPC_App::get_extra_tx_distance() {
    return m_extra_tx_distance;
}

void
GradPC_App::StartApplication()
{
    NS_ASSERT_MSG(!m_gradPC_func_vec.empty(), "m_gradPC_func_vec is empty.");

    // check if the size of the vector is the same as total devices(except the loopback)
    const unsigned int total_channels = m_node->GetNDevices() - 1;
    NS_ASSERT_MSG(m_gradPC_func_vec.size() >= total_channels - 1,
                  "too few elements in m_gradPC_func_vec");
    //NS_LOG_UNCOND("total_channels: " << total_channels);

    // if (m_node->GetId() == 12) {
    init_distance_vec();
    init_neighbor_list();
    if (m_location_obtainable)
    {
        if (m_reduce_default_power) {
            adjust_tx_power(m_gradPC_func_vec[0], 0);
            // TODO: change neighbor vector
        }
        for (unsigned int device_idx = 1; device_idx < total_channels; ++device_idx)
        {
            adjust_tx_power(m_gradPC_func_vec[device_idx - 1], device_idx);
        }
        return;
    }
    //}
}

void
GradPC_App::StopApplication()
{
    if (m_print_neighbor_list)
    {
        print_neighbor_list();
    }

    if (wait_interval == 0) {
        // GradPC_App only runs once
        return;
    }
    Simulator::Schedule(wait_interval, &GradPC_App::StartApplication, this);
}

// tx_power's unit is dBm
bool
GradPC_App::set_tx_power(float tx_power_dBm, const unsigned int device_idx)
{
    if (m_reduce_default_power == false)
        NS_ASSERT_MSG(device_idx > 0, "m_device_idx 0 should not be modified.");
    NS_ASSERT_MSG(device_idx <= m_node->GetNDevices() - 1,
                  "m_device_idx is out of range."); // minus 1 since we don't need the
                                                    // loopback(127.0.0.1) device

    const float min_tx_power_dBm = 0.01;
    bool tx_power_valid = true;
    if (tx_power_dBm <= min_tx_power_dBm)
    {
        // passed in tx_power_dBm is lower than lowerbound
        // set tx_power_dBm to lowerbound
        tx_power_valid = false;
        tx_power_dBm = min_tx_power_dBm;
    }
    Ptr<WifiNetDevice> wifi_device = DynamicCast<WifiNetDevice>(m_node->GetDevice(device_idx));
    Ptr<YansWifiPhy> wifi_phy = DynamicCast<YansWifiPhy>(wifi_device->GetPhy());
    double maxPower = wifi_phy->GetTxPowerEnd();
    if (tx_power_dBm > maxPower) {
        NS_LOG_UNCOND("requested tx_power_dBm " << tx_power_dBm << " exceeds TxPowerEnd " << maxPower << ", clamping");
        tx_power_dBm = maxPower;
        tx_power_valid = false; // indicate it was adjusted
    }
    // Only reduce TxPowerStart (level 0) for data/hello frames.
    // Keep TxPowerEnd at original max so ACK/CTS (sent at highest power level)
    // can still reach the sender, avoiding asymmetric link failures.
    wifi_phy->SetTxPowerStart(tx_power_dBm);
    return tx_power_valid;
}

void
GradPC_App::adjust_tx_power(const short gradPC_func_type, const unsigned int device_idx)
{
    NS_ASSERT_MSG(device_idx < m_node->GetNDevices() - 1, "device_idx is out of range.");
    if (m_neighbor_list.size() < (m_node->GetNDevices() - 1))
    {
        m_neighbor_list.resize(m_node->GetNDevices() - 1);
    }

    GradPC_type func_type = static_cast<GradPC_type>(gradPC_func_type);
    float tx_power {};
    // All channels compute power from the same original full-power neighbor set
    // to avoid cascading reduction that makes higher-index channels too weak.
    const unsigned int neighbor_size = m_neighbor_list[0].size();
    if (func_type == GradPC_type::logarithmic)
    {
        tx_power = gradPC_log_power(device_idx, neighbor_size);
    }
    else if (func_type == GradPC_type::proportional)
    {
        tx_power = gradPC_proportional_power(device_idx, neighbor_size, gradPC_proportional_delta);
    }
    else if (func_type == GradPC_type::torque)
    {
        NS_ASSERT_MSG(m_location_obtainable, "location must be obtainable to use gradPC_torque");
        tx_power = gradPC_torque_power(device_idx, neighbor_size);
    }
    else
    {
        NS_ABORT_MSG("Passed in gradPC_func_type: " << gradPC_func_type << " not found.");
    }

    // set tx_power
    //NS_LOG_DEBUG("node: " << GetNode()->GetId() << " device_idx: " << device_idx << " Tx power" << tx_power);
    set_tx_power(tx_power, device_idx);
    // if (!set_tx_power(tx_power, device_idx)) {
    // NS_ABORT_MSG("tx_power: " << tx_power << " dBm is lower than lowerbound.");
    //}
}

bool
GradPC_App::is_neighbor(const unsigned node_id, const unsigned int device_idx) {
    NS_ASSERT_MSG(device_idx < m_neighbor_list.size(), "invalid device_idx.");
    return m_neighbor_list[device_idx].count(node_id) != 0;
}

std::vector<std::unordered_set<uint32_t>>
GradPC_App::get_neighbor_list() {
    return m_neighbor_list;
}

void
GradPC_App::print_neighbor_list()
{
    std::cout << std::string(10, '=') << "\t[info] node_id: " << GetNode()->GetId() << "\t"
              << std::string(10, '=') << "\n";
    int set_idx = 0;
    for (const auto& set_item : m_neighbor_list)
    {
        std::cout << "set " << set_idx << ": ";
        std::vector<uint32_t> vec_item(set_item.begin(), set_item.end());
        std::sort(vec_item.begin(), vec_item.end());
        for (const auto& element : vec_item)
        {
            std::cout << std::setw(2) << element << " ";
        }
        std::cout << "\n";
        ++set_idx;
    }
}
