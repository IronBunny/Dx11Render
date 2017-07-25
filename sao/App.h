/**
  \file App.h

  This project uses G3D 9.00 SVN 2012-03-22 (http://g3d.sf.net)

 */
#ifndef App_h
#define App_h

#include <G3D/G3DAll.h>
#include "SAO.h"
#include "Scene.h"

class App : public GApp {
    SAO::Ref           m_SAO;
    Texture::Ref        m_aoBuffer;
    Framebuffer::Ref    m_aoResultFramebuffer;

    Profiler            m_profiler;

    GuiDropDownList*    m_sceneDropDownList;
    Scene::Ref          m_scene;

    Shader::Ref         m_deferredShader;

    ShadowMap::Ref      m_shadowMap;
    GBuffer::Ref        m_gbuffer;

    GFont::Ref          m_perfFont;
    GuiLabel*           m_perfLabel;

    float               m_aoIntensity;

    bool                m_useAO;
    bool                m_useTexture;
    bool                m_useEnvironmentMap;

    /** Used for enabling dragging of objects with m_splineEditor.*/
    Entity::Ref         m_selectedEntity;

    /** Used for editing entity splines.*/
    PhysicsFrameSplineEditor::Ref   m_splineEditor;

    GuiDropDownList*    m_entityList;

    /** Don't allow object editing */
    bool                m_preventEntityDrag;
    bool                m_preventEntitySelect;

    bool                m_showAxes;
    bool                m_showLightSources;
    bool                m_showWireframe;

    void reloadShaders();

    /** Loads whatever scene is currently selected in the m_sceneDropDownList. */
    void loadScene();

    /** Save the current scene over the one on disk. */
    void saveScene();

    /** Called from onInit */
    void makeGUI();

    void selectEntity(const Entity::Ref& e);

public:
    
    App(const GApp::Settings& settings = GApp::Settings());

    virtual void onInit() override;
    virtual void onSimulation(RealTime rdt, SimTime sdt, SimTime idt) override;
    virtual void onPose(Array<Surface::Ref>& posed3D, Array<Surface2D::Ref>& posed2D) override;

    // You can override onGraphics if you want more control over the rendering loop.
    // virtual void onGraphics(RenderDevice* rd, Array<Surface::Ref>& surface, Array<Surface2D::Ref>& surface2D) override;

    virtual void onGraphics3D(RenderDevice* rd, Array<Surface::Ref>& posed3D) override;
    virtual void onGraphics2D(RenderDevice* rd, Array<Surface2D::Ref>& posed2D) override;

    virtual bool onEvent(const GEvent& e) override;
    virtual void onUserInput(UserInput* ui) override;

    /** Sets m_endProgram to true. */
    virtual void endProgram();
};

#endif
