// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Radu Serban, Asher Elmquist, Evan Hoerl, Rainer Gericke
// =============================================================================
//
// Main driver function for the City Bus full model.
//
// The vehicle reference frame has Z up, X towards the front of the vehicle, and
// Y pointing to the left.
//
// =============================================================================

#include "chrono/core/ChStream.h"
#include "chrono/utils/ChUtilsInputOutput.h"

#include "chrono_vehicle/ChConfigVehicle.h"
#include "chrono_vehicle/ChVehicleModelData.h"
#include "chrono_vehicle/driver/ChDataDriver.h"
#include "chrono_vehicle/driver/ChVSGGuiDriver.h"
#include "chrono_vehicle/terrain/RigidTerrain.h"
#include "chrono_vehicle/wheeled_vehicle/utils/ChWheeledVehicleVisualSystemVSG.h"

#include "chrono_models/vehicle/citybus/CityBus.h"

#include "chrono_thirdparty/filesystem/path.h"

using namespace chrono;
using namespace chrono::vsg3d;
using namespace chrono::vehicle;
using namespace chrono::vehicle::citybus;

// =============================================================================

// Initial vehicle location and orientation
ChVector<> initLoc(0, 0, 0.5);
ChQuaternion<> initRot(1, 0, 0, 0);

enum DriverMode { DEFAULT, RECORD, PLAYBACK };
DriverMode driver_mode = DEFAULT;

// Visualization type for vehicle parts (PRIMITIVES, MESH, or NONE)
VisualizationType chassis_vis_type = VisualizationType::MESH;
VisualizationType suspension_vis_type = VisualizationType::PRIMITIVES;
VisualizationType steering_vis_type = VisualizationType::PRIMITIVES;
VisualizationType wheel_vis_type = VisualizationType::MESH;
VisualizationType tire_vis_type = VisualizationType::MESH;

// Collision type for chassis (PRIMITIVES, MESH, or NONE)
CollisionType chassis_collision_type = CollisionType::NONE;

// Type of tire model (RIGID, TMEASY, PAC02)
TireModelType tire_model = TireModelType::PAC02;

// Rigid terrain
RigidTerrain::PatchType terrain_model = RigidTerrain::PatchType::BOX;
double terrainHeight = 0;      // terrain height (FLAT terrain only)
double terrainLength = 200.0;  // size in X direction
double terrainWidth = 200.0;   // size in Y direction

// Point on chassis tracked by the camera
ChVector<> trackPoint(0.0, 0.0, 1.75);

// Contact method
ChContactMethod contact_method = ChContactMethod::SMC;
bool contact_vis = false;

// Simulation step sizes
double step_size = 1e-3;
double tire_step_size = step_size;

// Simulation end time
double t_end = 1000;

// Time interval between two render frames
double render_step_size = 1.0 / 50;  // FPS = 50

// Output directories
const std::string out_dir = GetChronoOutputPath() + "CityBus";
const std::string pov_dir = out_dir + "/POVRAY";

// Debug logging
bool debug_output = false;
double debug_step_size = 1.0 / 1;  // FPS = 1

// POV-Ray output
bool povray_output = false;

// =============================================================================

