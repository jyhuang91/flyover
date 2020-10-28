/*
 * routetbl.hpp
 * - routing table support for Router Parking
 *
 * Author: Jiayi Huang
 */

#ifndef _ROUTE_TBL_HPP_
#define _ROUTE_TBL_HPP_

#include <iostream>
#include <vector>

#include "booksim.hpp"

//#define INFINITY (__builtin_inff())

enum { INVALID = -1, EAST, WEST,
  SOUTH, NORTH, ARRIVED
};

class RouteTbl {

private:

  vector<int> _pred;
  vector<int> _dist;
  vector<bool> _visited;

  int _src;
  int _num_nodes;
  vector<bool> _router_states;

  // routing table: BFS shortest path
  vector<int> _rt_tbl;
  // adjacent matrix for shortest-path
  vector<vector<int> > _adj_mtx;

  // escape routing table: up*/down* tree
  vector<int> _esc_rt_tbl;
  // adjacent matrix after minimum spanning tree
  vector<vector<int> > _updown_adj_mtx;

public:

  RouteTbl();
  RouteTbl(int src, int num_nodes, vector<bool> router_states);

  ~RouteTbl();

private:

  void _Init();
  int _ClosestUnvisited();
  void _CalDist();
  void _CalUpDownDist();
  void _BFS(int current);

  void _PrintPath(int dest);
  void _PrintAllPath();

public:

  void BuildRoute();
  void BuildEscRoute(int root);

  inline vector<int> & GetRouteTbl() {return _rt_tbl;}
  inline vector<int> & GetEscRouteTbl() {return _esc_rt_tbl;}

};

#endif
