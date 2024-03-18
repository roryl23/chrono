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
// Authors: Alessandro Tasora, Radu Serban
// =============================================================================
//
// Show how to use the ChModalAssembly to do a basic modal analysis (eigenvalues
// and eigenvector of the ChModalAssembly, which can also contain constraints.
//
// =============================================================================

#include "chrono/physics/ChSystemNSC.h"
#include "chrono/physics/ChLinkMate.h"
#include "chrono/physics/ChLinkLock.h"
#include "chrono/physics/ChBodyEasy.h"
#include "chrono/physics/ChLinkMotorRotationSpeed.h"
#include "chrono/fea/ChElementBeamEuler.h"
#include "chrono/fea/ChBuilderBeam.h"
#include "chrono/fea/ChMesh.h"

#include "chrono_modal/ChModalAssembly.h"
#include "chrono_irrlicht/ChVisualSystemIrrlicht.h"

#include "chrono/solver/ChDirectSolverLS.h"
#ifdef CHRONO_PARDISO_MKL
    #include "chrono_pardisomkl/ChSolverPardisoMKL.h"
#endif

#include "chrono_thirdparty/filesystem/path.h"

using namespace chrono;
using namespace chrono::modal;
using namespace chrono::fea;
using namespace chrono::irrlicht;

// Output directory
const std::string out_dir = GetChronoOutputPath() + "MODAL_ASSEMBLY";

int ID_current_example = 1;
bool modal_analysis = true;

double beam_Young = 100.e6;
double beam_density = 1000;
double beam_wz = 0.3;
double beam_wy = 0.05;
double beam_L = 6;
int n_elements = 8;

double step_size = 0.05;

// static stuff for GUI:
bool SWITCH_EXAMPLE = false;
bool FIX_SUBASSEMBLY = true;
bool DO_MODAL_REDUCTION = false;
bool ADD_INTERNAL_BODY = false;
bool ADD_BOUNDARY_BODY = false;
bool ADD_FORCE = true;
bool ADD_OTHER_ASSEMBLY = false;

