﻿/* -------------------------------------------------------------------------
 *   SelfDriving C++ library based on PTGs and mrpt-nav
 * Copyright (C) 2019-2021 Jose Luis Blanco, University of Almeria
 * See LICENSE for license information.
 * ------------------------------------------------------------------------- */

#include <mrpt/opengl/CArrow.h>
#include <mrpt/opengl/CGridPlaneXY.h>
#include <mrpt/opengl/COpenGLScene.h>
#include <mrpt/opengl/CPointCloud.h>
#include <mrpt/opengl/CSetOfLines.h>
#include <mrpt/opengl/CSetOfObjects.h>
#include <mrpt/opengl/CText3D.h>
#include <mrpt/opengl/stock_objects.h>
#include <selfdriving/render_tree.h>

using namespace selfdriving;

auto selfdriving::render_tree(
    const MotionPrimitivesTreeSE2& tree, const PlannerInput& pi,
    const RenderOptions& ro) -> std::shared_ptr<mrpt::opengl::CSetOfObjects>
{
    using mrpt::opengl::stock_objects::CornerXYZ;
    using mrpt::opengl::stock_objects::CornerXYZSimple;

    auto  ret   = mrpt::opengl::CSetOfObjects::Create();
    auto& scene = *ret;

    // Build a model of the vehicle shape:
    auto   gl_veh_shape = mrpt::opengl::CSetOfLines::Create();
    double xyzcorners_scale;
    {
        gl_veh_shape->setLineWidth(ro.vehicle_line_width);
        gl_veh_shape->setColor_u8(ro.color_vehicle);

        double max_veh_radius = 0.;
        if (auto pPoly =
                std::get_if<mrpt::math::TPolygon2D>(&pi.ptgs.robotShape);
            pPoly)
        {
            const auto& poly = *pPoly;
            gl_veh_shape->appendLine(
                poly[0].x, poly[0].y, 0, poly[1].x, poly[1].y, 0);
            for (size_t i = 2; i <= poly.size(); i++)
            {
                const size_t idx = i % poly.size();
                mrpt::keep_max(max_veh_radius, poly[idx].norm());
                gl_veh_shape->appendLineStrip(poly[idx].x, poly[idx].y, 0);
            }
        }
        else if (auto pRadius = std::get_if<double>(&pi.ptgs.robotShape);
                 pRadius)
        {
            const double R            = *pRadius;
            const int    NUM_VERTICES = 14;
            for (int i = 0; i <= NUM_VERTICES; i++)
            {
                const size_t idx  = i % NUM_VERTICES;
                const size_t idxn = (i + 1) % NUM_VERTICES;
                const double ang  = idx * 2 * M_PI / (NUM_VERTICES - 1);
                const double angn = idxn * 2 * M_PI / (NUM_VERTICES - 1);
                gl_veh_shape->appendLine(
                    R * cos(ang), R * sin(ang), 0, R * cos(angn), R * sin(angn),
                    0);
            }
            gl_veh_shape->appendLine(0, R, 0, 0, -R, 0);
            gl_veh_shape->appendLine(0, R, 0, R, 0, 0);
            gl_veh_shape->appendLine(0, -R, 0, R, 0, 0);

            mrpt::keep_max(max_veh_radius, R);
        }
        else
        {
            THROW_EXCEPTION("Invalid vehicle shape variant<> type.");
        }

        xyzcorners_scale = max_veh_radius * 0.5;
    }

    // Override with user scale?
    if (ro.xyzcorners_scale) xyzcorners_scale = *ro.xyzcorners_scale;

    // "ground"
    if (ro.ground_xy_grid_frequency.value_or(1.0) > 0)
    {
        double gridSpacing;
        if (ro.ground_xy_grid_frequency)
        {  // user value:
            gridSpacing = ro.ground_xy_grid_frequency.value();
        }
        else
        {
            const auto lx        = pi.worldBboxMax.x - pi.worldBboxMin.x;
            const auto ly        = pi.worldBboxMax.y - pi.worldBboxMin.y;
            const auto lSmallest = std::max(lx, ly);
            if (lSmallest > 0)
                gridSpacing = (lSmallest / 5.0) * 0.999;
            else
                gridSpacing = 1.0;
        }

        auto obj = mrpt::opengl::CGridPlaneXY::Create(
            pi.worldBboxMin.x, pi.worldBboxMax.x, pi.worldBboxMin.y,
            pi.worldBboxMax.y, 0 /*z*/, gridSpacing);
        obj->setColor_u8(ro.color_ground_xy_grid);
        scene.insert(obj);
    }

    // Original randomly-pick pose:
    if (ro.x_rand_pose)
    {
        auto obj = CornerXYZ(xyzcorners_scale * 1.0);
        obj->setName("X_rand");
        obj->enableShowName();
        obj->setPose(mrpt::poses::CPose3D(*ro.x_rand_pose));
        scene.insert(obj);
    }

    // Nearest state pose:
    if (ro.x_nearest_pose)
    {
        auto obj = CornerXYZ(xyzcorners_scale * 1.0);
        obj->setName("X_near");
        obj->enableShowName();
        obj->setPose(mrpt::poses::CPose3D(*ro.x_nearest_pose));
        scene.insert(obj);
    }

    // Determine the up-to-now best solution, so we can highlight the best path
    // so far:
    MotionPrimitivesTreeSE2::path_t best_path;

    if (ro.highlight_path_to_node_id)
    { best_path = tree.backtrack_path(*ro.highlight_path_to_node_id); }

    // make list of nodes in the way of the best path:
    std::set<const MotionPrimitivesTreeSE2::edge_t*> edges_best_path,
        edges_best_path_decim;
    if (!best_path.empty())
    {
        auto it_end   = best_path.end();
        auto it_end_1 = best_path.end();
        std::advance(it_end_1, -1);

        for (auto it = best_path.begin(); it != it_end; ++it)
            if (it->edgeToParent_) edges_best_path.insert(*it->edgeToParent_);

        // Decimate the path (always keeping the first and last entry):
        ASSERT_GT_(ro.draw_shape_decimation, 0);
        for (auto it = best_path.begin(); it != it_end;)
        {
            if (it->edgeToParent_)
                edges_best_path_decim.insert(*it->edgeToParent_);

            if (it == it_end_1) break;
            for (size_t k = 0; k < ro.draw_shape_decimation; k++)
            {
                if (it == it_end || it == it_end_1) break;
                ++it;
            }
        }
    }

    // The starting pose vehicle shape must be inserted independently, because
    // the rest are edges and we draw the END pose of each edge:
    {
        auto vehShape  = mrpt::opengl::CSetOfLines::Create(*gl_veh_shape);
        auto shapePose = mrpt::math::TPose3D(pi.stateStart.pose);
        shapePose.z += ro.vehicle_shape_z;
        vehShape->setPose(shapePose);
        scene.insert(vehShape);
    }

    // Existing nodes & edges between them:
    for (const auto& idnode : tree.nodes())
    {
        const auto& node = idnode.second;

        mrpt::math::TPose2D poseParent;
        if (node.parentID_) poseParent = tree.nodes().at(*node.parentID_).pose;

        const mrpt::math::TPose2D& poseNode = node.pose;

        const bool isLastNode = (idnode.first == tree.nodes().rbegin()->first);
        const bool isBestPath = node.edgeToParent_ &&
                                edges_best_path.count(*node.edgeToParent_) != 0;
        const bool isBestPathAndDrawShape =
            node.edgeToParent_ &&
            edges_best_path_decim.count(*node.edgeToParent_) != 0;

        const bool drawTwistState = isBestPathAndDrawShape && ro.draw_twist;

        // Draw children nodes:
        {
            const float corner_scale =
                xyzcorners_scale * (isLastNode ? 1.5f : 1.0f);

            auto obj = CornerXYZSimple(corner_scale);
            obj->setPose(mrpt::poses::CPose3D(poseNode));
            scene.insert(obj);

            // Insert vehicle shapes along optimal path:
            if (isBestPathAndDrawShape)
            {
                auto vehShape =
                    mrpt::opengl::CSetOfLines::Create(*gl_veh_shape);
                auto shapePose = mrpt::math::TPose3D(poseNode);
                shapePose.z += ro.vehicle_shape_z;
                vehShape->setPose(shapePose);
                scene.insert(vehShape);
            }
            if (drawTwistState)
            {
                // Draw twist:
                if (node.vel.vx != 0 || node.vel.vy != 0)
                {
                    auto glLinVel = mrpt::opengl::CArrow::Create();
                    glLinVel->setArrowEnds(
                        0, 0, 0, node.vel.vx * ro.linVelScale,
                        node.vel.vy * ro.linVelScale, .0);
                    glLinVel->setSmallRadius(ro.twistArrowsRadius);
                    glLinVel->setLargeRadius(ro.twistArrowsRadius * 1.5);
                    glLinVel->setColor_u8(0xff, 0x00, 0x00);
                    glLinVel->setLocation(poseNode.x, poseNode.y, .0);
                    scene.insert(glLinVel);
                }

                if (node.vel.omega != 0)
                {
                    auto glAngVel = mrpt::opengl::CArrow::Create();
                    glAngVel->setArrowEnds(
                        0, 0, 0, 0, 0, node.vel.omega * ro.angVelScale);
                    glAngVel->setSmallRadius(ro.twistArrowsRadius);
                    glAngVel->setLargeRadius(ro.twistArrowsRadius * 1.5);
                    glAngVel->setColor_u8(0x00, 0xff, 0x00);
                    glAngVel->setLocation(poseNode.x, poseNode.y, .0);
                    scene.insert(glAngVel);
                }
            }
        }

        // Draw actual PTG path between parent and children nodes:
        if (auto etp = node.edgeToParent_.value_or(nullptr); etp)
        {
            // Create the path shape, in relative coords to the parent node:
            auto obj = mrpt::opengl::CSetOfLines::Create();
            obj->setPose(mrpt::poses::CPose3D(poseParent));

            // Avoid having to update PTG's dynamic state and calling to
            // ptg->renderPathAsSimpleLine():
            if (etp->interpolatedPath)
            {
                // dummy, just to allow the easy use of "strip" below:
                obj->appendLine(0, 0, 0, 0, 0, 0);
                const auto& ip = etp->interpolatedPath.value();
                for (const auto& relPose : ip)
                { obj->appendLineStrip(relPose.x, relPose.y, 0); }
            }
            else
            {
                // gross approximation with one single segment:
                const auto pIncr = etp->stateTo.pose - etp->stateFrom.pose;

                obj->appendLine(0, 0, 0, pIncr.x, pIncr.y, .0);
            }

            if (isLastNode && ro.highlight_last_added_edge)
            {
                // Last edge format:
                obj->setColor_u8(ro.color_last_edge);
                obj->setLineWidth(ro.width_last_edge);
            }
            else
            {
                // Normal format:
                obj->setColor_u8(ro.color_normal_edge);
                obj->setLineWidth(ro.width_normal_edge);
            }
            if (isBestPath)
            {
                obj->setColor_u8(ro.color_optimal_edge);
                obj->setLineWidth(ro.width_optimal_edge);
            }

            scene.insert(obj);
        }
    }

    // The new node:
    if (ro.new_state)
    {
        auto obj = CornerXYZ(xyzcorners_scale * 1.2);
        obj->setName("X_new");
        obj->enableShowName();
        obj->setPose(mrpt::poses::CPose3D(*ro.new_state));
        scene.insert(obj);
    }

    // Obstacles:
    if (ro.draw_obstacles)
    {
        auto obj = mrpt::opengl::CPointCloud::Create();

        const auto obs = pi.obstacles->obstacles();

        obj->loadFromPointsMap(obs.get());

        obj->setPointSize(ro.point_size_obstacles);
        obj->setColor_u8(ro.color_obstacles);
        scene.insert(obj);
    }

    // The current set of local obstacles:
    // Draw this AFTER the global map so it's visible:
    if (ro.draw_obstacles && ro.local_obs_from_nearest_pose &&
        ro.x_nearest_pose)
    {
        mrpt::opengl::CPointCloud::Ptr obj =
            mrpt::opengl::CPointCloud::Create();

        obj->loadFromPointsMap(ro.local_obs_from_nearest_pose.value());
        obj->setPose(*ro.x_nearest_pose);

        obj->setPointSize(ro.point_size_local_obstacles);
        obj->setColor_u8(ro.color_local_obstacles);
        scene.insert(obj);
    }

    // Start:
    {
        auto obj = CornerXYZ(xyzcorners_scale * 1.5);
        obj->setName("START");
        obj->enableShowName();
        obj->setColor_u8(ro.color_start);
        obj->setPose(pi.stateStart.pose);
        scene.insert(obj);
    }

    // Target:
    {
        auto obj = CornerXYZ(xyzcorners_scale * 1.5);
        obj->setName("GOAL");
        obj->enableShowName();
        obj->setColor_u8(ro.color_goal);
        obj->setPose(pi.stateGoal.pose);
        scene.insert(obj);
    }

    // Log msg:
    if (!ro.log_msg.empty())
    {
        auto gl_txt =
            mrpt::opengl::CText3D::Create(ro.log_msg, "sans", ro.log_msg_scale);
        gl_txt->setLocation(ro.log_msg_position);
        scene.insert(gl_txt);
    }

    return ret;
}