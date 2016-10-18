#ifndef _ROUTE_TBL_HPP_
#define _ROUTE_TBL_HPP_

#include <iostream>
#include <vector>

#include "booksim.hpp"

#define INFINITY (__builtin_inff())

enum { DIR_INVALID = -1, DIR_EAST, DIR_WEST, 
  DIR_SOUTH, DIR_NORTH, DIR_ARRIVED
};

class RouteTbl {

private:
  
  // adjacent matrix for shortest-path
  vector<vector<int> > _adj_mtx;
  vector<int> _pred;
  vector<int> _dist;
  vector<int> _visited;

  int _src;
  int _num_nodes;
  vector<bool> _router_states;

  // routing table: BFS shortest path
  vector<int> _rt_tbl;

  // escape routing table: up*/down* tree
  vector<int> _esc_rt_tbl;
  // adjacent matrix after minimum spanning tree
  vector<int> _spanning_adj_mtx;

public:

  RouteTbl();
  RouteTbl(int src, int num_nodes, vector<bool> router_states,
      vector<vector<int> > adj_mtx);

  ~RouteTbl();

  void _Init();
  int _ClosestUnvisited();
  void _CalDist();

  void BuildRoute();
  void BuildEscRoute(int root);

  void _Output();
  void _PrintPath(int dest);
