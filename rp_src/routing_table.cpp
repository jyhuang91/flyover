#include <deque>
#include <limits>

#include "routing_table.hpp"

//#define DEBUG

RoutingTable::RoutingTable() {
  // Empty constructor
}

RoutingTable::RoutingTable(int source, int num_vertices,
                           vector<bool> router_states,
                           vector<vector<int> > adj_matrix) {
  _source = source;
  _num_vertices = num_vertices;
  _router_states = router_states;
  _adj_matrix = adj_matrix;
#ifdef DEBUG
  cout << "adjacent matrix: " << endl;
  for (int i = 0; i < _num_vertices; ++i) {
    cout << i << ": ";
    for (int j = 0; j < _num_vertices; ++j)
      if (_adj_matrix[i][j] == 1) cout << j << "\t";
    cout << endl;
  }
#endif
  _mark.resize(_num_vertices, -1);
  _predecessor.resize(_num_vertices, -1);
  _distance.resize(_num_vertices, numeric_limits<int>::max());
  _distance[_source] = 0;
}

vector<vector<int> > RoutingTable::_GetAdjMatrix() { return _adj_matrix; }

void RoutingTable::_Initialize() {
  _mark.clear();
  _predecessor.clear();
  _distance.clear();
  _mark.resize(_num_vertices, false);
  _predecessor.resize(_num_vertices, -1);
  _distance.resize(_num_vertices, numeric_limits<int>::max());
  _distance[_source] = 0;
}

int RoutingTable::_GetClosestUnmarkedNode() {
  int minDistance = numeric_limits<int>::max();
  int closestUnmarkedNode;
  for (int i = 0; i < _num_vertices; i++) {
    if ((!_mark[i]) && (minDistance >= _distance[i])) {
      minDistance = _distance[i];
      closestUnmarkedNode = i;
    }
  }
  return closestUnmarkedNode;
}

void RoutingTable::_CalculateDistance() {
  _Initialize();
  int minDistance = numeric_limits<int>::max();
  int closestUnmarkedNode;
  int count = 0;
  while (count < _num_vertices) {
    closestUnmarkedNode = _GetClosestUnmarkedNode();
#ifdef DEBUG
    cout << "closest unmarked node is " << closestUnmarkedNode << ", marked? "
         << _mark[closestUnmarkedNode] << endl;
#endif
    _mark[closestUnmarkedNode] = true;
    for (int i = 0; i < _num_vertices; i++) {
      if ((!_mark[i]) && (_adj_matrix[closestUnmarkedNode][i] > 0)) {
        if (_distance[i] > _distance[closestUnmarkedNode] +
                               _adj_matrix[closestUnmarkedNode][i]) {
          _distance[i] = _distance[closestUnmarkedNode] +
                         _adj_matrix[closestUnmarkedNode][i];
          _predecessor[i] = closestUnmarkedNode;
        }
      }
    }
    count++;
  }
#ifdef DEBUG
  for (int i = 0; i < _num_vertices; ++i) {
    cout << i << "(" << _predecessor[i] << "): " << _distance[i] << endl;
  }
#endif
}

void RoutingTable::_PrintPath(int node) {
  if (node == _source) {
    cout << (node) << "..";
  } else if (_predecessor[node] == -1)
    cout << "No path from" << _source << " to " << (node) << endl;
  else {
    _PrintPath(_predecessor[node]);
    cout << (node) << "..";
  }
}

void RoutingTable::_Output() {
  for (int i = 0; i < _num_vertices; i++) {
    if (i == _source)
      cout << (_source) << ".." << _source;
    else
      _PrintPath(i);
    cout << "->" << _distance[i] << endl;
  }
}

// added by Jiayi ====== START ========== 11/27/14
void RoutingTable::RoutingTbl() {
  _routing_tbl.resize(_num_vertices);
  for (int i = 0; i < _num_vertices; i++) {
    if (_router_states[i] == false) {
      //   		printf("router %d is turned off\n", i);
      _routing_tbl[i] = DIR_INVALID;
      continue;
    }
    if (i == _source) {
      _routing_tbl[i] = DIR_ARRIVED;
    } else {
      int j = i;
      while (_predecessor[j] != _source) j = _predecessor[j];
      // Modified by Jiayi, 11/27/14
      int direction;
      if (j - _source == 1) {
        direction = DIR_EAST;
      } else if (j - _source == -1) {
        direction = DIR_WEST;
      } else if (j - _source == 8) {
        direction = DIR_SOUTH;
      } else {
        direction = DIR_NORTH;
      }
      _routing_tbl[i] = direction;
    }
  }
#ifdef DEBUG
  string direction_vec[6] = {"INVALID", "WEST",  "EAST",
                             "SOUTH",   "NORTH", "ARRIVED"};
  cout << "routing table of source: " << _source << endl;
  cout << "destination\toutport" << endl;

  cout << "Extra print statement added for debugging" << endl;

  for (int i = 0; i < _num_vertices; i++) {
    cout << "  " << i << "\t\t" << direction_vec[_routing_tbl[i] + 1] << endl;
  }

  cout << "exit function" << endl;
#endif
}
// added by Jiayi ====== END ========== 11/27/14

vector<int> RoutingTable::GetRoutingTbl() { return _routing_tbl; }

