/*
 * Copyright (c) 2008, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * \file 
 * 
 * Implementation of the sm::PoseGraph. 
 *
 * \author Paul Furgale
 *
 * Adapted from the Willow Garage implementation for constraint_graph.h
 *
 * \author Bhaskara Marthi
 */

#include <sm/pose_graph/PoseGraph.hpp>
#include <sm/pose_graph/Exceptions.hpp>
#include <boost/foreach.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <sm/assert_macros.hpp>
#include <boost/graph/graphviz.hpp>
#include <fstream>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/visitors.hpp>
#include <boost/property_map/property_map.hpp>
#include <boost/graph/graph_utility.hpp>


// Serialization
#include <fstream>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <limits>

namespace sm { namespace pose_graph {

        using std::pair;

        /************************************************************
         * Construction
         ***********************************************************/

        PoseGraph::PoseGraph () :
            nextVertexId_(1), nextEdgeId_(1)
        {
            clearGraph();
        }

        PoseGraph::~PoseGraph () 
        {}



        /************************************************************
         * Basic modification
         ***********************************************************/

        VertexId PoseGraph::addVertex()
        {
            while (vertexExists(nextVertexId_))
                nextVertexId_++;
            PoseGraph::addVertex(nextVertexId_);
            return nextVertexId_;
        }

        void PoseGraph::addVertex (const VertexId id)
        {
            if (vertexExists(id))
                throw DuplicateVertexIdException(id);
      
            graph_vertex_t v = add_vertex(Vertex(id), graph_);
            vertexMap_[id] = v;
            //vertexUpdated(graph_[v]);
        }



        EdgeId PoseGraph::addEdge (const VertexId to, const VertexId from, const transformation_t& T_to_from)
        {
            while (edgeExists(nextEdgeId_))
                nextEdgeId_++;
            PoseGraph::addEdge(nextEdgeId_, to, from, T_to_from);
            EdgeId eid = nextEdgeId_;
            nextEdgeId_++;
            return eid;
        }


        void PoseGraph::addEdge (const EdgeId id, const VertexId to, const VertexId from,
                                 const transformation_t& T_to_from)
        {
            SM_ASSERT_NE(PoseGraphException,to,from,"Cannot add an edge to and from the same vertex: " << to);
            const graph_vertex_t from_vertex = idVertex(from);
            const graph_vertex_t to_vertex = idVertex(to);
      
            if (edgeExists(id))
                throw DuplicateEdgeIdException(id);

            pair<graph_edge_t, bool> result = add_edge(from_vertex, to_vertex, Edge(id, to, from, T_to_from), graph_);
            SM_ASSERT_TRUE(PoseGraphException,result.second,"Unable to add edge " << id << " from " << from << " to " << to << " to the graph");

            edgeMap_[id] = result.first;
            //edgeUpdated(graph_[result.first]);
        }

        void PoseGraph::updateEdgeTransformation(EdgeId edgeId, const transformation_t & T_to_from)
        {
            EdgeMap::const_iterator i = edgeMap_.find(edgeId);   
            if(i == edgeMap_.end())
                throw UnknownEdgeIdException(edgeId);
      
            Edge & e = graph_[i->second];

            e.T_to_from() = T_to_from;      
        }

        void PoseGraph::clearGraph (void)
        {
            /// Clear out data associated with the graph
            graph_.clear();
            nextVertexId_ = VertexId(1);
            nextEdgeId_ = EdgeId(1);
            vertexMap_.clear();
            edgeMap_.clear();
            //graphCleared();

        }



        /************************************************************
         * Basic const ops
         ***********************************************************/
        EdgeId PoseGraph::nextEdgeId()
        {
            while (edgeExists(nextEdgeId_))
                nextEdgeId_++;
            return nextEdgeId_;
        }

