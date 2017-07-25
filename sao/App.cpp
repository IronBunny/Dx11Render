/** \file App.cpp 
*/
#include "App.h"

// Tells C++ to invoke command-line main() function even on OS X and Win32.
G3D_START_AT_MAIN();

// A guard band of COMPUTE_GUARD_BAND is applied on each side.  AO is not computed within the
// guard band, but the G-buffer must have valid data there.
//static const int COMPUTE_WIDTH = 2560, COMPUTE_HEIGHT = 1600, COMPUTE_GUARD_BAND = 256;
static const int COMPUTE_WIDTH = 1920, COMPUTE_HEIGHT = 1080, COMPUTE_GUARD_BAND = 192;

int main(int argc, const char* argv[]) {
    // Go to the right directory if under a debugger
    if (endsWith(FileSystem::currentDirectory(), "Release") || (endsWith(FileSystem::currentDirectory(), "Debug"))) {
        debugPrintf("Running under Visual Studio debugger...changing to parent directory.");
        chdir("..");
        FileSystem::clearCache();
    }

    alwaysAssertM(FileSystem::exists("SAO_AO.pix"), 
        std::string("Cannot find data files in the current directory (") + FileSystem::currentDirectory() + ")");
    
    // Configure the application window
    GApp::Settings settings(argc, argv);
    
    settings.window.width       = min(COMPUTE_WIDTH, int(OSWindow::primaryDisplayWindowSize().x));
    settings.window.height      = min(COMPUTE_HEIGHT, int(OSWindow::primaryDisplayWindowSize().y));
    settings.window.caption     = "Scalable Ambient Obscurance Demo";
    settings.window.defaultIconFilename = "icon.png";
    settings.dataDir            = "data";

    return App(settings).run();
}


App::App(const GApp::Settings& settings) : GApp(settings) {
#   ifdef G3D_DEBUG
        // Let the debugger catch unhandled exceptions
        catchCommonExceptions = false;
#   endif
}


void App::onInit() {
    GApp::onInit();
    // Called before the application loop beings.  Load data here and
    // not in the constructor so that common exceptions will be
    // automatically caught.

    showRenderingStats    = false;
    m_showLightSources    = false;
    m_showAxes            = false;
    m_showWireframe       = false;
    m_preventEntityDrag   = false;
    m_preventEntitySelect = false;
    m_aoIntensity         = 1.0f;
    m_useAO               = true;
    m_useTexture          = true;
    m_useEnvironmentMap   = true;

    GBuffer::Specification spec;

    // These fields are only needed for the deferred shading in the demo. A forward
    // rendering pipeline could ignore them.
    spec.format[GBuffer::Field::CS_POSITION]       = ImageFormat::RGB32F();
    spec.format[GBuffer::Field::WS_NORMAL]         = ImageFormat::RGB16F();
    spec.format[GBuffer::Field::LAMBERTIAN]        = ImageFormat::RGB8();

    // Results are equivalent with DEPTH24 and DEPTH32; DEPTH16 is too low-precision
    spec.format[GBuffer::Field::DEPTH_AND_STENCIL] = ImageFormat::DEPTH32F();
    spec.depthEncoding = DepthEncoding::HYPERBOLIC;

    m_gbuffer = GBuffer::create(spec);

    m_film->setAntialiasingEnabled(true);

    m_SAO = SAO::create();
    m_aoBuffer = Texture::createEmpty("m_aoBuffer", COMPUTE_WIDTH + 2 * COMPUTE_GUARD_BAND, COMPUTE_HEIGHT + 2 * COMPUTE_GUARD_BAND, ImageFormat::R8(), Texture::DIM_2D_NPOT, Texture::Settings::buffer());
    m_aoResultFramebuffer = Framebuffer::create("aoResultFramebuffer");
    m_aoResultFramebuffer->set(Framebuffer::COLOR0, m_aoBuffer);

    reloadShaders();

    makeGUI();

    // Start wherever the developer HUD last marked as "Home"
    defaultCamera.setCoordinateFrame(bookmark("Home"));

    m_shadowMap = ShadowMap::create();

    m_sceneDropDownList->setSelectedValue("Sponza");
    loadScene();
}


void App::reloadShaders() {
    m_SAO->reloadShaders();
    m_deferredShader = Shader::fromFiles("", "deferred.pix");
}


