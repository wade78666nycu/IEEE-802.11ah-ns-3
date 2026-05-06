#pragma once
#include "ns3/core-module.h"
#include "ns3/log.h"

#include <vector>

using namespace ns3;

// max_flows=0 means use all available pairs for the given seed/num_nodes.
void assign_src_dst_pair(const int rand_seed,
						 const int num_nodes,
						 std::vector<unsigned int>& src_node_vec,
						 std::vector<unsigned int>& dst_node_vec,
						 const unsigned int max_flows = 0);
void seed4_src_dst_pair(const int num_nodes,
						std::vector<unsigned int>& src_node_vec,
						std::vector<unsigned int>& dst_node_vec);
void seed5_src_dst_pair(const int num_nodes,
						std::vector<unsigned int>& src_node_vec,
						std::vector<unsigned int>& dst_node_vec);
void seed7_src_dst_pair(const int num_nodes,
						std::vector<unsigned int>& src_node_vec,
						std::vector<unsigned int>& dst_node_vec);
void seed9_src_dst_pair(const int num_nodes,
						std::vector<unsigned int>& src_node_vec,
						std::vector<unsigned int>& dst_node_vec);
void seed10_src_dst_pair(const int num_nodes,
						 std::vector<unsigned int>& src_node_vec,
						 std::vector<unsigned int>& dst_node_vec);
