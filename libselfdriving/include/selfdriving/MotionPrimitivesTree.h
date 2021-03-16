/* -------------------------------------------------------------------------
 *   SelfDriving C++ library based on PTGs and mrpt-nav
 * Copyright (C) 2019-2021 Jose Luis Blanco, University of Almeria
 * See LICENSE for license information.
 * ------------------------------------------------------------------------- */

#pragma once

#include <mrpt/containers/traits_map.h>
#include <mrpt/graphs/CDirectedTree.h>
#include <mrpt/math/wrap2pi.h>
#include <mrpt/nav/tpspace/CParameterizedTrajectoryGenerator.h>
#include <mrpt/poses/CPose2D.h>
#include <selfdriving/MoveEdgeSE2_TPS.h>
#include <selfdriving/SE2_KinState.h>
#include <cstdint>
#include <list>
#include <set>

namespace selfdriving
{
/** Generic base for metrics */
template <class node_t>
struct PoseDistanceMetric;

/** This class contains motions and motions tree structures for the hybrid
 * navigation algorithm
 *
 * Usage: this class inheredit mrpt::graphs::CDirectedTree, please refer to
 * inheritance for detail about generic tree methods.
 *   - initialize a motions tree using .initializeMoveTree()
 *   - addEdge (from, to)
 *   - add here more instructions
 *
 * *Changes history*:
 *  - 06/MAR/2014: Creation (MB)
 *  - 21/JAN/2015: Refactoring (JLBC)
 *  - Sep/2020: Adapted to TPS-RRT* (JLBC)
 */
template <
    class NODE_TYPE_DATA, class EDGE_TYPE,
    class MAPS_IMPLEMENTATION = mrpt::containers::map_traits_map_as_vector
    /* Use std::map<> vs. std::vector<>*/
    >
class MotionPrimitivesTree : public mrpt::graphs::CDirectedTree<EDGE_TYPE>
{
   public:
    struct node_t : public NODE_TYPE_DATA
    {
        /** Duplicated ID (it's also in the map::iterator->first), but put here
         * to make it available in path_t */
        mrpt::graphs::TNodeID node_id = INVALID_NODEID;
        /** INVALID_NODEID for the root, a valid ID otherwise */
        mrpt::graphs::TNodeID parent_id = INVALID_NODEID;
        /** NULL for root, a valid edge otherwise */
        EDGE_TYPE* edge_to_parent = nullptr;

        node_t(
            mrpt::graphs::TNodeID node_id_, mrpt::graphs::TNodeID parent_id_,
            EDGE_TYPE* edge_to_parent_, const NODE_TYPE_DATA& data)
            : NODE_TYPE_DATA(data),
              node_id(node_id_),
              parent_id(parent_id_),
              edge_to_parent(edge_to_parent_)
        {
        }
        node_t() = default;
    };

    using base_t = mrpt::graphs::CDirectedTree<EDGE_TYPE>;
    using edge_t = EDGE_TYPE;

    /** Map: TNode_ID => Node info */
    using node_map_t = typename MAPS_IMPLEMENTATION::template map<
        mrpt::graphs::TNodeID, node_t>;

    /** A topological path up-tree */
    using path_t = std::list<node_t>;

    /** Finds the nearest node to a given pose, using the given metric */
    template <class NODE_TYPE_FOR_METRIC>
    mrpt::graphs::TNodeID getNearestNode(
        const NODE_TYPE_FOR_METRIC&                     query_pt,
        const PoseDistanceMetric<NODE_TYPE_FOR_METRIC>& distanceMetricEvaluator,
        double*                                         out_distance = nullptr,
        const std::set<mrpt::graphs::TNodeID>* ignored_nodes = nullptr) const
    {
        ASSERT_(!m_nodes.empty());
        double min_d  = std::numeric_limits<double>::max();
        auto   min_id = INVALID_NODEID;
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it)
        {
            if (ignored_nodes &&
                ignored_nodes->find(it->first) != ignored_nodes->end())
                continue;  // ignore it
            const NODE_TYPE_FOR_METRIC ptTo(query_pt.state);
            const NODE_TYPE_FOR_METRIC ptFrom(it->second.state);
            if (distanceMetricEvaluator.cannotBeNearerThan(ptFrom, ptTo, min_d))
                continue;  // Skip the more expensive calculation of exact
            // distance
            double d = distanceMetricEvaluator.distance(ptFrom, ptTo);
            if (d < min_d)
            {
                min_d  = d;
                min_id = it->first;
            }
        }
        if (out_distance) *out_distance = min_d;
        return min_id;
    }

