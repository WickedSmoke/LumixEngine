#include "mainwindow.h"
#include <QApplication>
#include <qdir.h>
#include "core/log.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/world_editor.h"
#include "editor/gizmo.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "graphics/irender_device.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "physics/physics_scene.h"
#include "physics/physics_system.h"
#include "sceneview.h"
#include "gameview.h"
#include "wgl_render_device.h"
#include "materialmanager.h"

class App
{
	public:
		App()
		{
			m_game_render_device = NULL;
			m_edit_render_device = NULL;
			m_qt_app = NULL;
			m_main_window = NULL;
			m_world_editor = NULL;
		}

		~App()
		{
			delete m_main_window;
			delete m_qt_app;
			Lumix::WorldEditor::destroy(m_world_editor);
		}

		void onUniverseCreated()
		{
			m_edit_render_device->getPipeline().setScene(m_world_editor->getEngine().getRenderScene()); 
			m_game_render_device->getPipeline().setScene(m_world_editor->getEngine().getRenderScene()); 
		}

		void onUniverseDestroyed()
		{
			if(m_edit_render_device)
			{
				m_edit_render_device->getPipeline().setScene(NULL); 
			}
			if(m_game_render_device)
			{
				m_game_render_device->getPipeline().setScene(NULL); 
			}
			
		}

		HGLRC createGLContext(HWND hwnd[], int count)
		{

			ASSERT(count > 0);
			HDC hdc;
			hdc = GetDC(hwnd[0]);
			ASSERT(hdc != NULL);
			if (hdc == NULL)
			{
				Lumix::g_log_error.log("renderer") << "Could not get the device context";
				return NULL;
			}
			PIXELFORMATDESCRIPTOR pfd = 
			{ 
				sizeof(PIXELFORMATDESCRIPTOR),  //  size of this pfd  
				1,                     // version number  
				PFD_DRAW_TO_WINDOW |   // support window  
				PFD_SUPPORT_OPENGL |   // support OpenGL  
				PFD_DOUBLEBUFFER,      // double buffered  
				PFD_TYPE_RGBA,         // RGBA type  
				24,                    // 24-bit color depth  
				0, 0, 0, 0, 0, 0,      // color bits ignored  
				0,                     // no alpha buffer  
				0,                     // shift bit ignored  
				0,                     // no accumulation buffer  
				0, 0, 0, 0,            // accum bits ignored  
				32,                    // 32-bit z-buffer      
				0,                     // no stencil buffer  
				0,                     // no auxiliary buffer  
				PFD_MAIN_PLANE,        // main layer  
				0,                     // reserved  
				0, 0, 0                // layer masks ignored  
			}; 
			int pixelformat = ChoosePixelFormat(hdc, &pfd);
			if (pixelformat == 0)
			{
				ASSERT(false);
				Lumix::g_log_error.log("renderer") << "Could not choose a pixel format";
				return NULL;
			}
			BOOL success = SetPixelFormat(hdc, pixelformat, &pfd);
			if (success == FALSE)
			{
				ASSERT(false);
				Lumix::g_log_error.log("renderer") << "Could not set a pixel format";
				return NULL;
			}
			for (int i = 1; i < count; ++i)
			{
				if (hwnd[i])
				{
					HDC hdc2 = GetDC(hwnd[i]);
					if (hdc2 == NULL)
					{
						ASSERT(false);
						Lumix::g_log_error.log("renderer") << "Could not get the device context";
						return NULL;
					}
					BOOL success = SetPixelFormat(hdc2, pixelformat, &pfd);
					if (success == FALSE)
					{
						ASSERT(false);
						Lumix::g_log_error.log("renderer") << "Could not set a pixel format";
						return NULL;
					}
				}
			}
			HGLRC hglrc = wglCreateContext(hdc);
			if (hglrc == NULL)
			{
				ASSERT(false);
				Lumix::g_log_error.log("renderer") << "Could not create an opengl context";
				return NULL;
			}
			success = wglMakeCurrent(hdc, hglrc);
			if (success == FALSE)
			{
				ASSERT(false);
				Lumix::g_log_error.log("renderer") << "Could not make the opengl context current rendering context";
				return NULL;
			}
			return hglrc;
		}	

		void renderPhysics()
		{
			Lumix::PhysicsSystem* system = static_cast<Lumix::PhysicsSystem*>(m_world_editor->getEngine().getPluginManager().getPlugin("physics"));
			if(system && system->getScene())
			{
				system->getScene()->render();
			}
		}

