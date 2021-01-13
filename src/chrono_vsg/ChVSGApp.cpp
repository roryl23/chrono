// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2020 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Rainer Gericke
// =============================================================================
// Vulkan Scene Graph viewer, this class will hopefully draw the system to the
// screen and handle input some day
// =============================================================================

#include "chrono_vsg/core/ChApiVSG.h"

#include "ChVSGApp.h"
#include "chrono/geometry/ChBox.h"
#include "chrono/assets/ChTexture.h"
#include "chrono/assets/ChColorAsset.h"
#include "chrono/assets/ChBoxShape.h"
#include "chrono/assets/ChSphereShape.h"
#include "chrono/assets/ChEllipsoidShape.h"
#include "chrono/assets/ChCylinderShape.h"
#include "chrono_vsg/resources/ChVSGSettings.h"
#include "chrono_vsg/resources/ChVSGPhongMaterial.h"
#include "chrono_vsg/assets/ChPBRSetting.h"
#include "chrono_vsg/shapes/VSGIndexBox.h"
#include "chrono_vsg/shapes/VSGIndexSphere.h"
#include "chrono_vsg/shapes/VSGIndexCylinder.h"

using namespace chrono::vsg3d;

struct Params : public vsg::Inherit<vsg::Object, Params> {
    bool showGui = true;  // you can toggle this with your own EventHandler and key
    bool showDemoWindow = false;
    bool showSecondWindow = false;
    float clearColor[3]{0.2f, 0.2f, 0.4f};  // Unfortunately, this doesn't change dynamically in vsg
    uint32_t counter = 0;
    float dist = 0.f;
};

class MyGuiComponent {
  public:
    MyGuiComponent(vsg::ref_ptr<Params> params) : _params(params) {}

    // Example here taken from the Dear imgui comments (mostly)
    bool operator()() {
        bool visibleComponents = false;

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
        if (_params->showGui) {
            ImGui::Begin("Hello, world!");  // Create a window called "Hello, world!" and append into it.

            ImGui::Text("Some useful message here.");  // Display some text (you can use a format strings too)
            ImGui::Checkbox("Demo Window", &_params->showDemoWindow);  // Edit bools storing our window open/close state
            ImGui::Checkbox("Another Window", &_params->showSecondWindow);
            ImGui::SliderFloat("float", &_params->dist, 0.0f, 1.0f);  // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color", (float*)&_params->clearColor);  // Edit 3 floats representing a color

            if (ImGui::Button(
                    "Button"))  // Buttons return true when clicked (most widgets return true when edited/activated)
                _params->counter++;

            ImGui::SameLine();
            ImGui::Text("counter = %d", _params->counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate,
                        ImGui::GetIO().Framerate);
            ImGui::End();

            visibleComponents = true;
        }

        // 3. Show another simple window.
        if (_params->showSecondWindow) {
            ImGui::Begin("Another Window",
                         &_params->showSecondWindow);  // Pass a pointer to our bool variable (the window will have a
                                                       // closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                _params->showSecondWindow = false;
            ImGui::End();

            visibleComponents = true;
        }

        if (_params->showDemoWindow) {
            ImGui::ShowDemoWindow(&_params->showDemoWindow);

            visibleComponents = true;
        }

        return visibleComponents;
    }

  private:
    vsg::ref_ptr<Params> _params;
};

ChVSGApp::ChVSGApp()
    : m_horizonMountainHeight(0.0),
      m_timeStep(0.001),
      m_outputStep(0.001),
      m_drawMode(DrawMode::Textured),
      m_build_graph(true),
      m_wait_counter(1),
      m_wait_counter_max(1) {
    setClearColor(1.0f, 1.0f, 1.0f);
    m_up_vector = vsg::dvec3(0.0, 0.0, 1.0);
    m_light_position = vsg::vec3(100, 100, 100);

    m_fontFilename = "fonts/times.vsgb";
    m_searchPaths = ::vsg::getEnvPaths("VSG_FILE_PATH");

    auto options = vsg::Options::create();
    options->paths = m_searchPaths;
#ifdef USE_VSGXCHANGE
    options->readerWriter = vsgXchange::ReaderWriter_all::create();
#endif

    m_font = vsg::read_cast<vsg::Font>(m_fontFilename, options);

    if (!m_font) {
        std::cout << "Failling to read font : " << m_fontFilename << std::endl;
        return;
    }
}

void ChVSGApp::setUpVector(ChVector<> up) {
    m_up_vector = vsg::dvec3(up.x(), up.y(), up.z());
}