        VertexId PoseGraph::nextVertexId() 
        {
            while (vertexExists(nextVertexId_))
                nextVertexId_++;
            return nextVertexId_;
        }

    
        PoseGraph::VertexSet PoseGraph::allVertices () const
        {
            VertexSet nodes;
            BOOST_FOREACH (const VertexMap::value_type& e, vertexMap_) 
                nodes.insert(e.first);
            return nodes;
        }

        PoseGraph::EdgeSet PoseGraph::allEdges () const
        {
            EdgeSet edges;
            BOOST_FOREACH (const EdgeMap::value_type& e, edgeMap_) {
                edges.insert(e.first);
            }
            return edges;
        }

        PoseGraph::EdgeSet PoseGraph::incidentEdges (const VertexId n) const
        {
            EdgeSet edges;
            graph_vertex_t v = idVertex(n);
            BOOST_FOREACH (const graph_edge_t& e, out_edges(v, graph_))
                edges.insert(EdgeId(graph_[e].id()));
            return edges;
        }

        PoseGraph::IncidentVertices PoseGraph::incidentVertices (const EdgeId e) const
        {
            const Edge & edge = graph_[idEdge(e)];
      
            return IncidentVertices(edge.to(), edge.from());
        }

        const transformation_t& PoseGraph::getTransformation (const EdgeId e) const
        {
            return graph_[idEdge(e)].T_to_from();
        }

    
        void PoseGraph::writeGraphViz(boost::filesystem::path const & dotFile) const
        {
            std::ofstream fout(dotFile.string().c_str());
            SM_ASSERT_TRUE(std::runtime_error,fout.good(), "Unable to open graphviz file " << dotFile << " for writing");


            //boost::write_graphviz(fout,graph_,idWriter(graph_),idWriter(graph_));
      
            fout << "digraph G {\n";
            // iterate through printing each edge.
            boost::graph_traits<graph_t>::edge_iterator e, e_end;
            boost::tie(e,e_end) = boost::edges(graph_);
            for( ; e != e_end; e++)
            {
                fout << "\t" << graph_[*e].from() << " -> " << graph_[*e].to() << " [label=" << graph_[*e].id() << "] \n";
            }
            fout << "}";
      
        }    

        /************************************************************
         * Ops on underlying graph
         ***********************************************************/

        const PoseGraph::graph_t & PoseGraph::graph () const
        {
            return graph_;
        }

        PoseGraph::graph_t& PoseGraph::graph ()
        {
            return graph_;
        }


        PoseGraph::graph_edge_t PoseGraph::idEdge (const EdgeId e) const
        {
            EdgeMap::const_iterator pos = edgeMap_.find(e);
            if (pos == edgeMap_.end())
                throw UnknownEdgeIdException(e);
            return pos->second;
        }

        PoseGraph::graph_vertex_t PoseGraph::idVertex (const VertexId e) const
        {
            VertexMap::const_iterator pos = vertexMap_.find(e);
            if (pos == vertexMap_.end())
                throw UnknownVertexIdException(e);
            return pos->second;
        }

        /************************************************************
         * Utilities
         ***********************************************************/
        // Check if a container contains a id
        template <class Id, class Container>
        bool contains (const Container& container, const Id& id)
        {
            return container.find(id)!=container.end();
        }


        bool PoseGraph::vertexExists (const VertexId n) const
        {
            return contains(vertexMap_, n);
        }

        bool PoseGraph::edgeExists (const EdgeId e) const
        {
            return contains(edgeMap_, e);
        }



        void PoseGraph::save(const boost::filesystem::path & graphFileName) const
        {
            std::ofstream ofs(graphFileName.string().c_str(),std::ios::binary);
            SM_ASSERT_TRUE(PoseGraphException,ofs.good(),"Unable to open file " << graphFileName << " for writing");
            boost::archive::binary_oarchive oa(ofs);
      
            oa << *this;

        }

