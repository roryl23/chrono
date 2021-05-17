// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2021 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Radu Serban
// =============================================================================
//
// Chrono custom multicore collision system.
// Contains both the broadphase and the narrow phase methods.
//
// =============================================================================

#ifndef CH_COLLISION_SYSTEM_CHRONO_H
#define CH_COLLISION_SYSTEM_CHRONO_H

#include "chrono/core/ChTimer.h"

#include "chrono/collision/ChCollisionSystem.h"
#include "chrono/collision/ChCollisionModelChrono.h"
#include "chrono/collision/chrono/ChCollisionData.h"
#include "chrono/collision/chrono/ChCollisionDefines.h"
#include "chrono/collision/chrono/ChAABBGenerator.h"
#include "chrono/collision/chrono/ChBroadphase.h"
#include "chrono/collision/chrono/ChNarrowphase.h"

namespace chrono {
namespace collision {

/// @addtogroup collision_mc
/// @{

/// Chrono custom multicore collision system.
/// Contains both the broadphase and the narrow phase methods.
class ChApi ChCollisionSystemChrono : public ChCollisionSystem {
  public:
    ChCollisionSystemChrono();
    virtual ~ChCollisionSystemChrono();

    /// Clear all data instanced by this algorithm
    /// if any (like persistent contact manifolds).
    virtual void Clear(void) override {}

    /// Add a collision model to the collision
    /// engine (custom data may be allocated).
    virtual void Add(ChCollisionModel* model) override;

    /// Remove a collision model from the collision engine.
    /// Currently not implemented.
    virtual void Remove(ChCollisionModel* model) override;

    /// Set the number of OpenMP threads for collision detection.
    virtual void SetNumThreads(int nthreads) override;

    /// Run the algorithm and finds all the contacts.
    virtual void Run() override;

    /// Return an AABB bounding all collision shapes in the system
    virtual void GetBoundingBox(ChVector<>& aabb_min, ChVector<>& aabb_max) const override;

    /// Reset any timers associated with collision detection.
    virtual void ResetTimers();

    /// Return the time (in seconds) for broadphase collision detection.
    virtual double GetTimerCollisionBroad() const override;

    /// Return the time (in seconds) for narrowphase collision detection.
    virtual double GetTimerCollisionNarrow() const override;

    /// Fill in the provided contact container with collision information after Run().
    virtual void ReportContacts(ChContactContainer* mcontactcontainer) override;

    /// Fill in the provided proximity container with near point information after Run().
    /// Not used.
    virtual void ReportProximities(ChProximityContainer* mproximitycontainer) override {}

    /// Perform a ray-hit test with all collision models.
    /// Currently not implemented.
    virtual bool RayHit(const ChVector<>& from, const ChVector<>& to, ChRayhitResult& mresult) const override {
        return false;
    }

    /// Perform a ray-hit test with the specified collision model.
    /// Currently not implemented.
    virtual bool RayHit(const ChVector<>& from,
                        const ChVector<>& to,
                        ChCollisionModel* model,
                        ChRayhitResult& mresult) const override {
        return false;
    }

    /// Set and enable "active" box.
    /// Bodies outside this AABB are deactivated.
    void SetAABB(const ChVector<>& aabbmin, const ChVector<>& aabbmax);

    /// Get the dimensions of the "active" box.
    /// The return value indicates whether or not the active box feature is enabled.
    bool GetAABB(real3& aabbmin, real3& aabbmax);

    /// Mark bodies whose AABB is contained within the specified box.
    virtual void GetOverlappingAABB(custom_vector<char>& active_id, real3 Amin, real3 Amax);

    /// Return the pairs of IDs for overlapping contact shapes.
    virtual std::vector<vec2> GetOverlappingPairs();

  private:
    std::shared_ptr<ChCollisionData> data_manager;

    collision::ChBroadphase broadphase;         ///< methods for broad-phase collision detection
    collision::ChNarrowphase narrowphase;       ///< methods for narrow-phase collision detection
    collision::ChAABBGenerator aabb_generator;  ///< methods for cpmputing object AABBs

    custom_vector<char> body_active;

    ChTimer<> m_timer_broad;
    ChTimer<> m_timer_narrow;
};

/// @} collision_mc

}  // end namespace collision
}  // end namespace chrono

#endif