void MakeAndRunDemoCantilever(ChSystem& sys,
                              ChVisualSystemIrrlicht& vis,
                              bool do_modal_reduction,
                              bool add_internal_body,
                              bool add_boundary_body,
                              bool add_force,
                              bool add_other_assemblies,
                              bool fix_subassembly) {
    std::cout << "\n\nRUN TEST" << std::endl;

    // Clear previous demo, if any:
    sys.Clear();
    sys.SetChTime(0);

    // CREATE THE ASSEMBLY.
    //
    // The ChModalAssembly is the most important item when doing modal analysis.
    // You must add finite elements, bodies and constraints into this assembly in order
    // to compute the modal frequencies etc.; objects not added into this won't be counted.
    auto assembly = chrono_types::make_shared<ChModalAssembly>();
    sys.Add(assembly);

    // Now populate the assembly to analyze.
    // In this demo, make a cantilever with fixed end

    // Create two FEM meshes: one for nodes that will be removed in modal reduction,
    // the other for the nodes that will remain after modal reduction.

    auto mesh_internal = chrono_types::make_shared<ChMesh>();
    assembly->AddInternal(mesh_internal);  // NOTE: MESH FOR INTERNAL NODES: USE assembly->AddInternal()

    auto mesh_boundary = chrono_types::make_shared<ChMesh>();
    assembly->Add(mesh_boundary);  // NOTE: MESH FOR BOUNDARY NODES: USE assembly->Add()

    mesh_internal->SetAutomaticGravity(false);
    mesh_boundary->SetAutomaticGravity(false);

    // BEAMS:

    // Create a simplified section, i.e. thickness and material properties
    // for beams. This will be shared among some beams.
    auto section = chrono_types::make_shared<ChBeamSectionEulerAdvanced>();

    section->SetDensity(beam_density);
    section->SetYoungModulus(beam_Young);
    section->SetShearModulusFromPoisson(0.31);
    section->SetRayleighDampingBeta(0.01);
    section->SetRayleighDampingAlpha(0.0001);
    section->SetAsRectangularSection(beam_wy, beam_wz);

    ChBuilderBeamEuler builder;

    // The first node is a boundary node: add it to mesh_boundary
    auto my_node_A_boundary = chrono_types::make_shared<ChNodeFEAxyzrot>();
    my_node_A_boundary->SetMass(0);
    my_node_A_boundary->GetInertia().setZero();
    mesh_boundary->AddNode(my_node_A_boundary);

    // The last node is a boundary node: add it to mesh_boundary
    auto my_node_B_boundary = chrono_types::make_shared<ChNodeFEAxyzrot>(ChFrame<>(ChVector3d(beam_L, 0, 0)));
    my_node_B_boundary->SetMass(0);
    my_node_B_boundary->GetInertia().setZero();
    mesh_boundary->AddNode(my_node_B_boundary);

    // The other nodes are internal nodes: let the builder.BuildBeam add them to mesh_internal
    builder.BuildBeam(mesh_internal,       // the mesh where to put the created nodes and elements
                      section,             // the ChBeamSectionEuler to use for the ChElementBeamEuler elements
                      n_elements,          // the number of ChElementBeamEuler to create
                      my_node_A_boundary,  // the 'A' point in space (beginning of beam)
                      my_node_B_boundary,  // ChVector3d(beam_L, 0, 0), // the 'B' point in space (end of beam)
                      ChVector3d(0, 1, 0)  // the 'Y' up direction of the section for the beam
    );

    if (fix_subassembly) {
        // BODY: the base:
        auto my_body_A = chrono_types::make_shared<ChBodyEasyBox>(1, 2, 2, 200);
        my_body_A->SetFixed(true);
        my_body_A->SetPos(ChVector3d(-0.5, 0, 0));
        assembly->Add(my_body_A);

        // my_node_A_boundary->SetFixed(true); // NO - issues with bookeeping in modal_Hblock ***TO FIX***, for the
        // moment: Constraint the boundary node to truss
        auto my_root = chrono_types::make_shared<ChLinkMateGeneric>();
        my_root->Initialize(my_node_A_boundary, my_body_A, ChFrame<>(ChVector3d(0, 0, 1), QUNIT));
        assembly->Add(my_root);
    } else {
        // BODY: the base:
        auto my_body_A = chrono_types::make_shared<ChBodyEasyBox>(1, 2, 2, 200);
        my_body_A->SetFixed(true);
        my_body_A->SetPos(ChVector3d(-0.5, 0, 0));
        sys.Add(my_body_A);

        // Constraint the boundary node to truss
        auto my_root = chrono_types::make_shared<ChLinkMateGeneric>();
        my_root->Initialize(my_node_A_boundary, my_body_A, ChFrame<>(ChVector3d(0, 0, 1), QUNIT));
        sys.Add(my_root);
    }

    if (add_internal_body) {
        // BODY: in the middle, as internal
        auto my_body_B = chrono_types::make_shared<ChBodyEasyBox>(1.8, 1.8, 1.8, 200);
        my_body_B->SetPos(ChVector3d(beam_L * 0.5, 0, 0));
        assembly->AddInternal(my_body_B);

        auto my_mid_constr = chrono_types::make_shared<ChLinkMateGeneric>();
        my_mid_constr->Initialize(builder.GetLastBeamNodes()[n_elements / 2], my_body_B,
                                  ChFrame<>(ChVector3d(beam_L * 0.5, 0, 0), QUNIT));
        assembly->AddInternal(my_mid_constr);
    }

    if (add_boundary_body) {
        // BODY: in the end, as boundary
        auto my_body_C = chrono_types::make_shared<ChBodyEasyBox>(0.8, 0.8, 0.8, 200);
        my_body_C->SetPos(ChVector3d(beam_L, 0, 0));
        assembly->Add(my_body_C);

        auto my_end_constr = chrono_types::make_shared<ChLinkMateGeneric>();
        my_end_constr->Initialize(builder.GetLastBeamNodes().back(), my_body_C,
                                  ChFrame<>(ChVector3d(beam_L, 0, 0), QUNIT));
        assembly->Add(my_end_constr);
    }

    if (add_other_assemblies) {
        // Test how to connect the boundary nodes/bodies of a ChModalAssembly to some other ChAssembly
        // or to other items (bodies, etc.) that are added to the ChSystem, like in this way
        //   ChSystem
        //       ChModalAssembly
        //           internal ChBody, ChNode, etc.
        //           boundary ChBody, ChNode, etc.
        //       ChBody
        //       ChAssembly
        //           ChBody
        //       etc.

        // example if putting additional items directly in the ChSystem:
        auto my_body_D = chrono_types::make_shared<ChBodyEasyBox>(0.2, 0.4, 0.4, 200);
        my_body_D->SetPos(ChVector3d(beam_L * 1.1, 0, 0));
        sys.Add(my_body_D);

        auto my_end_constr2 = chrono_types::make_shared<ChLinkMateGeneric>();
        my_end_constr2->Initialize(builder.GetLastBeamNodes().back(), my_body_D,
                                   ChFrame<>(ChVector3d(beam_L, 0, 0), QUNIT));
        sys.Add(my_end_constr2);

        // example if putting additional items in a second assembly (just a simple rotating blade)
        auto assembly0 = chrono_types::make_shared<ChAssembly>();
        sys.Add(assembly0);

        auto my_body_blade = chrono_types::make_shared<ChBodyEasyBox>(0.2, 0.6, 0.2, 150);
        my_body_blade->SetPos(ChVector3d(beam_L * 1.15, 0.3, 0));
        assembly0->Add(my_body_blade);

        auto rotmotor1 = chrono_types::make_shared<ChLinkMotorRotationSpeed>();
        rotmotor1->Initialize(my_body_blade,                                             // slave
                              my_body_D,                                                 // master
                              ChFrame<>(my_body_D->GetPos(), QuatFromAngleY(CH_PI_2))  // motor frame, in abs. coords
        );
        auto mwspeed =
            chrono_types::make_shared<ChFunctionConst>(CH_2PI);  // constant angular speed, in [rad/s], 2PI/s =360�/s
        rotmotor1->SetSpeedFunction(mwspeed);
        assembly0->Add(rotmotor1);
    }

    if (add_force) {
        // Method A (simple)
        // The simplest way to add a force is to use mynode->SetForce(), or to add some ChBodyLoad.
        // Note: this works only for boundary nodes!
        my_node_B_boundary->SetForce(ChVector3d(0, -3, 0));  // to trigger some vibration at the free end

        // Method B (advanced)
        // Add a force also to internal nodes that will be removed after modal reduction.
        // This can be done using a callback that will be called all times the time integrator needs it.
        // You will provide a custom force writing into computed_custom_F_full vector (note: it is up to you to use the
        // proper indexes)
        class MyCallback : public ChModalAssembly::CustomForceFullCallback {
          public:
            MyCallback(){};
            virtual void evaluate(
                ChVectorDynamic<>&
                    computed_custom_F_full,  //< compute F here, size= m_num_coords_vel_boundary + m_num_coords_vel_internal
                const ChModalAssembly& link  ///< associated modal assembly
            ) {
                // remember! assume F vector is already properly sized, but not zeroed!
                computed_custom_F_full.setZero();

                // just for test, assign a force to a random coordinate of F, here an internal node
                computed_custom_F_full[computed_custom_F_full.size() - 16] = -60;
            }
        };
        auto my_callback = chrono_types::make_shared<MyCallback>();

        assembly->RegisterCallback_CustomForceFull(my_callback);
    }

    // Just for later reference, dump  M,R,K,Cq matrices. Ex. for comparison with Matlab eigs()
    sys.Setup();
    sys.Update();
    assembly->WriteSubassemblyMatrices(true, true, true, true, (out_dir + "/dump").c_str());

    if (do_modal_reduction) {
        // HERE PERFORM THE MODAL REDUCTION!

        assembly->SwitchModalReductionON(
            6,  // The number of modes to retain from modal reduction, or a ChModalSolveUndamped with more settings
            ChModalDampingRayleigh(0.001,
                                   0.005)  // The damping model - Optional parameter: default is ChModalDampingNone().
        );

        // Other types of damping that you can try, in SwitchModalReductionON:
        //    ChModalDampingNone()                    // no damping (also default)
        //    ChModalDampingReductionR(*assembly)  // transforms the original damping matrix of the full subassembly
        //    ChModalDampingReductionR(full_R_ext)    // transforms an externally-provided damping matrix of the full
        //    subassembly ChModalDampingCustom(reduced_R_ext)     // uses an externally-provided damping matrix of the
        //    reduced subassembly ChModalDampingRayleigh(0.01, 0.05)      // generates a damping matrix from reduced M
        //    ad K using Rayleygh alpha-beta ChModalDampingFactorRmm(zetas)          // generates a damping matrix from
        //    damping factors zetas of dynamic modes ChModalDampingFactorRayleigh(zetas,a,b) // generates a damping
        //    matrix from damping factors of dynamic modes and rayleigh a,b for boundary nodes
        //    ChModalDampingFactorAssembly(zetas)     // (not ready) generates a damping matrix from damping factors of
        //    the modes of the subassembly, including masses of boundary
        //      where for example         ChVectorDynamic<> zetas(4);  zetas << 0.7, 0.5, 0.6, 0.7; // damping factors,
        //      other values assumed as last one.

        // OPTIONAL

        // Just for later reference, dump reduced M,R,K,Cq matrices. Ex. for comparison with Matlab eigs()
        assembly->WriteSubassemblyMatrices(true, true, true, true, (out_dir + "/dump_reduced").c_str());

        // Use this for high simulation performance (the internal nodes won't be updated for postprocessing)
        // assembly->SetInternalNodesUpdate(false);

        // Finally, log damped eigenvalue analysis to see the effect of the modal damping (0= search ALL damped modes)
        assembly->ComputeModesDamped(0);

        for (int i = 0; i < assembly->Get_modes_frequencies().rows(); ++i)
            std::cout << " Damped mode n." << i << "  frequency [Hz]: " << assembly->Get_modes_frequencies()(i)
                     << "   damping factor z: " << assembly->Get_modes_damping_ratios()(i) << std::endl;

        // Finally, check if we approximately have the same eigenmodes of the original not reduced assembly:
        assembly->ComputeModes(12);

        // If you need to enter more detailed settings for the eigenvalue solver, do this :
        /*
        assembly->ComputeModes(ChModalSolveUndamped(
            12,             // n. nodes to search
            1e-5,           // base freq.
            500,            // max iterations
            1e-10,          // tolerance
            false,          // verbose
            ChGeneralizedEigenvalueSolverKrylovSchur()) // solver type
        );
        */

        for (int i = 0; i < assembly->Get_modes_frequencies().rows(); ++i)
            std::cout << " Mode n." << i << "  frequency [Hz]: " << assembly->Get_modes_frequencies()(i) << std::endl;

    } else {
        // Otherwise we perform a conventional modal analysis on the full ChModalAssembly.
        assembly->ComputeModes(12);

        // If you need to focus on modes in specific frequency regions, use {nmodes, about_freq} pairs as in :
        /*
        assembly->ComputeModes(ChModalSolveUndamped(
            { { 8, 1e-3 },{2, 2.5} },   // 8 smallest freq.modes, plus 2 modes closest to 2.5 Hz
            500,                        // max iterations per each {modes,freq} pair
            1e-10,                      // tolerance
            false,                      // verbose
            ChGeneralizedEigenvalueSolverKrylovSchur()) //  solver type
        );
        */

        for (int i = 0; i < assembly->Get_modes_frequencies().rows(); ++i)
            std::cout << " Mode n." << i << "  frequency [Hz]: " << assembly->Get_modes_frequencies()(i) << std::endl;
    }

    // VISUALIZATION ASSETS:

    auto visualizeInternalA = chrono_types::make_shared<ChVisualShapeFEA>(mesh_internal);
    visualizeInternalA->SetFEMdataType(ChVisualShapeFEA::DataType::ELEM_BEAM_MY);
    visualizeInternalA->SetColorscaleMinMax(-600, 600);
    visualizeInternalA->SetSmoothFaces(true);
    visualizeInternalA->SetWireframe(false);
    mesh_internal->AddVisualShapeFEA(visualizeInternalA);

    auto visualizeInternalB = chrono_types::make_shared<ChVisualShapeFEA>(mesh_internal);
    visualizeInternalB->SetFEMglyphType(ChVisualShapeFEA::GlyphType::NODE_CSYS);
    visualizeInternalB->SetFEMdataType(ChVisualShapeFEA::DataType::NONE);
    visualizeInternalB->SetSymbolsThickness(0.2);
    visualizeInternalB->SetSymbolsScale(0.1);
    visualizeInternalB->SetZbufferHide(false);
    mesh_internal->AddVisualShapeFEA(visualizeInternalB);

    auto visualizeBoundaryB = chrono_types::make_shared<ChVisualShapeFEA>(mesh_boundary);
    visualizeBoundaryB->SetFEMglyphType(ChVisualShapeFEA::GlyphType::NODE_CSYS);
    visualizeBoundaryB->SetFEMdataType(ChVisualShapeFEA::DataType::NONE);
    visualizeBoundaryB->SetSymbolsThickness(0.4);
    visualizeBoundaryB->SetSymbolsScale(4);
    visualizeBoundaryB->SetZbufferHide(false);
    mesh_boundary->AddVisualShapeFEA(visualizeBoundaryB);

    // This is needed if you want to see things in Irrlicht
    vis.BindAll();

    int current_example = ID_current_example;
    while (ID_current_example == current_example && !SWITCH_EXAMPLE && vis.Run()) {
        vis.BeginScene();
        vis.Render();
        tools::drawGrid(&vis, 1, 1, 12, 12, ChCoordsys<>(ChVector3d(0, 0, 0), CH_PI_2, VECT_Z),
                        ChColor(0.5f, 0.5f, 0.5f), true);
        vis.EndScene();

        if (!modal_analysis)
            sys.DoStepDynamics(step_size);
    }
}