        void PoseGraph::load(const boost::filesystem::path & graphFileName)
        {
            std::ifstream ifs(graphFileName.string().c_str(),std::ios::binary);
            SM_ASSERT_TRUE(PoseGraphException,ifs.good(),"Unable to open file " << graphFileName << " for reading");
            boost::archive::binary_iarchive ia(ifs);
            clearGraph();
            ia >> *this;

        }


        std::pair<const Edge *, bool> PoseGraph::getEdgeInternal(VertexId to, VertexId from) const 
        {
            graph_vertex_t v = idVertex(from);
            std::pair<graph_edge_t, bool> G = getEdgeInternal(to,v);
      
            return std::make_pair(&graph_[G.first],G.second);
        }

        std::pair<PoseGraph::graph_edge_t, bool> PoseGraph::getEdgeInternal(VertexId to, graph_vertex_t from) const
        {
            graph_traits::out_edge_iterator out_i, out_end;
            graph_traits::edge_descriptor e;
            bool found = false;
            bool invert = false;

            BOOST_FOREACH(e, out_edges(from,graph_))
            {
                const Edge & edge = graph_[e];
                if(edge.to() == to)
                {
                    found = true;
                    invert = false;
                    break;
                }
                else if(edge.from() == to)
                {
                    found = true;
                    invert = true;
                    break;
                }
            }

            SM_ASSERT_TRUE(std::runtime_error, found, "No edge matching " << from << " --> " << to << " found");

            return std::make_pair(e,invert);


        }


        bool PoseGraph::edgeExists(VertexId to, VertexId from) const
        {
            graph_vertex_t v = idVertex(from);
            graph_traits::out_edge_iterator out_i, out_end;
            graph_traits::edge_descriptor e;
            bool found = false;
            bool invert = false;

            BOOST_FOREACH(e, out_edges(v,graph_))
            {
                const Edge & edge = graph_[e];
                if(edge.to() == to)
                {
                    found = true;
                    invert = false;
                    break;
                }
                else if(edge.from() == to)
                {
                    found = true;
                    invert = true;
                    break;
                }
            }

            return found;
        }


        Eigen::Matrix4d PoseGraph::getEdgeTransformationMatrix(VertexId to, VertexId from) const 
        {
            graph_vertex_t v = idVertex(from);
            return getEdgeTransformationMatrix(to,v);

        }

        Eigen::Matrix4d PoseGraph::getEdgeTransformationMatrix(VertexId to, graph_vertex_t from) const
        {
            std::pair<graph_edge_t, bool> G = getEdgeInternal(to,from);
      
            if(G.second)
            {
                return graph_[G.first].T_to_from().T().inverse().matrix();
            }
            else
            {
                return graph_[G.first].T_to_from().T().matrix();
            }
        }

        transformation_t PoseGraph::getEdgeTransformation(VertexId to, VertexId from) const
        {
            graph_vertex_t v = idVertex(from);
            return getEdgeTransformation(to,v);
      
        }

        transformation_t PoseGraph::getEdgeTransformation(VertexId to, graph_vertex_t from) const
        {
            std::pair<graph_edge_t, bool> G = getEdgeInternal(to,from);
            if(G.second)
            {
                return graph_[G.first].T_to_from().inverse();
            }
            else
            {
                return graph_[G.first].T_to_from();
            }
  
        }

        transformation_t PoseGraph::getEdgeTransformation(graph_vertex_t to, VertexId from) const
        {
            std::pair<graph_edge_t, bool> G = getEdgeInternal(from, to);
            if(!G.second)
            {
                return graph_[G.first].T_to_from().inverse();
            }
            else
            {
                return graph_[G.first].T_to_from();
            }
  
        }


        Eigen::Matrix4d PoseGraph::getTransformationMatrix(VertexId to, VertexId from) const
        {
            return breadthFirstSearch(to,from).T();
        }


      
        // returns T_to_from;
        transformation_t PoseGraph::getTransformation(VertexId to, VertexId from) const
        {
            return breadthFirstSearch(to,from);
        }


namespace detail {
    
    struct FoundTargetException{};

