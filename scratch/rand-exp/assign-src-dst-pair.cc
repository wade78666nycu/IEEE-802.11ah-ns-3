#include "assign-src-dst-pair.h"
NS_LOG_COMPONENT_DEFINE("AssignSrcDstPair");

void
assign_src_dst_pair(const int rand_seed,
					const int num_nodes,
					std::vector<unsigned int>& src_node_vec,
					std::vector<unsigned int>& dst_node_vec)
{
	if (rand_seed == 4)
	{
		seed4_src_dst_pair(num_nodes, src_node_vec, dst_node_vec);
	}
	else if (rand_seed == 5)
	{
		seed5_src_dst_pair(num_nodes, src_node_vec, dst_node_vec);
	}
	else if (rand_seed == 7)
	{
		seed7_src_dst_pair(num_nodes, src_node_vec, dst_node_vec);
	}
	else if (rand_seed == 9)
	{
		seed9_src_dst_pair(num_nodes, src_node_vec, dst_node_vec);
	}
	else if (rand_seed == 10)
	{
		seed10_src_dst_pair(num_nodes, src_node_vec, dst_node_vec);
	}
	else
	{
		NS_ASSERT_MSG(false, "No matching rand seed function found.");
	}
}

void
seed4_src_dst_pair(const int num_nodes,
				   std::vector<unsigned int>& src_node_vec,
				   std::vector<unsigned int>& dst_node_vec)
{
	if (num_nodes >= 100)
	{
		// random rectangle with 100 nodes
		src_node_vec = std::vector<unsigned int>{42, 72, 0, 53, 20, 88, 27};
		dst_node_vec = std::vector<unsigned int>{45, 90, 50, 96, 2, 70, 68};
	}
	else if (num_nodes >= 90)
	{
		// random rectangle with 90 nodes
		src_node_vec = std::vector<unsigned int>{18, 30, 48, 45, 80, 65, 15};
		dst_node_vec = std::vector<unsigned int>{79, 25, 58, 26, 87, 41, 83};
	}
	else if (num_nodes >= 80)
	{
		// random rectangle with 80 nodes
		src_node_vec = std::vector<unsigned int>{50, 31, 54, 34, 13, 67, 52};
		dst_node_vec = std::vector<unsigned int>{35, 51, 79, 37, 2, 26, 49};
	}
	else if (num_nodes >= 70)
	{
		// random rectangle with 70 nodes
		src_node_vec = std::vector<unsigned int>{66, 41, 55, 54, 61, 4, 68};
		dst_node_vec = std::vector<unsigned int>{50, 65, 33, 69, 25, 62, 27};
	}
	else if (num_nodes >= 60)
	{
		// random rectangle with 60 nodes
		src_node_vec = std::vector<unsigned int>{50, 25, 5, 54, 56, 9, 27};
		dst_node_vec = std::vector<unsigned int>{39, 6, 41, 31, 23, 59, 34};
	}
	else if (num_nodes >= 50)
	{
		// random rectangle with 50 nodes
		src_node_vec = std::vector<unsigned int>{11, 41, 27, 5, 12, 13, 18};
		dst_node_vec = std::vector<unsigned int>{30, 9, 22, 6, 39, 44, 35};
	}
	else if (num_nodes >= 40)
	{
		// random rectangle with 40 nodes
		src_node_vec = std::vector<unsigned int>{15, 7, 27, 13, 6, 2, 12};
		dst_node_vec = std::vector<unsigned int>{11, 32, 34, 20, 21, 9, 33};
	}
	else if (num_nodes >= 30)
	{
		// random rectangle with 30 nodes
		src_node_vec = std::vector<unsigned int>{2, 29, 27, 16, 14, 28, 0};
		dst_node_vec = std::vector<unsigned int>{5, 12, 22, 28, 17, 24, 18};
	}
	else
	{
		NS_ASSERT_MSG(false, "No valid src_node_vec/dst_node_vec found.");
	}
}