void App::makeGUI() {
    // Turn on the developer HUD
    developerWindow->videoRecordDialog->setScreenShotFormat("PNG");
    developerWindow->videoRecordDialog->setEnabled(true);
    developerWindow->videoRecordDialog->setCaptureGui(false);
    developerWindow->setVisible(false);
    developerWindow->cameraControlWindow->setVisible(false);
    debugWindow->setVisible(false);
    
    GFont::Ref iconFont = GFont::fromFile(System::findDataFile("icon.fnt"));
    
    {
        // Create a scene management GUI
        GuiPane* scenePane = debugPane->addPane("Scene", GuiTheme::ORNATE_PANE_STYLE);
        scenePane->moveBy(0, -10);
        scenePane->beginRow(); {
            // Example of using a callback; you can also listen for events in onEvent or bind controls to data
            m_sceneDropDownList = scenePane->addDropDownList("", Scene::sceneNames(), NULL, GuiControl::Callback(this, &App::loadScene));

            static const char* reloadIcon = "q";
            static const char* diskIcon = "\xcd";

            scenePane->addButton(GuiText(reloadIcon, iconFont, 14), this, &App::loadScene, GuiTheme::TOOL_BUTTON_STYLE)->setWidth(32);
            scenePane->addButton(GuiText(diskIcon, iconFont, 18), this, &App::saveScene, GuiTheme::TOOL_BUTTON_STYLE)->setWidth(32);
        } scenePane->endRow();

        const int w = 120;
        scenePane->beginRow(); {
            scenePane->addCheckBox("Axes", &m_showAxes)->setWidth(w);
            scenePane->addCheckBox("Light sources", &m_showLightSources);
        } scenePane->endRow();
        scenePane->beginRow(); {
            scenePane->addCheckBox("Wireframe", &m_showWireframe)->setWidth(w);
            scenePane->addCheckBox("Profile", Pointer<bool>(&m_profiler, &Profiler::enabled, &Profiler::setEnabled));
        } scenePane->endRow();
        static const char* lockIcon = "\xcf";
        scenePane->addCheckBox(GuiText(lockIcon, iconFont, 20), &m_preventEntityDrag, GuiTheme::TOOL_CHECK_BOX_STYLE);
        scenePane->pack();

        GuiPane* entityPane = debugPane->addPane("Entity", GuiTheme::ORNATE_PANE_STYLE);
        entityPane->moveRightOf(scenePane);
        entityPane->moveBy(10, 0);
        m_entityList = entityPane->addDropDownList("Name");

        // Dock the spline editor
        m_splineEditor = PhysicsFrameSplineEditor::create("Spline Editor", entityPane);
        addWidget(m_splineEditor);
        developerWindow->cameraControlWindow->moveTo(Point2(window()->width() - developerWindow->cameraControlWindow->rect().width(), 0));
        m_splineEditor->moveTo(developerWindow->cameraControlWindow->rect().x0y0() - Vector2(m_splineEditor->rect().width(), 0));
        entityPane->pack();

        GuiPane* aoPane = debugPane->addPane("AO", GuiTheme::ORNATE_PANE_STYLE);
        aoPane->moveRightOf(entityPane);
        aoPane->moveBy(10, 0);

        aoPane->addNumberBox("Radius",    Pointer<float>(m_SAO, &SAO::radius,    &SAO::setRadius),    "m", GuiTheme::LOG_SLIDER,    0.010f,  4.0f);
        aoPane->addNumberBox("Bias",      Pointer<float>(m_SAO, &SAO::bias,      &SAO::setBias),      "m", GuiTheme::LINEAR_SLIDER, 0.000f,  0.5f);
        aoPane->addNumberBox("Darkness",  &m_aoIntensity,                                                "x", GuiTheme::LOG_SLIDER,    0.001f,  4.0f);

        aoPane->addLabel("Lighting Terms:");
        aoPane->addCheckBox("AO",          &m_useAO); 
        aoPane->addCheckBox("Environment", &m_useEnvironmentMap); 
        aoPane->addCheckBox("Texture",     &m_useTexture); 

        aoPane->pack();

        debugWindow->pack();
        debugWindow->setRect(Rect2D::xywh(0, 0, window()->width(), debugWindow->rect().height()));
    }

    //////////////////////////////////////////////////////////////////////////////
    {
        GuiWindow::Ref demoWindow = GuiWindow::create("", NULL, Rect2D::xywh(0,0,100,100), GuiTheme::PANEL_WINDOW_STYLE);
        GuiLabel* temp;

        temp = demoWindow->pane()->addLabel(GuiText("Scalable Ambient Obscurance", NULL, 18.0f));
        temp->moveBy(5, 0);

        GuiPane* aoPane = demoWindow->pane()->addPane("Ambient Obscurance", GuiTheme::ORNATE_PANE_STYLE);
        aoPane->moveBy(0, 15);
        {
            const int w = 260;
            aoPane->addNumberBox("Radius",    Pointer<float>(m_SAO, &SAO::radius,    &SAO::setRadius),    "m", GuiTheme::LOG_SLIDER,    0.010f,  1.5f)->setWidth(w);
            aoPane->addNumberBox("Bias",      Pointer<float>(m_SAO, &SAO::bias,      &SAO::setBias),      "m", GuiTheme::LINEAR_SLIDER, 0.000f,  0.5f)->setWidth(w);
            aoPane->addNumberBox("Darkness",  &m_aoIntensity,                                                "x", GuiTheme::LOG_SLIDER,    0.001f,  4.0f)->setWidth(w);
            aoPane->pack();
        }

        GuiPane* showPane = demoWindow->pane()->addPane("View", GuiTheme::ORNATE_PANE_STYLE);
        {
            const int w = 120;
            showPane->beginRow(); {
                showPane->addCheckBox("AO",          &m_useAO)->setWidth(w);
                showPane->addCheckBox("Environment", &m_useEnvironmentMap)->setWidth(w);
            } showPane->endRow();
            showPane->beginRow(); {
                showPane->addCheckBox("Texture",    &m_useTexture)->setWidth(w);
                showPane->addCheckBox("Wireframe",  &m_showWireframe)->setWidth(w);
            } showPane->endRow();
            showPane->setWidth(aoPane->rect().width());
        }
        GuiPane* perfPane = demoWindow->pane()->addPane(format("AO Pass Time (%dx%d + %d)", window()->width(), window()->height(), COMPUTE_GUARD_BAND), GuiTheme::ORNATE_PANE_STYLE);
        m_perfFont = GFont::fromFile(System::findDataFile("arial.fnt"));
        m_perfLabel = perfPane->addLabel(GuiText("x.xx ms", m_perfFont, 18, Color3::black()));
        m_perfLabel->moveBy(90, -5);
        if ((COMPUTE_WIDTH > window()->width()) || (COMPUTE_HEIGHT > window()->height())) {
            perfPane->addLabel("For profiling purposes, AO was computed at higher resolution than the displayed result")->setSize(aoPane->rect().width(), 50);
        }
        perfPane->setSize(aoPane->rect().width(), 100);

        GuiPane* systemPane = demoWindow->pane()->addPane("System", GuiTheme::ORNATE_PANE_STYLE);
        systemPane->moveBy(0, 0);
        systemPane->addLabel("GPU:");
        systemPane->addLabel(GLCaps::vendor())->moveBy(10, -10);
        systemPane->addLabel(GLCaps::renderer())->moveBy(10, -10);
        systemPane->addLabel("CPU:");
        systemPane->addLabel(System::cpuArchitecture() + format(", %4.1f GHz", System::cpuSpeedMHz() / 1000.0f))->moveBy(10, -10);
        systemPane->setSize(aoPane->rect().width(), 135);

        temp = demoWindow->pane()->addLabel("Controls");
        temp->moveBy(5, 0);

        Texture::Ref guide = Texture::fromFile(System::findDataFile("keyguide-small.png"));
        temp = demoWindow->pane()->addLabel(GuiText(guide, guide->rect2DBounds()));
        temp->moveBy(70, 0);

        Texture::Ref credits = Texture::fromFile("credits.png");
        temp = demoWindow->pane()->addLabel(GuiText(credits, credits->rect2DBounds()));
        temp->moveBy(5, 50);

        demoWindow->pack();
        demoWindow->setRect(Rect2D::xywh(0, 0, 291, window()->height()));

        showPane->setWidth(aoPane->rect().width());
        perfPane->setSize(aoPane->rect().width(), 100);
        systemPane->setSize(aoPane->rect().width(), 130);
            
        addWidget(demoWindow);
        demoWindow->setVisible(true);
    }
}