vector<int> RoutingTable::BuildRoutingTable() {
  _CalculateDistance();
  RoutingTbl();
#ifdef DEBUG
  _Output();
#endif
  return _routing_tbl;
}
// calculate the distance for the new spanningAdjMatrix for up down routing
// table construction.
void RoutingTable::spanning_calculate_distance() {
  _Initialize();
  int minDistance = numeric_limits<int>::max();
  int closestUnmarkedNode;
  int count = 0;
  while (count < _num_vertices) {
    closestUnmarkedNode = _GetClosestUnmarkedNode();
    _mark[closestUnmarkedNode] = true;
    for (int i = 0; i < _num_vertices; i++) {
      if ((!_mark[i]) && (_spanning_adj_matrix[closestUnmarkedNode][i] > 0)) {
        if (_distance[i] > _distance[closestUnmarkedNode] +
                               _spanning_adj_matrix[closestUnmarkedNode][i]) {
          _distance[i] = _distance[closestUnmarkedNode] +
                         _spanning_adj_matrix[closestUnmarkedNode][i];
          _predecessor[i] = closestUnmarkedNode;
        }
      }
    }
    count++;
  }
}

// Function to calculate routing table with new spnningAdjMatrix for up down
// routing.
void RoutingTable::spanning_routing_tbl_calculate() {
  _escape_routing_tbl.resize(_num_vertices);
  for (int i = 0; i < _num_vertices; i++) {
    if (_router_states[i] == 0) {
      //  		printf("router %d is turned off\n", i);
      _escape_routing_tbl[i] = DIR_INVALID;
      continue;
    }
    if (i == _source) {
      _escape_routing_tbl[i] = DIR_ARRIVED;
    } else {
      int j = i;
      while (_predecessor[j] != _source) j = _predecessor[j];
      // Modified by Jiayi, 11/27/14
      int direction;
      if (j - _source == 1) {
        direction = DIR_EAST;
      } else if (j - _source == -1) {
        direction = DIR_WEST;
      } else if (j - _source == 8) {
        direction = DIR_SOUTH;
      } else {
        direction = DIR_NORTH;
      }
      _escape_routing_tbl[i] = direction;
    }
  }
#ifdef DEBUG
  string direction_vec[6] = {"INVALID", "WEST",  "EAST",
                             "SOUTH",   "NORTH", "ARRIVED"};
  cout << "escape routing table of source: " << _source << endl;
  cout << "destination\toutport" << endl;
  for (int i = 0; i < _num_vertices; i++)
    cout << "  " << i << "\t\t" << direction_vec[_escape_routing_tbl[i] + 1]
         << endl;
  cout << "exit function escape routing table calculate \n";
#endif
}

// Function called from topology2Dmesh class in order to construct the secondary
// routing table for escape channel.
vector<int> RoutingTable::BuildEscapeRoutingTable(int fabric_manager) {
  //  printf("DIJKSTRA: inside calculate routing table function\n");

  ConstructAdjMatrixForSpanningTree(fabric_manager);

  // printf("DIJKSTRA-SPANNING TREE: adjMatrix after spanning tree computation.
  // \n ");
  for (int i = 0; i < _num_vertices; i++) {
    for (int j = 0; j < _num_vertices; j++) {
      //           printf("%d \t", spanningAdjMatrix[i][j]);
    }
    //     printf(" \n");
  }
  spanning_calculate_distance();
  spanning_routing_tbl_calculate();

#ifdef DEBUG
  _Output();
#endif

  return _escape_routing_tbl;
}

// To compute the spanningAdjMatrix for spanning tree constructed from cyclic
// graph. takes adjMatrix as input. and gives out spanningAdjMatrix.
void RoutingTable::ConstructAdjMatrixForSpanningTree(int fabric_manager) {
  // Specifying the size of spanning adjacent matrix.
  _spanning_adj_matrix.resize(_num_vertices);
  //    printf("SPNNING TREE: spanning matrix resized \n");

  // Initializing all values in spanning adjacent matrix to -1(actualy
  // infinite).
  for (int r = 0; r < _num_vertices; ++r) {
    _spanning_adj_matrix[r].resize(_num_vertices, -1);
    _spanning_adj_matrix[r][r] = 0;
  }

  //   printf("SPANNING TREE : Initialized spanning Adj matrix\n");
  for (int i = 0; i < _num_vertices; i++) {
    for (int j = 0; j < _num_vertices; j++) {
      //         printf("%d \t", spanningAdjMatrix[i][j]);
    }
    //   printf(" \n");
  }

  // Resize the nodes visited matrix
  _nodes_visited.resize(_num_vertices, 0);

  // Call the DFS algo to construct the spanningAdjMatrix for the cyclic graph.
  // printf("SPANNING TREE : DFS called \n");

  _BFS(fabric_manager);
  //_DFS(fabric_manager);
}

// Depth first search to compute spanning tree in a cyclic graph.
void RoutingTable::_DFS(int current_node) {
  if (_nodes_visited[current_node] == 1) {
    return;
  }

  else {
    _nodes_visited[current_node] = 1;
    for (int j = 0; j < _num_vertices; j++) {
      if (_adj_matrix[current_node][j] == 1 && _nodes_visited[j] == 0) {
        //              printf("DFS: nodes visited = %d \n", current_node);
        _spanning_adj_matrix[current_node][j] = 1;
        _spanning_adj_matrix[j][current_node] = 1;

        _DFS(j);
      }
    }
  }
}

void RoutingTable::_BFS(int current_node) {
  deque<int> bfs_q;

  bfs_q.push_back(current_node);
  _nodes_visited[current_node] = 1;

  while (!bfs_q.empty()) {
    current_node = bfs_q.front();
    bfs_q.pop_front();
    for (int i = 0; i < _num_vertices; ++i) {
      if (_adj_matrix[current_node][i] == 1 && _nodes_visited[i] == 0) {
        _spanning_adj_matrix[current_node][i] = 1;
        _spanning_adj_matrix[i][current_node] = 1;
        _nodes_visited[i] = 1;
        bfs_q.push_back(i);
      }
    }
  }
}
