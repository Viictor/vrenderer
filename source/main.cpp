#include <ShellScalingApi.h>

#include <donut/app/ApplicationBase.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <taskflow/taskflow.hpp>

#include "profiler/Profiler.h"
#include "editor/Editor.h"
#include "Renderer.h"


using namespace donut;

#ifdef WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int __argc, const char** __argv)
#endif
{
	const nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);
    app::DeviceManager* deviceManager = app::DeviceManager::Create(api);

    app::DeviceCreationParameters deviceParams;
#ifdef _DEBUG
    deviceParams.enableDebugRuntime = true;
    deviceParams.enableNvrhiValidationLayer = true;
#endif
    app::InstanceParameters instanceParams;
    instanceParams.enableDebugRuntime = deviceParams.enableDebugRuntime;
    instanceParams.headlessDevice = false;

#ifdef _WINDOWS
    if (deviceParams.enablePerMonitorDPI)
    {
        // this needs to happen before glfwInit in order to override GLFW behavior
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    }
    else {
        SetProcessDpiAwareness(PROCESS_DPI_UNAWARE);
    }
#endif

    deviceManager->CreateInstance(instanceParams);

    std::vector<donut::app::AdapterInfo> adapters;
    deviceManager->EnumerateAdapters(adapters);

    for (int i = 0; i < adapters.size(); ++i) {
        if (adapters[i].name.find("NV") != std::string::npos)
        {
            deviceParams.adapterIndex = i;
            break;
        }
    }

	constexpr uint32_t numQueues = 1;
    //deviceParams.enableComputeQueue = true; numQueues++;
    //deviceParams.enableCopyQueue = true; numQueues++;

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, "vRenderer"))
    {
        log::fatal("Cannot initialize a graphics device with the requested parameters");
        return 1;
    }

    // Profiler Initialization
	constexpr uint32_t numFramesToProfile = 10;
    gCPUProfiler.Initialize(numFramesToProfile, 1024);
    gGPUProfiler.Initialize(deviceManager->GetDevice(), numQueues, numFramesToProfile, 2, 1024, 128, 32);

    {
        tf::Executor executor;
        const std::shared_ptr<vRenderer::Renderer> renderer = std::make_shared<vRenderer::Renderer>(deviceManager, executor);
        const std::shared_ptr<vRenderer::Editor> editor = std::make_unique<vRenderer::Editor>(deviceManager, renderer->GetRootFs(), *renderer);

		// Editor Windows
		editor->Init(renderer->GetShaderFactory());
        editor->AddEditorWindow(new vRenderer::EditorWindowCallback([renderer]()
            {
                renderer->RenderUI();
            }));

        // Render Passes
        deviceManager->AddRenderPassToBack(renderer.get());
        deviceManager->AddRenderPassToBack(editor.get());

        // Loading Cube.GLTF scene
		{
			std::filesystem::path scenePath = "/media/gltfScenes";
        	std::vector<std::string> sceneFilesAvailable = app::FindScenes(*renderer->GetRootFs(), scenePath);
        	std::string sceneName = app::FindPreferredScene(sceneFilesAvailable, "Cube.gltf");

        	renderer->BeginLoadingScene(renderer->GetRootFs(), sceneName);
		}
        deviceManager->RunMessageLoop();
        
        executor.wait_for_all();
    }

    gCPUProfiler.Shutdown();
    deviceManager->Shutdown();

    delete deviceManager;

    return 0;
}