// Custom event manager
class MyEventReceiver : public irr::IEventReceiver {
  public:
    MyEventReceiver(ChVisualSystemIrrlicht& vis) : m_vis(vis) {}

    bool OnEvent(const irr::SEvent& event) {
        if (event.EventType == irr::EET_KEY_INPUT_EVENT && !event.KeyInput.PressedDown) {
            switch (event.KeyInput.Key) {
                case irr::KEY_KEY_1:
                    SWITCH_EXAMPLE = true;
                    DO_MODAL_REDUCTION = !DO_MODAL_REDUCTION;
                    return true;
                case irr::KEY_KEY_2:
                    SWITCH_EXAMPLE = true;
                    ADD_INTERNAL_BODY = !ADD_INTERNAL_BODY;
                    return true;
                case irr::KEY_KEY_3:
                    SWITCH_EXAMPLE = true;
                    ADD_BOUNDARY_BODY = !ADD_BOUNDARY_BODY;
                    return true;
                case irr::KEY_KEY_4:
                    SWITCH_EXAMPLE = true;
                    ADD_FORCE = !ADD_FORCE;
                    return true;
                case irr::KEY_KEY_5:
                    SWITCH_EXAMPLE = true;
                    ADD_OTHER_ASSEMBLY = !ADD_OTHER_ASSEMBLY;
                    return true;
                case irr::KEY_KEY_6:
                    SWITCH_EXAMPLE = true;
                    FIX_SUBASSEMBLY = !FIX_SUBASSEMBLY;
                    return true;

                case irr::KEY_SPACE:
                    modal_analysis = !modal_analysis;
                    m_vis.EnableModalAnalysis(modal_analysis);
                    m_vis.SetInfoTab(modal_analysis ? 1 : 0);
                    return true;
                default:
                    break;
            }
        }
        return false;
    }