void ChVSGApp::doTimeStep() {
    m_system->DoStepDynamics(m_timeStep);
    if (m_wait_counter == m_wait_counter_max) {
        UpdateSceneGraph();
    }
    IncreaseWaitCounter();
}

void ChVSGApp::IncreaseWaitCounter() {
    m_wait_counter++;
    if (m_wait_counter > m_wait_counter_max) {
        m_wait_counter = 1;
    }
}

void ChVSGApp::setupTexPool(vsg::ref_ptr<vsg::Window> window, vsg::ViewportState* viewport, uint32_t maxNumTextures) {
    auto device = window->getOrCreateDevice();

    _compile = vsg::CompileTraversal::create(window, viewport);

    // for now just allocated enough room for s
    uint32_t maxSets = maxNumTextures;
    vsg::DescriptorPoolSizes descriptorPoolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxNumTextures}};

    _compile->context.descriptorPool = vsg::DescriptorPool::create(device, maxSets, descriptorPoolSizes);

    _allocatedTextureCount = 0;
    _maxNumTextures = maxNumTextures;
}

void ChVSGApp::compile(vsg::ref_ptr<vsg::Node> subgraph) {
    std::cout << "ChVSGApp::compile(" << subgraph << ") _compile = " << _compile << std::endl;
    if (_compile) {
        subgraph->accept(*_compile);
        _compile->context.record();
        _compile->context.waitForCompletion();
    }
}

bool ChVSGApp::Initialize(int windowWidth, int windowHeight, const char* windowTitle, ChSystem* system) {
    if (!system) {
        return false;
    }
    m_system = system;
    m_windowTraits = ::vsg::WindowTraits::create();
    m_windowTraits->windowTitle = windowTitle;
    m_windowTraits->width = windowWidth;
    m_windowTraits->height = windowHeight;
    m_windowTraits->x = 100;
    m_windowTraits->y = 100;

    m_scenegraph = vsg::Group::create();

    // adjust output wait states
    if (m_outputStep <= m_timeStep) {
        m_wait_counter_max = 1;
    } else {
        m_wait_counter_max = size_t(m_outputStep / m_timeStep);
    }
    // fill the scenegraph with asset definitions from the physical system
    BuildSceneGraph();

    // create viewer
    m_viewer = ::vsg::Viewer::create();

    // create window
    m_window = ::vsg::Window::create(m_windowTraits);
    // if (!window) {
    if (!m_window) {
        GetLog() << "Could not create windows.\n";
        return false;
    }

    VkClearColorValue& clearColor = m_window->clearColor();
    for (int i = 0; i < 4; i++) {
        clearColor.float32[i] = m_clearColor[i];
    }
    m_viewer->addWindow(m_window);

    // compute the bounds of the scene graph to help position camera
    vsg::ComputeBounds computeBounds;
    m_scenegraph->accept(computeBounds);
    vsg::dvec3 centre = (computeBounds.bounds.min + computeBounds.bounds.max) * 0.5;
    double radius = vsg::length(computeBounds.bounds.max - computeBounds.bounds.min) * 0.6;
    double nearFarRatio = 0.001;
    GetLog() << "BoundMin = {" << computeBounds.bounds.min.x << ";" << computeBounds.bounds.min.y << ";"
             << computeBounds.bounds.min.z << "}\n";
    GetLog() << "BoundMax = {" << computeBounds.bounds.max.x << ";" << computeBounds.bounds.max.y << ";"
             << computeBounds.bounds.max.z << "}\n";
    // set up the camera
    auto lookAt = vsg::LookAt::create(-(centre + vsg::dvec3(0.0, -radius * 3.5, 0.0)), centre, m_up_vector);

    auto perspective = vsg::Perspective::create(
        30.0, static_cast<double>(m_window->extent2D().width) / static_cast<double>(m_window->extent2D().height),
        nearFarRatio * radius, radius * 4.5);

    auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(m_window->extent2D()));

    // setup texture pool
    // setupTexPool(m_window, camera->getViewportState(), 128);
    // compile(m_scenegraph);

    auto commandGraph = vsg::createCommandGraphForView(m_window, camera, m_scenegraph);
    auto renderGraph = vsg::RenderGraph::create(m_window);
    commandGraph->addChild(renderGraph);

    // create the normal 3D view of the scene
    renderGraph->addChild(vsg::View::create(camera, m_scenegraph));

    // Create the ImGui node and add it to the renderGraph
    auto params = Params::create();
    renderGraph->addChild(vsgImGui::RenderImGui::create(m_window, MyGuiComponent(params)));

    // Add the ImGui event handler first to handle events early
    m_viewer->addEventHandler(vsgImGui::SendEventsToImGui::create());

    // add close handler to respond to pressing the window close window button and pressing escape
    m_viewer->addEventHandler(::vsg::CloseHandler::create(m_viewer));

    // add a trackball event handler to control the camera view use the mouse
    m_viewer->addEventHandler(::vsg::Trackball::create(camera));

    m_viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});

    m_viewer->compile();
    return true;
}