void App::loadScene() {
    const std::string& sceneName = m_sceneDropDownList->selectedValue().text();

    // Use immediate mode rendering to force a simple message onto the screen
    drawMessage("Loading " + sceneName + "...");

    // Load the scene
    try {
        m_scene = Scene::create(sceneName, defaultCamera);
        defaultController->setFrame(defaultCamera.coordinateFrame());

        // Populate the entity list
        Array<std::string> nameList;
        m_scene->getEntityNames(nameList);
        m_entityList->clear();
        m_entityList->append("<none>");
        for (int i = 0; i < nameList.size(); ++i) {
            m_entityList->append(nameList[i]);
        }

    } catch (const ParseError& e) {
        const std::string& msg = e.filename + format(":%d(%d): ", e.line, e.character) + e.message;
        drawMessage(msg);
        debugPrintf("%s", msg.c_str());
        System::sleep(5);
        m_scene = NULL;
    }
}


void App::onSimulation(RealTime rdt, SimTime sdt, SimTime idt) {
    GApp::onSimulation(rdt, sdt, idt);

    m_splineEditor->setEnabled(m_splineEditor->enabled() && ! m_preventEntityDrag);
    m_splineEditor->setVisible(false);//m_splineEditor->enabled());

    // Add physical simulation here.  You can make your time
    // advancement based on any of the three arguments.
    if (m_scene.notNull()) {
        if (m_selectedEntity.notNull() && m_splineEditor->enabled()) {
            // Apply the edited spline.  Do this before object simulation, so that the object
            // is in sync with the widget for manipulating it.
            m_selectedEntity->setFrameSpline(m_splineEditor->spline());
        }

        m_scene->onSimulation(sdt);
    }
}