  private:
    ChVisualSystemIrrlicht& m_vis;
};

int main(int argc, char* argv[]) {
    std::cout << "Copyright (c) 2021 projectchrono.org\nChrono version: " << CHRONO_VERSION << std::endl;

    // Directory for output data
    if (!filesystem::create_directory(filesystem::path(out_dir))) {
        std::cout << "Error creating directory " << out_dir << std::endl;
        return 1;
    }

    // CREATE THE MODEL

    // Create a Chrono::Engine physical system
    ChSystemNSC sys;

    // no gravity used here
    sys.SetGravitationalAcceleration(VNULL);

    // VISUALIZATION

    // Create the Irrlicht visualization system
    ChVisualSystemIrrlicht vis;
    vis.AttachSystem(&sys);
    vis.SetWindowSize(1024, 768);
    vis.SetWindowTitle("Modal reduction");
    vis.Initialize();
    vis.AddLogo();
    vis.AddSkyBox();
    vis.AddCamera(ChVector3d(1, 1.3, 6), ChVector3d(3, 0, 0));
    vis.AddLightWithShadow(ChVector3d(20, 20, 20), ChVector3d(0, 0, 0), 50, 5, 50, 55);
    vis.AddLight(ChVector3d(-20, -20, 0), 6, ChColor(0.6f, 1.0f, 1.0f));
    vis.AddLight(ChVector3d(0, -20, -20), 6, ChColor(0.6f, 1.0f, 1.0f));

    // This is for GUI tweaking of system parameters..
    MyEventReceiver receiver(vis);
    // note how to add a custom event receiver to the default interface:
    vis.AddUserEventReceiver(&receiver);

    // Some help on the screen
    auto my_gui_info =
        vis.GetGUIEnvironment()->addStaticText(L" ", irr::core::rect<irr::s32>(400, 80, 850, 200), false, true, 0);

    // Set linear solver
#ifdef CHRONO_PARDISO_MKL
    auto mkl_solver = chrono_types::make_shared<ChSolverPardisoMKL>();
    sys.SetSolver(mkl_solver);
#else
    auto qr_solver = chrono_types::make_shared<ChSolverSparseQR>();
    sys.SetSolver(qr_solver);
#endif

    /*
    // Use HHT second order integrator (but slower)
    sys.SetTimestepperType(ChTimestepper::Type::HHT);
    if (auto stepper = std::dynamic_pointer_cast<ChTimestepperHHT>(sys.GetTimestepper())) {
        stepper->SetStepControl(false);
    }
    */

    // Note, in order to have this modal visualization  working, a ChModalAssembly must have been added to the ChSystem,
    // where some modes must have been already computed.
    vis.EnableModalAnalysis(modal_analysis);
    vis.SetModalSpeed(15);
    vis.SetModalAmplitude(0.8);
    vis.SetModalModeNumber(0);

    // Optional: open the GUI and set the tab to either Dynamics or Modal Analysis
    vis.ShowInfoPanel(true);
    vis.SetInfoTab(modal_analysis ? 1 : 0);

    // Run the sub-demos
    while (true) {
        vis.SetModalModeNumber(0);

        my_gui_info->setText(
            (std::wstring(L" Press 1: toggle modal reduction   -now: ") + (DO_MODAL_REDUCTION ? L"ON" : L"OFF") +
             L"\n" + std::wstring(L" Press 2: toggle internal body     -now: ") + (ADD_INTERNAL_BODY ? L"ON" : L"OFF") +
             L"\n" + std::wstring(L" Press 3: toggle boundary body     -now: ") + (ADD_BOUNDARY_BODY ? L"ON" : L"OFF") +
             L"\n" + std::wstring(L" Press 4: toggle forces            -now: ") + (ADD_FORCE ? L"ON" : L"OFF") + L"\n" +
             std::wstring(L" Press 5: toggle add other assembly -now: ") + (ADD_OTHER_ASSEMBLY ? L"ON" : L"OFF") +
             L"\n" + std::wstring(L" Press 6: toggle modal assembly: ") +
             (FIX_SUBASSEMBLY ? L"contains fixed node" : L"is free-free") + L"\n\n" +
             std::wstring(L" Press SPACE: toggle between dynamic and modal analysis"))
                .c_str());

        MakeAndRunDemoCantilever(sys, vis, DO_MODAL_REDUCTION, ADD_INTERNAL_BODY, ADD_BOUNDARY_BODY, ADD_FORCE,
                                 ADD_OTHER_ASSEMBLY, FIX_SUBASSEMBLY);

        SWITCH_EXAMPLE = false;

        if (!vis.Run())
            break;
    }

    return 0;
}