ChVSGApp::~ChVSGApp() {
    ;
}

void ChVSGApp::Render() {
    m_viewer->handleEvents();
    m_viewer->update();
    m_viewer->recordAndSubmit();
    m_viewer->present();
}

void ChVSGApp::BuildSceneGraph() {
    // analyse system, look for bodies and assets
    for (auto body : m_system->Get_bodylist()) {
        // position of the body
        const Vector pos = body->GetFrame_REF_to_abs().GetPos();
        // rotation of the body
        Quaternion rot = body->GetFrame_REF_to_abs().GetRot();
        double angle;
        Vector axis;
        rot.Q_to_AngAxis(angle, axis);
        bool textureFound = false;
        bool colorFound = false;
        bool pbrMapsFound = false;
        bool pbrSetFound = false;
        ChTexture bodyTexture;
        ChPBRSetting bodyPBRSet;
        ChPBRMaps bodyPBRMaps;
        ChColor bodyColor(1.0, 0.0, 0.0, 1.0);
        for (int i = 0; i < body->GetAssets().size(); i++) {
            auto asset = body->GetAssets().at(i);
            if (std::dynamic_pointer_cast<ChColorAsset>(asset)) {
                ChColorAsset* color_asset = (ChColorAsset*)(asset.get());
                bodyColor.R = color_asset->GetColor().R;
                bodyColor.G = color_asset->GetColor().G;
                bodyColor.B = color_asset->GetColor().B;
                bodyColor.A = color_asset->GetColor().A;
                colorFound = true;
            }
            if (std::dynamic_pointer_cast<ChTexture>(asset)) {
                ChTexture* texture_asset = (ChTexture*)(asset.get());
                bodyTexture.SetTextureFilename(texture_asset->GetTextureFilename());
                textureFound = true;
            }
            if (std::dynamic_pointer_cast<ChPBRSetting>(asset)) {
                ChPBRSetting* texture_asset = (ChPBRSetting*)(asset.get());
                bodyPBRSet.SetAlbedo(texture_asset->GetAlbedo());
                bodyPBRSet.SetMetallic(texture_asset->GetMetallic());
                bodyPBRSet.SetRoughness(texture_asset->GetRoughness());
                bodyPBRSet.SetAO(texture_asset->GetAO());
                pbrSetFound = true;
            }
            if (std::dynamic_pointer_cast<ChPBRMaps>(asset)) {
                ChPBRMaps* texture_asset = (ChPBRMaps*)(asset.get());
                bodyPBRMaps.SetAlbedoMapPath(texture_asset->GetAlbedoMapPath());
                bodyPBRMaps.SetNormalMapPath(texture_asset->GetNormalMapPath());
                bodyPBRMaps.SetMetallicMapPath(texture_asset->GetMetallicMapPath());
                bodyPBRMaps.SetRoughnessMapPath(texture_asset->GetRoughnessMapPath());
                bodyPBRMaps.SetAoMapPath(texture_asset->GetAoMapPath());
                pbrMapsFound = true;
            }
        }
        for (int i = 0; i < body->GetAssets().size(); i++) {
            auto asset = body->GetAssets().at(i);

            ChVisualization* visual_asset = ((ChVisualization*)(asset.get()));
            // position of the asset
            Vector center = visual_asset->Pos;
            // rotate asset pos into global frame
            center = rot.Rotate(center);
            // Get the local rotation of the asset
            Quaternion lrot = visual_asset->Rot.Get_A_quaternion();
            // add the local rotation to the rotation of the body
            lrot = rot % lrot;
            lrot.Normalize();
            lrot.Q_to_AngAxis(angle, axis);
            if (ChBoxShape* box_shape = dynamic_cast<ChBoxShape*>(asset.get())) {
                // GetLog() << "Found BoxShape!\n";
                ChVector<> size = box_shape->GetBoxGeometry().GetSize();
                ChVector<> pos_final = pos + center;
                auto transform = vsg::MatrixTransform::create();
                transform->setMatrix(vsg::translate(pos_final.x(), pos_final.y(), pos_final.z()) *
                                     vsg::rotate(angle, axis.x(), axis.y(), axis.z()) *
                                     vsg::scale(size.x(), size.y(), size.z()));
                VSGIndexBox box(body, asset, transform);
                if (textureFound) {
                    box.Initialize(bodyTexture);
                } else if (colorFound) {
                    box.Initialize(bodyColor);
                } else if (pbrSetFound) {
                    box.Initialize(bodyPBRSet);
                } else if (pbrMapsFound) {
                    box.Initialize(bodyPBRMaps);
                }
                vsg::ref_ptr<vsg::Node> node = box.createVSGNode();
                m_scenegraph->addChild(node);
            }
            if (ChSphereShape* sphere_shape = dynamic_cast<ChSphereShape*>(asset.get())) {
                // GetLog() << "Found SphereShape!\n";
                double radius = sphere_shape->GetSphereGeometry().rad;
                ChVector<> size(radius, radius, radius);
                ChVector<> pos_final = pos + center;

                auto transform = vsg::MatrixTransform::create();
                transform->setMatrix(vsg::translate(pos_final.x(), pos_final.y(), pos_final.z()) *
                                     vsg::rotate(angle, axis.x(), axis.y(), axis.z()) *
                                     vsg::scale(size.x(), size.y(), size.z()));

                VSGIndexSphere sphere(body, asset, transform);
                if (textureFound) {
                    sphere.Initialize(bodyTexture);
                } else if (colorFound) {
                    sphere.Initialize(bodyColor);
                } else if (pbrSetFound) {
                    sphere.Initialize(bodyPBRSet);
                } else if (pbrMapsFound) {
                    sphere.Initialize(bodyPBRMaps);
                }
                vsg::ref_ptr<vsg::Node> node = sphere.createVSGNode();
                m_scenegraph->addChild(node);
            }
            if (ChEllipsoidShape* ellipsoid_shape = dynamic_cast<ChEllipsoidShape*>(asset.get())) {
                // GetLog() << "Found ElipsoidShape!\n";
                Vector radius = ellipsoid_shape->GetEllipsoidGeometry().rad;
                ChVector<> size(radius.x(), radius.y(), radius.z());
                ChVector<> pos_final = pos + center;
                auto transform = vsg::MatrixTransform::create();

                transform->setMatrix(vsg::translate(pos_final.x(), pos_final.y(), pos_final.z()) *
                                     vsg::rotate(angle, axis.x(), axis.y(), axis.z()) *
                                     vsg::scale(size.x(), size.y(), size.z()));

                VSGIndexSphere ellipsoid(body, asset, transform);
                if (textureFound) {
                    ellipsoid.Initialize(bodyTexture);
                } else if (colorFound) {
                    ellipsoid.Initialize(bodyColor);
                } else if (pbrSetFound) {
                    ellipsoid.Initialize(bodyPBRSet);
                } else if (pbrMapsFound) {
                    ellipsoid.Initialize(bodyPBRMaps);
                }
                vsg::ref_ptr<vsg::Node> node = ellipsoid.createVSGNode();
                m_scenegraph->addChild(node);
            }
            if (ChCylinderShape* cylinder_shape = dynamic_cast<ChCylinderShape*>(asset.get())) {
                // GetLog() << "Found CylinderShape!\n";
                double radius = cylinder_shape->GetCylinderGeometry().rad;
                ChVector<> dir = cylinder_shape->GetCylinderGeometry().p1 - cylinder_shape->GetCylinderGeometry().p2;
                double height = dir.Length();
                ChVector<> pos_final = pos + center;
                auto transform = vsg::MatrixTransform::create();
                transform->setMatrix(vsg::translate(pos_final.x(), pos_final.y(), pos_final.z()) *
                                     vsg::rotate(angle, axis.x(), axis.y(), axis.z()) *
                                     vsg::scale(radius, radius, height));
                VSGIndexCylinder cylinder(body, asset, transform);
                if (textureFound) {
                    cylinder.Initialize(bodyTexture);
                } else if (colorFound) {
                    cylinder.Initialize(bodyColor);
                } else if (pbrSetFound) {
                    cylinder.Initialize(bodyPBRSet);
                } else if (pbrMapsFound) {
                    cylinder.Initialize(bodyPBRMaps);
                }
                vsg::ref_ptr<vsg::Node> node = cylinder.createVSGNode();
                m_scenegraph->addChild(node);
            }
        }
    }
    m_build_graph = false;
}

