/*****************************************************************************

 This file is part of QSS Solver.

 QSS Solver is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 QSS Solver is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with QSS Solver.  If not, see <http://www.gnu.org/licenses/>.

 ******************************************************************************/

#include "graph.h"

Graph::Graph (int states, int events) :
     _states (states), _events (events),_graphEdges (
	0), _graph (), _hyperGraph (),_wmap (), _hwmap ()
{
  _profile = GRP_GraphProfile();
}

Graph::~Graph ()
{
}

map<int, set<int> >
Graph::graph ()
{
  return (_graph);
}

map<int, set<int> >
Graph::hyperGraph ()
{
  return (_hyperGraph);
}

void
Graph::addGraphEdge (int orig, int dest)
{
  _graph[orig].insert (dest);
}

void
Graph::addHyperGraphEdge (int orig, int dest)
{
  _hyperGraph[orig].insert (dest);
}

int
Graph::graphEdgeWeight (int node, int edge)
{
  int w = 10;
  if (node < _states)
    {
      if (edge >= _states)
        {
          w = 100;
        }
    }
  else
    {
      w = 100;
    }
  if (_wmap[node].find (node + 1) != _wmap[node].end ())
    {
      w = 1;
    }
  return (w);
}

int
Graph::hyperGraphEdgeWeight (int node, int edge)
{
  int w = 10;
  if (node >= _states)
	{
	  w = 100;
	}
  if (_hwmap[node].find(node+1) != _hwmap[node].end())
    {
      w = 1;
    }
  return (w);
}

void
Graph::connectGraphs ()
{
  int i, nvtxs = _states + _events;
  for (i = 0; i < nvtxs; i++)
    {
      if (_graph[i].find (i + 1) == _graph[i].end () && i < nvtxs - 1)
	{
	  _graph[i].insert (i + 1);
	  _wmap[i].insert (i + 1);
	}
      if (_hyperGraph[i].find (i + 1) == _hyperGraph[i].end () && i < nvtxs - 1)
	{
	  _hyperGraph[i].insert (i + 1);
	  _hwmap[i].insert (i + 1);
	}
      _graphEdges += _graph[i].size ();
    }
}

int
Graph::graphEdges ()
{
  return (_graphEdges);
}

int
Graph::graphNodeEdges (int node)
{
  return (_graph[node].size ());
}

int
Graph::hyperGraphEdges ()
{
  return (_hyperGraph.size());
}

bool
Graph::empty ()
{
  return (_graph.empty ());
}