    void insertNodeAndEdge(
        const mrpt::graphs::TNodeID parent_id,
        const mrpt::graphs::TNodeID new_child_id,
        const NODE_TYPE_DATA&       new_child_node_data,
        const EDGE_TYPE&            new_edge_data)
    {
        // edge:
        typename base_t::TListEdges& edges_of_parent =
            base_t::edges_to_children[parent_id];
        edges_of_parent.push_back(typename base_t::TEdgeInfo(
            new_child_id, false /*direction_child_to_parent*/, new_edge_data));
        // node:
        m_nodes[new_child_id] = node_t(
            new_child_id, parent_id, &edges_of_parent.back().data,
            new_child_node_data);
    }

    /** Insert a node without edges (should be used only for a tree root node)
     */
    void insertNode(
        const mrpt::graphs::TNodeID node_id, const NODE_TYPE_DATA& node_data)
    {
        m_nodes[node_id] = node_t(node_id, INVALID_NODEID, nullptr, node_data);
    }

    mrpt::graphs::TNodeID getNextFreeNodeID() const { return m_nodes.size(); }
    const node_map_t&     getAllNodes() const { return m_nodes; }
    /** Builds the path (sequence of nodes, with info about next edge) up-tree
     * from a `target_node` towards the root
     * Nodes are ordered in the direction ROOT -> start_node
     */
    void backtrackPath(
        const mrpt::graphs::TNodeID target_node, path_t& out_path) const
    {
        out_path.clear();
        auto it_src = m_nodes.find(target_node);
        if (it_src == m_nodes.end())
            throw std::runtime_error(
                "backtrackPath: target_node not found in tree!");
        const node_t* node = &it_src->second;
        for (;;)
        {
            out_path.push_front(*node);

            mrpt::graphs::TNodeID next_node_id = node->parent_id;
            if (next_node_id == INVALID_NODEID)
            {
                break;  // finished
            }
            else
            {
                auto it_next = m_nodes.find(next_node_id);
                if (it_next == m_nodes.end())
                    throw std::runtime_error(
                        "backtrackPath: Node ID not found during tree "
                        "traversal!");
                node = &it_next->second;
            }
        }
    }

   private:
    /** Info per node */
    node_map_t m_nodes;

};  // end TMoveTree

/** Pose metric for SE(2) */
template <>
struct PoseDistanceMetric<SE2_KinState>
{
    bool cannotBeNearerThan(
        const SE2_KinState& a, const SE2_KinState& b, const double d) const
    {
        if (std::abs(a.pose.x - b.pose.x) > d) return true;
        if (std::abs(a.pose.y - b.pose.y) > d) return true;
        return false;
    }

    double distance(const SE2_KinState& a, const SE2_KinState& b) const
    {
        return mrpt::square(a.pose.x - b.pose.x) +
               mrpt::square(a.pose.y - b.pose.y) +
               mrpt::square(mrpt::math::angDistance(a.pose.phi, b.pose.phi));
    }
    PoseDistanceMetric() = default;
};

struct SE2_KinState_TP
{
    /** state in SE2 as 2D pose (x, y, phi) */
    mrpt::math::TPose2D state;
    SE2_KinState_TP(const mrpt::math::TPose2D& state_) : state(state_) {}
    SE2_KinState_TP() = default;
};

/** Pose metric for SE(2) limited to a given PTG manifold. NOTE: This 'metric'
 * is NOT symmetric for all PTGs: d(a,b)!=d(b,a) */
template <>
struct PoseDistanceMetric<SE2_KinState_TP>
{
    bool cannotBeNearerThan(
        const SE2_KinState_TP& a, const SE2_KinState_TP& b,
        const double d) const
    {
        if (std::abs(a.state.x - b.state.x) > d) return true;
        if (std::abs(a.state.y - b.state.y) > d) return true;
        return false;
    }
    double distance(
        const SE2_KinState_TP& src, const SE2_KinState_TP& dst) const
    {
        double               d;
        int                  k;
        mrpt::poses::CPose2D relPose(mrpt::poses::UNINITIALIZED_POSE);
        relPose.inverseComposeFrom(
            mrpt::poses::CPose2D(dst.state), mrpt::poses::CPose2D(src.state));
        bool tp_point_is_exact =
            m_ptg.inverseMap_WS2TP(relPose.x(), relPose.y(), k, d);
        if (tp_point_is_exact)
            return d * m_ptg.getRefDistance();  // de-normalize distance
        else
            return std::numeric_limits<double>::max();  // not in range: we
        // can't evaluate this
        // distance!
    }
    PoseDistanceMetric(const mrpt::nav::CParameterizedTrajectoryGenerator& ptg)
        : m_ptg(ptg)
    {
    }

   private:
    const mrpt::nav::CParameterizedTrajectoryGenerator& m_ptg;
};

/** tree data structure for planning in SE2 within TP-Space manifolds */
using MotionPrimitivesTreeSE2 =
    MotionPrimitivesTree<SE2_KinState_TP, MoveEdgeSE2_TPS>;

/** @} */
}  // namespace selfdriving