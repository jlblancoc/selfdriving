/* -------------------------------------------------------------------------
 *   SelfDriving C++ library based on PTGs and mrpt-nav
 * Copyright (C) 2019 Jose Luis Blanco, University of Almeria
 * See LICENSE for license information.
 * ------------------------------------------------------------------------- */

#pragma once

#include <libselfdriving/PlannerTypes.h>
#include <mrpt/system/COutputLogger.h>
#include <mrpt/system/CTimeLogger.h>

namespace selfdrive
{
class TPS_RRTstar : public mrpt::system::COutputLogger
{
   public:
    TPS_RRTstar();
    ~TPS_RRTstar() = default;

    NavPlan plan(const PlannerInput& in);

    struct Parameters
    {
        double grid_resolution = .05;  //!< [meters]
    };

    Parameters params_;

    /** Time profiler (Default: enabled)*/
    mrpt::system::CTimeLogger profiler_{true, "TPS_RRTstar"};

   private:
    static void transformPointcloudWithSquareClipping(
        const mrpt::maps::CPointsMap& in_map, mrpt::maps::CPointsMap& out_map,
        const mrpt::poses::CPose2D& asSeenFrom, const double MAX_DIST_XY);

    void spaceTransformer(
        const mrpt::maps::CSimplePointsMap& in_obstacles, const ptg_t& ptg,
        const double MAX_DIST, std::vector<double>& out_TPObstacles);

    void spaceTransformerOneDirectionOnly(
        const int                           tp_space_k_direction,
        const mrpt::maps::CSimplePointsMap& in_obstacles, const ptg_t& ptg,
        const double MAX_DIST, double& out_TPObstacle_k);
};

}  // namespace selfdrive
