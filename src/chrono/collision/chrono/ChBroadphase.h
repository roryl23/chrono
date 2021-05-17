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
// Authors: Hammad Mazhar, Radu Serban
// =============================================================================
//
// Description: Class definitions for the Broadpahse
//
// =============================================================================

#pragma once

#include "chrono/collision/ChCollisionModel.h"
#include "chrono/collision/chrono/ChCollisionData.h"

namespace chrono {
namespace collision {

/// @addtogroup collision_mc
/// @{

/// Class for performing broad-phase collision detection.
class ChApi ChBroadphase {
  public:
    ChBroadphase();
    void DispatchRigid();
    void OneLevelBroadphase();
    void DetermineBoundingBox();
    void OffsetAABB();
    void ComputeTopLevelResolution();
    void RigidBoundingBox();
    void FluidBoundingBox();

    std::shared_ptr<ChCollisionData> data_manager;
};

/// @} collision_mc

}  // end namespace collision
}  // end namespace chrono
