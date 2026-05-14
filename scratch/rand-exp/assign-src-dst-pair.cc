#include "assign-src-dst-pair.h"
NS_LOG_COMPONENT_DEFINE("AssignSrcDstPair");

void
assign_src_dst_pair(const int rand_seed,
					const int num_nodes,
					std::vector<unsigned int>& src_node_vec,
					std::vector<unsigned int>& dst_node_vec,
					const unsigned int max_flows)
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

	if (max_flows > 0 && src_node_vec.size() > max_flows)
	{
		src_node_vec.resize(max_flows);
		dst_node_vec.resize(max_flows);
	}
}

void
seed4_src_dst_pair(const int num_nodes,
				   std::vector<unsigned int>& src_node_vec,
				   std::vector<unsigned int>& dst_node_vec)
{
	if (num_nodes >= 100)
	{
		// random rectangle with 100 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{73, 86, 82, 23,  1, 42, 57,
		                                         40, 76, 69, 25, 94,  9, 41};
		dst_node_vec = std::vector<unsigned int>{91,  0, 92, 36, 32, 10, 93,
		                                         26, 80, 85,  2, 87, 77, 15};
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
		// random rectangle with 50 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{38, 22, 37, 21, 16, 49, 44,
		                                         48, 40, 41,  7, 13, 32, 29};
		dst_node_vec = std::vector<unsigned int>{15,  0, 24, 39, 42, 26, 12,
		                                          4, 36, 14,  6, 27,  3, 45};
	}
	else if (num_nodes >= 40)
	{
		// random rectangle with 40 nodes
		src_node_vec = std::vector<unsigned int>{15, 7, 27, 13, 6, 2, 12};
		dst_node_vec = std::vector<unsigned int>{11, 32, 34, 20, 21, 9, 33};
	}
	else if (num_nodes >= 30)
	{
		// random rectangle with 30 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{18, 24,  6, 13, 10, 19, 20,
		                                         27, 11, 16, 21, 26,  5, 14};
		dst_node_vec = std::vector<unsigned int>{ 0,  2,  3,  9, 12, 25, 28,
		                                          8, 23, 29,  1,  7, 22, 17};
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
		// random rectangle with 100 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{15, 48, 72, 30, 87,  6, 55,
		                                         41, 98, 23, 63, 80, 11, 36};
		dst_node_vec = std::vector<unsigned int>{28, 91, 19, 74, 43, 57, 82,
		                                         14, 37, 69,  5, 46, 93, 60};
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
		// random rectangle with 50 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{24,  7, 39, 16, 44,  1, 32,
		                                         48, 13, 27, 40,  5, 19, 35};
		dst_node_vec = std::vector<unsigned int>{46, 30, 11, 42,  8, 22, 17,
		                                          3, 36, 49, 14, 28, 43,  0};
	}
	else if (num_nodes >= 40)
	{
		// random rectangle with 40 nodes
		src_node_vec = std::vector<unsigned int>{15, 25, 23, 5, 38, 11, 6};
		dst_node_vec = std::vector<unsigned int>{33, 7, 8, 39, 10, 13, 27};
	}
	else if (num_nodes >= 30)
	{
		// random rectangle with 30 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{ 2, 19, 11, 26,  7, 14, 28,
		                                          4, 23,  9, 17,  0, 13, 21};
		dst_node_vec = std::vector<unsigned int>{15,  6, 24,  3, 20, 29, 10,
		                                         27, 12, 25,  1, 18,  8,  5};
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
		// random rectangle with 100 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{33, 25, 99, 84, 78, 81, 21,
		                                         93, 82,  1, 22,  0, 61, 73};
		dst_node_vec = std::vector<unsigned int>{ 5, 28, 34,  4, 88, 42, 56,
		                                         71, 76, 19, 72, 59, 29, 74};
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
		// random rectangle with 50 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{24, 35, 14, 31, 10, 48, 11,
		                                         16, 28, 29, 42, 38, 30,  8};
		dst_node_vec = std::vector<unsigned int>{17, 47,  0, 21, 36, 45, 43,
		                                         32,  1, 46, 39, 41, 49, 40};
	}
	else if (num_nodes >= 40)
	{
		// random rectangle with 40 nodes
		src_node_vec = std::vector<unsigned int>{30, 32, 27, 14, 25, 7, 20};
		dst_node_vec = std::vector<unsigned int>{22, 21, 13, 18, 23, 15, 2};
	}
	else if (num_nodes >= 30)
	{
		// random rectangle with 30 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{21,  5, 15, 29,  7, 28,  9,
		                                          0, 13,  8, 27, 22, 26, 14};
		dst_node_vec = std::vector<unsigned int>{ 2, 10, 23, 12, 19, 25, 17,
		                                         18, 11,  6,  3, 16, 24,  4};
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
		// random rectangle with 100 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{60, 31, 63, 83, 82, 32, 67,
		                                         15, 25, 47, 55, 70, 38, 50};
		dst_node_vec = std::vector<unsigned int>{ 1, 44, 81, 10, 95, 84,  8,
		                                         75, 90,  7, 35, 20, 88, 97};
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
		// random rectangle with 50 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{45, 36, 26, 14, 42, 25, 19,
		                                         20, 31, 22, 33, 15, 44, 46};
		dst_node_vec = std::vector<unsigned int>{34, 21, 18,  5, 17, 11, 12,
		                                         38, 27, 37, 16, 39,  9,  1};
	}
	else if (num_nodes >= 40)
	{
		// random rectangle with 40 nodes
		src_node_vec = std::vector<unsigned int>{15, 7, 27, 13, 6, 2, 12};
		dst_node_vec = std::vector<unsigned int>{11, 32, 34, 20, 21, 9, 33};
	}
	else if (num_nodes >= 30)
	{
		// random rectangle with 30 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{15, 25, 26, 24, 20, 17, 22,
		                                          3,  9, 28,  6, 13,  7, 27};
		dst_node_vec = std::vector<unsigned int>{14,  8,  0, 11,  2, 18, 12,
		                                         21,  1, 10, 19, 16, 29,  5};
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
		// random rectangle with 100 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{80, 26, 92, 89,  6, 49, 88,
		                                         96, 25, 40, 54, 79, 57,  0};
		dst_node_vec = std::vector<unsigned int>{85, 76, 63,  1,  4, 37, 70,
		                                         82, 65, 36, 94, 16, 55, 17};
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
		// random rectangle with 50 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{ 6, 45,  5, 26,  3, 23, 13,
		                                         25, 46, 37, 10, 24, 14, 42};
		dst_node_vec = std::vector<unsigned int>{16,  2, 49, 43, 35, 40, 38,
		                                          0, 34, 12, 21,  9, 48, 41};
	}
	else if (num_nodes >= 40)
	{
		// random rectangle with 40 nodes
		src_node_vec = std::vector<unsigned int>{30, 32, 27, 14, 25, 7, 20};
		dst_node_vec = std::vector<unsigned int>{22, 21, 13, 18, 23, 15, 2};
	}
	else if (num_nodes >= 30)
	{
		// random rectangle with 30 nodes — 14 flows (first 7 = light load, all 14 = heavy load)
		src_node_vec = std::vector<unsigned int>{23, 21,  4, 26, 11, 16, 17,
		                                         15,  0,  5, 27, 28, 25, 20};
		dst_node_vec = std::vector<unsigned int>{24, 12, 14, 18, 10,  7,  3,
		                                          9, 19, 29,  2,  8, 13, 22};
	}
	else
	{
		NS_ASSERT_MSG(false, "No valid src_node_vec/dst_node_vec found.");
	}
}