void
seed5_src_dst_pair(const int num_nodes,
				   std::vector<unsigned int>& src_node_vec,
				   std::vector<unsigned int>& dst_node_vec)
{
	if (num_nodes >= 100)
	{
		// random rectangle with 100 nodes
		src_node_vec = std::vector<unsigned int>{33, 12, 40, 44, 69, 54, 85};
		dst_node_vec = std::vector<unsigned int>{2, 84, 6, 76, 93, 96, 23};
	}
	else if (num_nodes >= 90)
	{
		// random rectangle with 90 nodes
		src_node_vec = std::vector<unsigned int>{89, 77, 38, 69, 74, 85, 30};
		dst_node_vec = std::vector<unsigned int>{78, 71, 57, 9, 76, 83, 14};
	}
	else if (num_nodes >= 80)
	{
		// random rectangle with 80 nodes
		src_node_vec = std::vector<unsigned int>{72, 49, 12, 44, 69, 60, 20};
		dst_node_vec = std::vector<unsigned int>{33, 40, 27, 65, 59, 23, 54};
	}
	else if (num_nodes >= 70)
	{
		// random rectangle with 70 nodes
		src_node_vec = std::vector<unsigned int>{56, 15, 57, 23, 41, 51, 5};
		dst_node_vec = std::vector<unsigned int>{40, 27, 33, 30, 44, 65, 39};
	}
	else if (num_nodes >= 60)
	{
		// random rectangle with 60 nodes
		src_node_vec = std::vector<unsigned int>{53, 12, 49, 41, 23, 34, 9};
		dst_node_vec = std::vector<unsigned int>{4, 6, 40, 14, 54, 1, 44};
	}
	else if (num_nodes >= 50)
	{
		// random rectangle with 50 nodes
		src_node_vec = std::vector<unsigned int>{33, 4, 14, 8, 28, 46, 3};
		dst_node_vec = std::vector<unsigned int>{13, 26, 6, 41, 42, 37, 12};
	}
	else if (num_nodes >= 40)
	{
		// random rectangle with 40 nodes
		src_node_vec = std::vector<unsigned int>{15, 25, 23, 5, 38, 11, 6};
		dst_node_vec = std::vector<unsigned int>{33, 7, 8, 39, 10, 13, 27};
	}
	else if (num_nodes >= 30)
	{
		// random rectangle with 30 nodes
		src_node_vec = std::vector<unsigned int>{4, 12, 23, 5, 16, 0, 24};
		dst_node_vec = std::vector<unsigned int>{6, 21, 8, 14, 26, 18, 10};
	}
	else
	{
		NS_ASSERT_MSG(false, "No valid src_node_vec/dst_node_vec found.");
	}
}

void
seed7_src_dst_pair(const int num_nodes,
				   std::vector<unsigned int>& src_node_vec,
				   std::vector<unsigned int>& dst_node_vec)
{
	if (num_nodes >= 100)
	{
		// random rectangle with 100 nodes
		src_node_vec = std::vector<unsigned int>{94, 98, 27, 91, 96, 43, 1};
		dst_node_vec = std::vector<unsigned int>{99, 13, 4, 31, 89, 69, 34};
	}
	else if (num_nodes >= 90)
	{
		// random rectangle with 90 nodes
		src_node_vec = std::vector<unsigned int>{57, 42, 76, 19, 77, 61, 89};
		dst_node_vec = std::vector<unsigned int>{54, 59, 30, 75, 35, 23, 58};
	}
	else if (num_nodes >= 80)
	{
		// random rectangle with 80 nodes
		src_node_vec = std::vector<unsigned int>{27, 4, 76, 70, 51, 63, 12};
		dst_node_vec = std::vector<unsigned int>{13, 46, 75, 69, 43, 78, 31};
	}
	else if (num_nodes >= 70)
	{
		// random rectangle with 70 nodes
		src_node_vec = std::vector<unsigned int>{46, 13, 25, 31, 14, 17, 52};
		dst_node_vec = std::vector<unsigned int>{22, 29, 41, 63, 45, 60, 58};
	}
	else if (num_nodes >= 60)
	{
		// random rectangle with 60 nodes
		src_node_vec = std::vector<unsigned int>{25, 30, 29, 40, 17, 31, 52};
		dst_node_vec = std::vector<unsigned int>{48, 22, 56, 55, 0, 18, 58};
	}
	else if (num_nodes >= 50)
	{
		// random rectangle with 50 nodes
		src_node_vec = std::vector<unsigned int>{22, 7, 39, 14, 40, 30, 25};
		dst_node_vec = std::vector<unsigned int>{18, 11, 21, 12, 47, 41, 48};
	}
	else if (num_nodes >= 40)
	{
		// random rectangle with 40 nodes
		src_node_vec = std::vector<unsigned int>{30, 32, 27, 14, 25, 7, 20};
		dst_node_vec = std::vector<unsigned int>{22, 21, 13, 18, 23, 15, 2};
	}
	else if (num_nodes >= 30)
	{
		// random rectangle with 30 nodes
		src_node_vec = std::vector<unsigned int>{2, 9, 21, 15, 25, 7, 20};
		dst_node_vec = std::vector<unsigned int>{22, 21, 23, 18, 29, 13, 4};
	}
	else
	{
		NS_ASSERT_MSG(false, "No valid src_node_vec/dst_node_vec found.");
	}
}