bool App::onEvent(const GEvent& event) {
    if (GApp::onEvent(event)) {
        return true;
    }

    if (event.type == GEventType::VIDEO_RESIZE) {
        // Example GUI dynamic layout code.  Resize the debugWindow to fill
        // the screen horizontally.
        debugWindow->setRect(Rect2D::xywh(0, 0, window()->width(), debugWindow->rect().height()));
    }


    if (! m_preventEntitySelect && (event.type == GEventType::MOUSE_BUTTON_DOWN) && (event.button.button == 0)) {
        // Left click: select by casting a ray through the center of the pixel
        const Ray& ray = defaultCamera.worldRay(event.button.x + 0.5f, event.button.y + 0.5f, renderDevice->viewport());
        
        float distance = finf();

        selectEntity(m_scene->intersect(ray, distance));
    }
    

    if (! m_preventEntitySelect && (event.type == GEventType::GUI_ACTION) && (event.gui.control == m_entityList)) {
        // User clicked on dropdown list
        selectEntity(m_scene->entity(m_entityList->selectedValue().text()));
    }

    // If you need to track individual UI events, manage them here.
    // Return true if you want to prevent other parts of the system
    // from observing this specific event.
    //
    // For example,
    // if ((event.type == GEventType::GUI_ACTION) && (event.gui.control == m_button)) { ... return true;}
    if ((event.type == GEventType::KEY_DOWN) && (event.key.keysym.sym == 'r')) { reloadShaders(); return true; }

    return false;
}


void App::selectEntity(const Entity::Ref& e) {
    m_selectedEntity = e;

    if (m_selectedEntity.notNull()) {
        m_splineEditor->setSpline(m_selectedEntity->frameSpline());
        m_splineEditor->setEnabled(! m_preventEntityDrag);
        m_entityList->setSelectedValue(m_selectedEntity->name());
    } else {
        m_splineEditor->setEnabled(false);
    }
}


void App::onUserInput(UserInput* ui) {
    GApp::onUserInput(ui);
    (void)ui;
    // Add key handling here based on the keys currently held or
    // ones that changed in the last frame.
}


void App::onPose(Array<Surface::Ref>& posed3D, Array<Surface2D::Ref>& posed2D) {
    GApp::onPose(posed3D, posed2D);

    // Append any models to the arrays that you want to later be rendered by onGraphics()
    if (m_scene.notNull()) {
        m_scene->onPose(posed3D);
    }
}