            struct FoundTargetEvent
            {
                typedef boost::on_discover_vertex event_filter;
      
                FoundTargetEvent(VertexId id) : id_(id) {}
                template<typename VERTEX, typename GRAPH>
                void operator()(VERTEX v, const GRAPH & g)
                    {
                        if(g[v].id() == id_)
                        {
                            throw FoundTargetException();
                        }
                    }
                VertexId id_;
            };
    
} // namespace detail

        transformation_t PoseGraph::breadthFirstSearch(VertexId toId, VertexId fromId, std::vector<VertexId> * outPath) const
        {

            // Adapted with difficulty from http://www.boost.org/doc/libs/1_45_0/libs/graph/example/bfs.cpp
            std::vector<graph_vertex_t> predecessor(boost::num_vertices(graph_));
      
            graph_vertex_t from = idVertex(fromId);
            graph_vertex_t to   = idVertex(toId);
      

            bool foundPath = false;
            try {
                boost::breadth_first_search
                    (
                        graph_, from, //boost::visitor(boost::bfs_visitor<boost::null_visitor>())); 
                        boost::visitor
                        (
                            boost::make_bfs_visitor
                            (
                                // record_predecessors fills in the predecessors map. FoundTargetEvent throws a FoundTargetException
                                // when the "to" edge is discovered.
                                std::make_pair(boost::record_predecessors(&predecessor[0],boost::on_tree_edge()),detail::FoundTargetEvent(toId))
                                )
                            )
                        );
            } 
            catch(detail::FoundTargetException /*e*/)
            {
                foundPath = true;
            }
      
            std::vector<VertexId> path;
            transformation_t T_to_from;

            if(foundPath)
            {
                std::vector<VertexId> * thePath;
                if(outPath)
                {
                    thePath = outPath;
                    thePath->clear();
                }
                else
                {
                    thePath = &path;
                }
                graph_vertex_t vidx = to;
                const Vertex * V = &graph_[vidx];
                thePath->push_back(V->id());
                while(vidx != from)
                {
                    graph_vertex_t pidx = predecessor[vidx];
                    const Vertex * P = &graph_[pidx];
                    T_to_from =  T_to_from * getEdgeTransformation(vidx, P->id());

                    vidx = pidx;
                    V = P;
                    thePath->push_back(V->id());
                }
	  
                // Build a localization chain from the result.
                //std::reverse(thePath->begin(), thePath->end());

            }
            else
            {
                throw NoPathFound(fromId,toId);
            }

      
            return T_to_from;
        }

        const Edge & PoseGraph::getEdge(EdgeId edgeId) const
        {
            EdgeMap::const_iterator i = edgeMap_.find(edgeId);   
            if(i == edgeMap_.end())
                throw UnknownEdgeIdException(edgeId);
      
            return graph_[i->second];
        }

        const Vertex & PoseGraph::getVertex(VertexId vertexId) const
        {
            VertexMap::const_iterator i = vertexMap_.find(vertexId);   
            if(i == vertexMap_.end())
                throw UnknownVertexIdException(vertexId);
      
            return graph_[i->second];
        }

        const Edge & PoseGraph::getEdge(VertexId v1, VertexId v2) const
        {
            std::pair<const Edge *, bool> p = getEdgeInternal(v1, v2);
            return *p.first;
        }


    
        template<typename T>
        bool PoseGraph::pathExistsInternal(const std::vector<T> & path) const
        {
            bool success = true;
            for(size_t i = 1; i < path.size() && success; i++)
            {
                success = success && edgeExists(VertexId(path[i-1]),VertexId(path[i]));	  
            }

            return success;
        }


        bool PoseGraph::pathExists(const std::vector<boost::uint64_t> & path) const
        {
            return pathExistsInternal<boost::uint64_t>(path);
        }
    
        bool PoseGraph::pathExists(const std::vector<VertexId> & path) const
        {
            return pathExistsInternal<VertexId>(path);
        }


    }} // namespace sm::pose_graph