void
seed9_src_dst_pair(const int num_nodes,
				   std::vector<unsigned int>& src_node_vec,
				   std::vector<unsigned int>& dst_node_vec)
{
	if (num_nodes >= 100)
	{
		// random rectangle with 100 nodes
		src_node_vec = std::vector<unsigned int>{60, 31, 63, 83, 82, 32, 67};
		dst_node_vec = std::vector<unsigned int>{1, 44, 81, 10, 95, 84, 8};
	}
	else if (num_nodes >= 90)
	{
		// random rectangle with 90 nodes
		src_node_vec = std::vector<unsigned int>{60, 31, 42, 83, 78, 37, 40};
		dst_node_vec = std::vector<unsigned int>{88, 44, 81, 10, 1, 84, 8};
	}
	else if (num_nodes >= 80)
	{
		// random rectangle with 80 nodes
		src_node_vec = std::vector<unsigned int>{36, 74, 60, 50, 59, 65, 37};
		dst_node_vec = std::vector<unsigned int>{46, 27, 39, 73, 76, 3, 18};
	}
	else if (num_nodes >= 70)
	{
		// random rectangle with 70 nodes
		src_node_vec = std::vector<unsigned int>{1, 36, 50, 19, 65, 13, 0};
		dst_node_vec = std::vector<unsigned int>{55, 42, 40, 69, 24, 62, 43};
	}
	else if (num_nodes >= 60)
	{
		// random rectangle with 60 nodes
		src_node_vec = std::vector<unsigned int>{50, 25, 5, 54, 56, 9, 27};
		dst_node_vec = std::vector<unsigned int>{39, 6, 41, 31, 23, 59, 34};
	}
	else if (num_nodes >= 50)
	{
		// random rectangle with 50 nodes
		src_node_vec = std::vector<unsigned int>{11, 41, 27, 5, 12, 13, 18};
		dst_node_vec = std::vector<unsigned int>{30, 9, 22, 6, 39, 44, 35};
	}
	else if (num_nodes >= 40)
	{
		// random rectangle with 40 nodes
		src_node_vec = std::vector<unsigned int>{15, 7, 27, 13, 6, 2, 12};
		dst_node_vec = std::vector<unsigned int>{11, 32, 34, 20, 21, 9, 33};
	}
	else if (num_nodes >= 30)
	{
		// random rectangle with 30 nodes
		src_node_vec = std::vector<unsigned int>{2, 29, 27, 16, 14, 28, 0};
		dst_node_vec = std::vector<unsigned int>{5, 12, 22, 28, 17, 24, 18};
	}
	else
	{
		NS_ASSERT_MSG(false, "No valid src_node_vec/dst_node_vec found.");
	}
}

void
seed10_src_dst_pair(const int num_nodes,
					std::vector<unsigned int>& src_node_vec,
					std::vector<unsigned int>& dst_node_vec)
{
	if (num_nodes >= 100)
	{
		// random rectangle with 100 nodes
		src_node_vec = std::vector<unsigned int>{65, 67, 25, 80, 22, 0, 32};
		dst_node_vec = std::vector<unsigned int>{95, 48, 5, 40, 8, 76, 12};
	}
	else if (num_nodes >= 90)
	{
		// random rectangle with 90 nodes
		src_node_vec = std::vector<unsigned int>{74, 67, 25, 80, 22, 0, 32};
		dst_node_vec = std::vector<unsigned int>{31, 48, 5, 40, 8, 76, 12};
	}
	else if (num_nodes >= 80)
	{
		// random rectangle with 80 nodes
		src_node_vec = std::vector<unsigned int>{63, 36, 51, 66, 13, 16, 77};
		dst_node_vec = std::vector<unsigned int>{48, 22, 7, 18, 49, 72, 20};
	}
	else if (num_nodes >= 70)
	{
		// random rectangle with 70 nodes
		src_node_vec = std::vector<unsigned int>{46, 13, 25, 31, 14, 17, 52};
		dst_node_vec = std::vector<unsigned int>{22, 29, 41, 63, 45, 60, 58};
	}
	else if (num_nodes >= 60)
	{
		// random rectangle with 60 nodes
		src_node_vec = std::vector<unsigned int>{24, 54, 41, 12, 44, 39, 55};
		dst_node_vec = std::vector<unsigned int>{2, 53, 5, 42, 4, 48, 45};
	}
	else if (num_nodes >= 50)
	{
		// random rectangle with 50 nodes
		src_node_vec = std::vector<unsigned int>{18, 26, 31, 7, 27, 4, 3};
		dst_node_vec = std::vector<unsigned int>{10, 30, 49, 38, 12, 44, 42};
	}
	else if (num_nodes >= 40)
	{
		// random rectangle with 40 nodes
		src_node_vec = std::vector<unsigned int>{30, 32, 27, 14, 25, 7, 20};
		dst_node_vec = std::vector<unsigned int>{22, 21, 13, 18, 23, 15, 2};
	}
	else if (num_nodes >= 30)
	{
		// random rectangle with 30 nodes
		src_node_vec = std::vector<unsigned int>{2, 29, 27, 16, 14, 28, 0};
		dst_node_vec = std::vector<unsigned int>{5, 12, 22, 28, 17, 24, 18};
	}
	else
	{
		NS_ASSERT_MSG(false, "No valid src_node_vec/dst_node_vec found.");
	}
}