int main(int argc, char* argv[]) {
    GetLog() << "Copyright (c) 2017 projectchrono.org\nChrono version: " << CHRONO_VERSION << "\n\n";

    // --------------
    // Create systems
    // --------------

    // Create the City Bus vehicle, set parameters, and initialize
    CityBus my_bus;
    my_bus.SetContactMethod(contact_method);
    my_bus.SetChassisCollisionType(chassis_collision_type);
    my_bus.SetChassisFixed(false);
    my_bus.SetInitPosition(ChCoordsys<>(initLoc, initRot));
    my_bus.SetTireType(tire_model);
    my_bus.SetTireStepSize(tire_step_size);
    my_bus.Initialize();

    my_bus.SetChassisVisualizationType(chassis_vis_type);
    my_bus.SetSuspensionVisualizationType(suspension_vis_type);
    my_bus.SetSteeringVisualizationType(steering_vis_type);
    my_bus.SetWheelVisualizationType(wheel_vis_type);
    my_bus.SetTireVisualizationType(tire_vis_type);

    // Create the terrain
    RigidTerrain terrain(my_bus.GetSystem());

    MaterialInfo minfo;
    minfo.mu = 0.9f;
    minfo.cr = 0.01f;
    minfo.Y = 2e7f;
    auto patch_mat = minfo.CreateMaterial(contact_method);

    std::shared_ptr<RigidTerrain::Patch> patch;
    switch (terrain_model) {
        case RigidTerrain::PatchType::BOX:
            patch = terrain.AddPatch(patch_mat, CSYSNORM, terrainLength, terrainWidth);
            patch->SetTexture(vehicle::GetDataFile("terrain/textures/tile4.jpg"), 200, 200);
            break;
        case RigidTerrain::PatchType::HEIGHT_MAP:
            patch = terrain.AddPatch(patch_mat, CSYSNORM, vehicle::GetDataFile("terrain/height_maps/test64.bmp"), 128,
                                     128, 0, 4);
            patch->SetTexture(vehicle::GetDataFile("terrain/textures/grass.jpg"), 16, 16);
            break;
        case RigidTerrain::PatchType::MESH:
            patch = terrain.AddPatch(patch_mat, CSYSNORM, vehicle::GetDataFile("terrain/meshes/test.obj"));
            patch->SetTexture(vehicle::GetDataFile("terrain/textures/grass.jpg"), 100, 100);
            break;
    }
    patch->SetColor(ChColor(0.8f, 0.8f, 0.5f));

    terrain.Initialize();

    // Create the interactive driver system
    ChVSGGuiDriver driver((ChVehicle*)&my_bus.GetVehicle());

    std::string driver_file = out_dir + "/driver_inputs.txt";
    utils::CSV_writer driver_csv(" ");

    // Set the time response for steering and throttle keyboard inputs.
    double steering_time = 1.0;  // time to go from 0 to +1 (or from 0 to -1)
    double throttle_time = 1.0;  // time to go from 0 to +1
    double braking_time = 0.3;   // time to go from 0 to +1
    driver.SetSteeringDelta(render_step_size / steering_time);
    driver.SetThrottleDelta(render_step_size / throttle_time);
    driver.SetBrakingDelta(render_step_size / braking_time);

    // If in playback mode, attach the data file to the driver system and
    // force it to playback the driver inputs.
    if (driver_mode == PLAYBACK) {
        driver.SetInputDataFile(driver_file);
        driver.SetInputMode(ChVSGGuiDriver::InputMode::DATAFILE);
    }

    driver.Initialize();

    // Create the vehicle Irrlicht interface
    auto vis = chrono_types::make_shared<ChWheeledVehicleVisualSystemVSG>();
    vis->SetWindowTitle("VSG: City Bus Demo");
    vis->SetChaseCamera(trackPoint, 14.0, 0.5);
    vis->AttachGuiDriver(&driver);
    vis->AttachVehicle(&my_bus.GetVehicle());
    vis->Initialize();

    // -----------------
    // Initialize output
    // -----------------

    if (!filesystem::create_directory(filesystem::path(out_dir))) {
        std::cout << "Error creating directory " << out_dir << std::endl;
        return 1;
    }
    if (povray_output) {
        if (!filesystem::create_directory(filesystem::path(pov_dir))) {
            std::cout << "Error creating directory " << pov_dir << std::endl;
            return 1;
        }
        terrain.ExportMeshPovray(out_dir);
    }

    // ------------------------
    // Create the driver system
    // ------------------------

    // ---------------
    // Simulation loop
    // ---------------

    if (debug_output) {
        GetLog() << "\n\n============ System Configuration ============\n";
        my_bus.LogHardpointLocations();
    }

    my_bus.GetVehicle().LogSubsystemTypes();
    std::cout << "\nVehicle mass: " << my_bus.GetVehicle().GetMass() << std::endl;

    // Number of simulation steps between miscellaneous events
    int render_steps = (int)std::ceil(render_step_size / step_size);
    int debug_steps = (int)std::ceil(debug_step_size / step_size);

    // Initialize simulation frame counters
    int step_number = 0;
    int render_frame = 0;

    if (contact_vis) {
        // vis->SetSymbolScale(1e-4);
        // vis->EnableContactDrawing(ContactsDrawMode::CONTACT_FORCES);
    }

    while (vis->Run()) {
        double time = vis->GetModelTime();

        // End simulation
        if (time >= t_end)
            break;

        // Debug logging
        if (debug_output && step_number % debug_steps == 0) {
            GetLog() << "\n\n============ System Information ============\n";
            GetLog() << "Time = " << time << "\n\n";
            my_bus.DebugLog(OUT_SPRINGS | OUT_SHOCKS | OUT_CONSTRAINTS);
        }

        // Collect output data from modules (for inter-module communication)
        DriverInputs driver_inputs = driver.GetInputs();

        // Driver output
        if (driver_mode == RECORD) {
            driver_csv << time << driver_inputs.m_steering << driver_inputs.m_throttle << driver_inputs.m_braking
                       << std::endl;
        }
        // for(size_t k=0; k<render_steps; k++) {
        //  Update modules (process inputs from other modules)
        driver.Synchronize(time);
        terrain.Synchronize(time);
        my_bus.Synchronize(time, driver_inputs, terrain);
        vis->Synchronize(driver.GetInputModeAsString(), driver_inputs);

        // Advance simulation for one timestep for all modules
        driver.Advance(step_size);
        terrain.Advance(step_size);
        my_bus.Advance(step_size);
        vis->Advance(step_size);

        if (step_number % render_steps == 0) {
            vis->Render();
            //vis->UpdateFromMBS();
            render_frame++;
        }
        step_number++;
    }

    if (driver_mode == RECORD) {
        driver_csv.write_to_file(driver_file);
    }
    return 0;
}