void ChVSGApp::UpdateSceneGraph() {
    for (auto body : m_system->Get_bodylist()) {
        // position of the body
        const Vector pos = body->GetFrame_REF_to_abs().GetPos();
        // rotation of the body
        Quaternion rot = body->GetFrame_REF_to_abs().GetRot();
        double angle;
        Vector axis;
        rot.Q_to_AngAxis(angle, axis);
        for (int i = 0; i < body->GetAssets().size(); i++) {
            auto asset = body->GetAssets().at(i);

            if (!std::dynamic_pointer_cast<ChVisualization>(asset)) {
                continue;
            }
            ChVisualization* visual_asset = ((ChVisualization*)(asset.get()));
            // position of the asset
            Vector center = visual_asset->Pos;
            // rotate asset pos into global frame
            center = rot.Rotate(center);
            // Get the local rotation of the asset
            Quaternion lrot = visual_asset->Rot.Get_A_quaternion();
            // add the local rotation to the rotation of the body
            lrot = rot % lrot;
            lrot.Normalize();
            lrot.Q_to_AngAxis(angle, axis);
            if (ChBoxShape* box_shape = dynamic_cast<ChBoxShape*>(asset.get())) {
                // GetLog() << "Found BoxShape!\n";
                ChVector<> size = box_shape->GetBoxGeometry().GetSize();
                ChVector<> pos_final = pos + center;
                auto tm = GetTransform(body, asset);
                tm->setMatrix(vsg::translate(pos_final.x(), pos_final.y(), pos_final.z()) *
                              vsg::rotate(angle, axis.x(), axis.y(), axis.z()) *
                              vsg::scale(size.x(), size.y(), size.z()));
            }
            if (ChSphereShape* sphere_shape = dynamic_cast<ChSphereShape*>(asset.get())) {
                // GetLog() << "Found SphereShape!\n";
                double radius = sphere_shape->GetSphereGeometry().rad;
                ChVector<> size(radius, radius, radius);
                ChVector<> pos_final = pos + center;
                auto tm = GetTransform(body, asset);
                tm->setMatrix(vsg::translate(pos_final.x(), pos_final.y(), pos_final.z()) *
                              vsg::rotate(angle, axis.x(), axis.y(), axis.z()) *
                              vsg::scale(size.x(), size.y(), size.z()));
            }
            if (ChEllipsoidShape* ellipsoid_shape = dynamic_cast<ChEllipsoidShape*>(asset.get())) {
                // GetLog() << "Found ElipsoidShape!\n";
                Vector radius = ellipsoid_shape->GetEllipsoidGeometry().rad;
                ChVector<> size(radius.x(), radius.y(), radius.z());
                ChVector<> pos_final = pos + center;
                auto tm = GetTransform(body, asset);
                tm->setMatrix(vsg::translate(pos_final.x(), pos_final.y(), pos_final.z()) *
                              vsg::rotate(angle, axis.x(), axis.y(), axis.z()) *
                              vsg::scale(size.x(), size.y(), size.z()));
            }
            if (ChCylinderShape* cylinder_shape = dynamic_cast<ChCylinderShape*>(asset.get())) {
                // GetLog() << "Found CylinderShape!\n";
                double radius = cylinder_shape->GetCylinderGeometry().rad;
                ChVector<> dir = cylinder_shape->GetCylinderGeometry().p1 - cylinder_shape->GetCylinderGeometry().p2;
                double height = dir.Length();
                ChVector<> pos_final = pos + center;
                auto tm = GetTransform(body, asset);
                tm->setMatrix(vsg::translate(pos_final.x(), pos_final.y(), pos_final.z()) *
                              vsg::rotate(angle, axis.x(), axis.y(), axis.z()) * vsg::scale(radius, radius, height));
            }
        }
    }
}

vsg::ref_ptr<vsg::MatrixTransform> ChVSGApp::GetTransform(std::shared_ptr<ChBody> body,
                                                          std::shared_ptr<ChAsset> asset) {
    vsg::ref_ptr<vsg::MatrixTransform> transform;
    size_t numCh = m_scenegraph->getNumChildren();
    for (size_t iChild = 0; iChild < numCh; iChild++) {
        auto myNode = m_scenegraph->getChild(iChild);
        std::shared_ptr<ChBody> bodyInNode;
        bool bodyOk = myNode->getValue("bodyPtr", bodyInNode);
        std::shared_ptr<ChAsset> assetInNode;
        bool assetOk = myNode->getValue("assetPtr", assetInNode);
        if (assetOk && bodyOk && (body == bodyInNode) && (asset == assetInNode)) {
            bool transformOk = myNode->getValue("transform", transform);
            if (transformOk) {
                break;
            } else {
                GetLog() << "UpdateSceneGraph(): ill shaped group node, should never happen.\n";
            }
        }
    }
    return transform;
}