void App::onGraphics3D(RenderDevice* rd, Array<Surface::Ref>& surface3D) {
    if (m_scene.isNull()) {
        return;
    }

    // Create the GBuffer
    m_gbuffer->resize(COMPUTE_WIDTH + 2 * COMPUTE_GUARD_BAND, COMPUTE_HEIGHT + 2 * COMPUTE_GUARD_BAND);
    m_gbuffer->prepare(rd, defaultCamera, 0, -1.0f / desiredFrameRate());

    // In a real deferred shading program, we would render early z, then use a scissor test to 
    // avoid the cost of rendering all of the other G-buffers outside of the visible frame
    Surface::renderIntoGBuffer(rd, surface3D, m_gbuffer);

    const double width  = m_gbuffer->width();
    const double height = m_gbuffer->height();
    const double z_f    = defaultCamera.farPlaneZ();
    const double z_n    = defaultCamera.nearPlaneZ();

    const Vector3& clipInfo = (z_f == -inf()) ? Vector3(float(z_n), -1.0f, 1.0f) : Vector3(float(z_n * z_f),  float(z_n - z_f),  float(z_f));

    // Projection matrix
    Matrix4 P;
    defaultCamera.getProjectUnitMatrix(m_gbuffer->rect2DBounds(), P);
    const Vector4 projInfo
        (float(-2.0 / (width * P[0][0])), 
         float(-2.0 / (height * P[1][1])),
         float((1.0 - (double)P[0][2]) / P[0][0]), 
         float((1.0 + (double)P[1][2]) / P[1][1]));


    rd->push2D(m_aoResultFramebuffer); {
        m_profiler.beginGFX("AO");
        m_SAO->compute(rd, m_gbuffer->texture(GBuffer::Field::DEPTH_AND_STENCIL), defaultCamera, COMPUTE_GUARD_BAND);
        m_profiler.endGFX();
    } rd->pop2D();

    rd->push2D(); {
        Shader::ArgList& args = m_deferredShader->args;
        args.set("aoBuffer",                  m_aoBuffer);
        args.set("environmentMapTexture",     m_useEnvironmentMap ? m_scene->lighting()->environmentMapTexture : Texture::whiteCube());
        args.set("environmentMapConstant",    m_useEnvironmentMap ? m_scene->lighting()->environmentMapConstant : 0.9f);
        args.set("useTexture",                m_useTexture);
        args.set("useAO",                     m_useAO);
        args.set("useEnvironmentMap",         m_useEnvironmentMap);
        args.set("aoIntensity",               m_aoIntensity);
        args.set("offset",                    Vector2int16(COMPUTE_GUARD_BAND, COMPUTE_GUARD_BAND));
        m_gbuffer->bindReadUniforms(args);
        rd->applyRect(m_deferredShader);
    } rd->pop2D();


    if (m_showWireframe) {
        Surface::renderWireframe(rd, surface3D);
    }

    //////////////////////////////////////////////////////
    // Sample immediate-mode rendering code
    rd->enableLighting();
    for (int i = 0; i < m_scene->lighting()->lightArray.size(); ++i) {
        rd->setLight(i, m_scene->lighting()->lightArray[i]);
    }
    rd->setAmbientLightColor(Color3::white() * 0.5f);

    if (m_showAxes) {
        Draw::axes(Point3(0, 0, 0), rd);
    }    

    if (m_showLightSources) {
        Draw::lighting(m_scene->lighting(), rd);
    }

    // Call to make the GApp show the output of debugDraw
    drawDebugShapes();
    m_profiler.nextFrame();

    if (m_profiler.enabled()) {
        const float t = m_profiler.gfxTime("AO") / units::milliseconds();
        // screenPrintf("AO: %5.2f ms\n", t);
        m_perfLabel->setCaption(GuiText(format("%5.2f ms", t), m_perfFont, 18.0f));
    }
}


void App::onGraphics2D(RenderDevice* rd, Array<Surface2D::Ref>& posed2D) {
    // Render 2D objects like Widgets.  These do not receive tone mapping or gamma correction
    Surface2D::sortAndRender(rd, posed2D);
}


void App::endProgram() {
    m_endProgram = true;
}


void App::saveScene() {
    // Called when the "save" button is pressed
    if (m_scene.notNull()) {
        Any a = m_scene->toAny();
        const std::string& filename = a.source().filename;
        if (filename != "") {
            a.save(filename);
            debugPrintf("Saved %s\n", filename.c_str());
        } else {
            debugPrintf("Could not save: empty filename");
        }
    }
}
