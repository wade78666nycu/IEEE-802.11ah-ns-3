#pragma once
#include "hello-beacon.h"

#include "ns3/application.h"
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
      gradPC_proportional_delta(0.2),
      wait_interval(0),
      m_location_obtainable(true),
      m_reduce_default_power(false),
      m_extra_tx_distance(0.0),
      m_gradpc_log_k(5.0),
      m_neighbor_upperbound(0),
      m_hello_power_reduction_db(0.0){}
    virtual ~GradPC_App();

    void print_neighbor_list();
    void set_gradPC_func(const std::vector<short> gradPC_func_vec);

    double get_device_power_dBm(const unsigned int device_idx);
    double get_device_power_W(const unsigned int device_idx);
    void set_reduce_default_power();
    void unset_reduce_default_power();

    void set_extra_tx_distance(const float extra_distance);
    void set_gradpc_log_k(const float k);
    float get_extra_tx_distance();
    void set_hello_power_reduction(double db);

    // TODO: add a mutex to protect m_neighbor_list
    // Need to make sure m_neighbor_list is not being modified when calling is_neighbor function,
    // or else race condition may occur
    bool is_neighbor(const unsigned int node_id, const unsigned int device_idx);

  public:
    std::vector<std::unordered_set<uint32_t>> get_neighbor_list();
    bool m_print_neighbor_list;
    float operating_freq; // frequency in MHz
    float path_loss_exp;
    float gradPC_proportional_delta;
    Time wait_interval; // interval to wait until next cycle starts

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

    // If the location is obtainable, gradPC function returns the power in units dBm.
    // If not, gradPC function returns the upperbound of the neighbors.
    unsigned int gradPC_log(const unsigned int neighbor_size);
    float gradPC_log_power(const int device_idx, const unsigned int neighbor_size);
    unsigned int gradPC_proportional(const unsigned int neighbor_size, const float delta = 0.25);
    float gradPC_proportional_power(const int device_idx, const unsigned int neighbor_size, const float delta = 0.25);
    float gradPC_torque_power(const int device_idx, const unsigned int neighbor_size);

    std::unordered_set<uint32_t> update_neighbor_set(const int neighbor_upperbound, const float tx_distance);

  private:
    bool m_location_obtainable;
    bool m_reduce_default_power; // whether to change the default power on device 0 or not
    float m_extra_tx_distance; // extra distance (meters) when reducing tx power
    float m_gradpc_log_k;
    double m_hello_power_reduction_db; // dBm reduction for Hello beacons vs data (0 = disabled)

    std::vector<short> m_gradPC_func_vec;

    std::vector<std::unordered_set<uint32_t>> m_neighbor_list;
    unsigned int m_neighbor_upperbound;

    // m_distance_vec: vector<std::pair<distance, node_id>>
    // placing the distance as the first item to sort conveniently
    std::vector<std::pair<float, uint32_t>> m_distance_vec;
};

} // namespace ns3