		void init(int argc, char* argv[])
		{
			m_qt_app = new QApplication(argc, argv);
			QFile file("editor/stylesheet.qss");
			file.open(QFile::ReadOnly);
			m_qt_app->setStyleSheet(QLatin1String(file.readAll()));

			m_main_window = new MainWindow();
			m_main_window->show();

			HWND hwnd = (HWND)m_main_window->getSceneView()->getViewWidget()->winId();
			HWND game_hwnd = (HWND)m_main_window->getGameView()->getContentWidget()->winId();
			HWND hwnds[] = {hwnd, game_hwnd};
			HGLRC hglrc = createGLContext(hwnds, 2);

			m_world_editor = Lumix::WorldEditor::create(QDir::currentPath().toLocal8Bit().data());
			ASSERT(m_world_editor);
			m_world_editor->tick();

			m_main_window->setWorldEditor(*m_world_editor);
			m_main_window->getSceneView()->setWorldEditor(m_world_editor);

			m_edit_render_device = new WGLRenderDevice(m_world_editor->getEngine(), "pipelines/main.json");
			m_edit_render_device->m_hdc = GetDC(hwnd);
			m_edit_render_device->m_opengl_context = hglrc;
			m_edit_render_device->getPipeline().setScene(m_world_editor->getEngine().getRenderScene()); /// TODO manage scene properly
			m_world_editor->setEditViewRenderDevice(*m_edit_render_device);
			m_edit_render_device->getPipeline().addCustomCommandHandler("render_physics").bind<App, &App::renderPhysics>(this);

			m_game_render_device = new	WGLRenderDevice(m_world_editor->getEngine(), "pipelines/game_view.json");
			m_game_render_device->m_hdc = GetDC(game_hwnd);
			m_game_render_device->m_opengl_context = hglrc;
			m_game_render_device->getPipeline().setScene(m_world_editor->getEngine().getRenderScene()); /// TODO manage scene properly
			m_world_editor->getEngine().getRenderer().setRenderDevice(*m_game_render_device);

			m_world_editor->universeCreated().bind<App, &App::onUniverseCreated>(this);
			m_world_editor->universeDestroyed().bind<App, &App::onUniverseDestroyed>(this);

			m_main_window->getSceneView()->setPipeline(m_edit_render_device->getPipeline());
			m_main_window->getGameView()->setPipeline(m_game_render_device->getPipeline());
		}

		void shutdown()
		{
			delete m_game_render_device;
			m_game_render_device = NULL;
			delete m_edit_render_device;
			m_edit_render_device = NULL;
		}

		void renderEditView()
		{
			PROFILE_FUNCTION();
			m_edit_render_device->beginFrame();
			m_world_editor->render(*m_edit_render_device);
			m_world_editor->renderIcons(*m_edit_render_device);
			m_world_editor->getGizmo().updateScale(m_world_editor->getEditCamera());
			m_world_editor->getGizmo().render(m_world_editor->getEngine().getRenderer(), *m_edit_render_device);
			m_edit_render_device->endFrame();

			m_main_window->getMaterialManager()->updatePreview();
		}

		void handleEvents()
		{
			PROFILE_FUNCTION();
			{
				PROFILE_BLOCK("qt::processEvents");
				m_qt_app->processEvents();
			}
			BYTE keys[256];
			GetKeyboardState(keys);
			if (m_main_window->getSceneView()->getViewWidget()->hasFocus())
			{
				/// TODO refactor
				if(keys[VK_CONTROL] >> 7 == 0)
				{
					float speed = m_main_window->getSceneView()->getNavivationSpeed();
					if (keys[VK_LSHIFT] >> 7)
					{
						speed *= 10;
					}
					if (keys['W'] >> 7)
					{
						m_world_editor->navigate(1, 0, speed);
					}
					else if (keys['S'] >> 7)
					{
						m_world_editor->navigate(-1, 0, speed);
					}
					if (keys['A'] >> 7)
					{
						m_world_editor->navigate(0, -1, speed);
					}
					else if (keys['D'] >> 7)
					{
						m_world_editor->navigate(0, 1, speed);
					}
				}
			}
		}

		void run()
		{
			while (m_main_window->isVisible())
			{
				{
					PROFILE_BLOCK("tick");
					renderEditView();
					m_world_editor->getEngine().getRenderer().renderGame();
					m_world_editor->tick();
					handleEvents();
				}
				Lumix::g_profiler.frame();
			}
		}

	private:
		WGLRenderDevice* m_edit_render_device;
		WGLRenderDevice* m_game_render_device;
		MainWindow* m_main_window;
		Lumix::WorldEditor* m_world_editor;
		QApplication* m_qt_app;
};

int main(int argc, char* argv[])
{
	App app;
	app.init(argc, argv);
	app.run();
	app.shutdown();
	return 0;
}
