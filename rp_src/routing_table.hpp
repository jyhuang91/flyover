#ifndef _ROUTING_TABLE_HPP
#define _ROUTING_TABLE_HPP

#include <iostream>
#include <vector>

#include "booksim.hpp"

#define INFINITY (__builtin_inff())//9999

enum {
	DIR_INVALID = -1, DIR_EAST, DIR_WEST, DIR_SOUTH, DIR_NORTH, DIR_ARRIVED
};

class RoutingTable {
private:
	// Adj Matrix for Minimal path routing table calculation.
	vector<vector<int> > _adj_matrix;
	vector<int> _predecessor;
	vector<int> _distance;
	vector<bool> _mark; //keep track of visited node
	int _source;
	int _num_vertices;
	vector<bool> _router_states;	// Jiayi

	// Routing table computed using dijkstra's algorithm for minimal pah computation.
	vector<int> _routing_tbl;

	// Routing table computed using spanning tree for escape channel.
	vector<int> _escape_routing_tbl;

	// Adj matrix computed after minimum spanning tree.
	vector<vector<int> > _spanning_adj_matrix;

	// Array to hold vistied nodes while constructing spanning tree using DFS.
	vector<int> _nodes_visited;


public:
	/*
	 * Function read() reads No of vertices, Adjacency Matrix and source
	 * Matrix from the user. The number of vertices must be greather than
	 * zero, all members of Adjacency Matrix must be postive as distances
	 * are always positive. The source vertex must also be positive from 0
	 * to noOfVertices - 1

	 */
	RoutingTable();

	RoutingTable(int source, int num_vertices, vector<bool> router_states,
			vector<vector<int> > ajd_matrix);

	vector<vector<int> > _GetAdjMatrix();

	/*
	 * Function initialize initializes all the data members at the begining of
	 * the execution. The distance between source to source is zero and all other
	 * distances between source and vertices are infinity. The mark is initialized
	 * to false and predecessor is initialized to -1
	 */

	void _Initialize();

	/*
	 * Function getClosestUnmarkedNode returns the node which is nearest from the
	 * Predecessor marked node. If the node is already marked as visited, then it search
	 * for another node.
	 */

	int _GetClosestUnmarkedNode();
	/*
	 * Function calculateDistance calculates the minimum distances from the source node to
	 * Other node.
	 */

	void _CalculateDistance();
	/*
	 * Function output prints the results
	 */

	void _Output();
	void _PrintPath(int);

	void RoutingTbl();	// added by Jiayi, 11/26/14

	vector<int> GetRoutingTbl();

	vector<int> BuildRoutingTable();

	// Function ot calculate esc channel routing table. called from topology
	vector<int> BuildEscapeRoutingTable(int fabric_manager);

	// local function to recalculate adjacency matrix after minimum spanning tree.
	void ConstructAdjMatrixForSpanningTree(int fabric_manager);

	void _DFS(int current_node);
	void _BFS(int current_node);

	void spanning_routing_tbl_calculate();

	void spanning_calculate_distance();

};

#endif
