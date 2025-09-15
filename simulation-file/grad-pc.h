#pragma once
#include "hello-beacon.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/vector.h"
#include "ns3/wifi-module.h"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <unordered_set>
#include <vector>

// TODO: Handle the case if no neighbors are found

namespace ns3
{
class Application;

class GradPC_App : public Application
{
  public:
    enum class GradPC_type : short
    {
        logarithmic,
        proportional,
        torque
    };

    static TypeId GetTypeId();
    //GradPC_App();
    GradPC_App()
    : m_print_neighbor_list(false),
      operating_freq(920.0),
      path_loss_exp(2.5),
      cca_threshold(-96.0),
      m_location_obtainable(true),
      m_neighbor_upperbound(0){}
    virtual ~GradPC_App();

    void print_neighbor_list();
    void set_gradPC_func(const std::vector<short> gradPC_func_vec);

    // If the location is obtainable, gradPC function returns the power(Watts).
    // If not, gradPC function returns the upperbound of the neighbors.
    // GradPCI
    unsigned int gradPC_log(const unsigned int neighbor_size);
    float gradPC_log_power(const unsigned int neighbor_size);
    // GradPCII
    unsigned int gradPC_proportional(const unsigned int neighbor_size, const float delta = 0.5);
    float gradPC_proportional_power(const unsigned int neighbor_size, const float delta = 0.5);
    // GradPCIII (location must be obtainable)
    float gradPC_torque_power(const unsigned int neighbor_size, const unsigned int device_idx);

    double get_device_power_dBm(const unsigned int device_idx);
    double get_device_power_W(const unsigned int device_idx);

    // TODO: add a mutex to protect m_neighbor_list
    // Need to make sure m_neighbor_list is not being modified when calling is_neighbor function,
    // or else race condition may occur
    bool is_neighbor(const unsigned int node_id, const unsigned int device_idx);

    bool m_print_neighbor_list;
    float operating_freq; // frequency in MHz
    float path_loss_exp;
    float cca_threshold;

  private:
    void StartApplication() override;
    void StopApplication() override;

    void init_distance_vec();
    void init_neighbor_list();
    unsigned int get_neighbor_upperbound(const unsigned int neighbor_size);

    // adjust tx power according to given gradPC function type
    void adjust_tx_power(const short gradPC_func_type, const unsigned int device_idx);

    float calculate_tx_power(const float max_distance);
    // return true if tx_power is set successfully, false if tx_power reached lowerbound
    bool set_tx_power(float tx_power_dBm, const unsigned int device_idx);

    bool m_location_obtainable;

    std::vector<short> m_gradPC_func_vec;

    std::vector<std::unordered_set<uint32_t>> m_neighbor_list;
    unsigned int m_neighbor_upperbound;

    // m_distance_vec: vector<std::pair<distance, node_id>>
    // placing the distance as the first item to sort conveniently
    std::vector<std::pair<float, uint32_t>> m_distance_vec;
};

} // namespace ns3
