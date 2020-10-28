/*
 * routetbl.cc
 * - routing table support for Router Parking
 *
 * Author: Jiayi Huang
 */

#include <deque>
#include <limits>

#include "mem/ruby/network/booksim2/globals.hh"
#include "mem/ruby/network/booksim2/misc_utils.hh"
#include "mem/ruby/network/booksim2/routetbl.hh"

//#define DEBUG

RouteTbl::RouteTbl()
{
}

RouteTbl::~RouteTbl()
{
}

RouteTbl::RouteTbl(int src, int num_nodes, vector<bool> router_states)
  : _src(src), _num_nodes(num_nodes), _router_states(router_states)
{
  _Init();
  _rt_tbl.resize(_num_nodes, INVALID);
  _esc_rt_tbl.resize(_num_nodes, INVALID);

#ifdef DEBUG_ROUTE
  cout << "Power-on routers: ";
  for (int i = 0; i < _num_nodes; i++) {
    if (_router_states[i])
      cout << i << " ";
  }
  cout << endl;
#endif
  assert(_num_nodes == powi(gK, gN));
  assert(gN == 2);

  _adj_mtx.resize(_num_nodes);
  for (int i = 0; i < _num_nodes; i++) {
    _adj_mtx[i].resize(_num_nodes, -1);

    int ix = i % gK;
    int iy = i / gK;
    for (int j = 0; j < _num_nodes; j++) {
      if (_router_states[i] == false)
        continue;
      if (_router_states[j] == false)
        continue;

      int jx = j % gK;
      int jy = j / gK;

      if (i == j) {
        _adj_mtx[i][j] = 0;
      } else if ( (abs(ix - jx) == 1 && iy == jy) ||
          (abs(iy - jy) == 1 && ix == jx) ) {
        _adj_mtx[i][j] = 1;
      }
    }
  }
#ifdef DEBUG_ROUTE
  cout << "Adjacent List: " << endl;
  for (int i = 0; i < _num_nodes; i++) {
    cout << i << ": ";
    for (int j = 0; j < _num_nodes; j++) {
      if (_adj_mtx[i][j] > 0)
        cout << j << " ";
    }
    cout << endl;
  }
#endif
}

void RouteTbl::_Init()
{
  _visited.clear();
  _pred.clear();
  _dist.clear();
  _visited.resize(_num_nodes, false);
  _pred.resize(_num_nodes, -1);
  _dist.resize(_num_nodes, numeric_limits<int>::max());
  _dist[_src] = 0;
}

int RouteTbl::_ClosestUnvisited()
{
  int min_dist = numeric_limits<int>::max();
  int closest = -1;
  for (int i = 0; i < _num_nodes; i++) {
    if (!_visited[i] && (min_dist >= _dist[i])) {
      min_dist = _dist[i];
      closest = i;
    }
  }

  return closest;
}

void RouteTbl::_CalDist()
{
  _Init();

  int closest;
  int count = 0;

  while (count < _num_nodes) {

    closest = _ClosestUnvisited();
    //cout << "closest: " << closest << endl;
    if (closest < 0) {
      //cout << "count: " << count << endl;
      //assert(0);
      break;
    }

    _visited[closest] = true;

    for (int i = 0; i < _num_nodes; i++) {
      if (!_visited[i] && (_adj_mtx[closest][i] > 0)) {
        if (_dist[i] > _dist[closest] + _adj_mtx[closest][i]) {
          _dist[i] = _dist[closest] + _adj_mtx[closest][i];
          _pred[i] = closest;
        }
      }
    }

    count++;
  }
}

void RouteTbl::_CalUpDownDist()
{
  _Init();
  
  int closest;
  int count = 0;

  while (count < _num_nodes) {

    closest = _ClosestUnvisited();
    _visited[closest] = true;

    for (int i = 0; i < _num_nodes; i++) {
      if (!_visited[i] && (_updown_adj_mtx[closest][i] > 0)) {
        if (_dist[i] > _dist[closest] + _updown_adj_mtx[closest][i]) {
          _dist[i] = _dist[closest] + _updown_adj_mtx[closest][i];
          _pred[i] = closest;
        }
      }
    }
    count++;
  }
}

void RouteTbl::BuildRoute()
{
  _CalDist();

  for (int i = 0; i < _num_nodes; i++) {
    if (_router_states[i] == false) {
      continue;
    }
    if (i == _src) {
      _rt_tbl[i] = ARRIVED;
    } else {
      
      int j = i;
      while (_pred[j] != _src) {
        j = _pred[j];
      }

      int dir = INVALID;
      int coord = j - _src;
      if (coord == 1) {
        dir = EAST;
      } else if (coord == -1) {
        dir = WEST;
      } else if (coord == gK) {
        dir = SOUTH;
      } else {
        dir = NORTH;
      }
      
      _rt_tbl[i] = dir;
    }
  }

#ifdef DEBUG_ROUTE
  cout << endl;
  cout << "Regular routes:" << endl;
  _PrintAllPath();
#endif
}

void RouteTbl::BuildEscRoute(int root)
{
  // construct up*/down* tree
  _updown_adj_mtx.resize(_num_nodes);
  for (int i = 0; i < _num_nodes; i++) {
    _updown_adj_mtx[i].resize(_num_nodes, -1);
    _updown_adj_mtx[i][i] = 0;
  }

  _visited.clear();
  _visited.resize(_num_nodes, false);
  _BFS(root);
  _CalUpDownDist();

  _esc_rt_tbl.resize(_num_nodes, INVALID);
  for (int i = 0; i < _num_nodes; i++) {
    if (_router_states[i] == false)
      continue;
    
    if (i == _src) {
      _esc_rt_tbl[i] = ARRIVED;
    } else {
      int j = i;
      while (_pred[j] != _src) {
        j = _pred[j];
      }

      int dir = INVALID;
      int coord = j - _src;
      if (coord == 1) {
        dir = EAST;
      } else if (coord == -1) {
        dir = WEST;
      } else if (coord == gK) {
        dir = SOUTH;
      } else {
        dir = NORTH;
      }
      _esc_rt_tbl[i] = dir;
    }
  }

#ifdef DEBUG_ROUTE
  cout << "Escape Up*/Down* tree routes:" << endl;
  _PrintAllPath();
#endif
}

/* Helper functions */

void RouteTbl::_BFS(int current)
{
  deque<int> bfs_q;

  bfs_q.push_back(current);
  _visited[current] = true;

  while (!bfs_q.empty()) {
    current = bfs_q.front();
    bfs_q.pop_front();
    for (int i = 0; i < _num_nodes; i++) {
      if (_adj_mtx[current][i] == 1 && _visited[i] == false) {
        _updown_adj_mtx[current][i] = 1;
        _updown_adj_mtx[i][current] = 1;
        _visited[i] = true;
        bfs_q.push_back(i);
      }
    }
  }
}

void RouteTbl::_PrintPath(int node) {
  
  if (node == _src) {
    cout << node << "->";
  } else if (_pred[node] == -1) {
    //cout << "No path from " << _src
    //  << " to " << node
    //  << " (dist: " << _dist[node]
    //  << ")" << endl;
  } else {
    _PrintPath(_pred[node]);
    cout << node << "->";
  }
}

void RouteTbl::_PrintAllPath()
{
  for (int i = 0; i < _num_nodes; i++) {
    if (i == _src) {
      cout << _src << "->" << _src;
    } else {
      _PrintPath(i);
    }
    if (_dist[i] < _num_nodes)
      cout << " (" << _dist[i] << ")." << endl;
  }
}
