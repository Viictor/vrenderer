#include <donut/app/ApplicationBase.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <taskflow/taskflow.hpp>

#include "profiler/Profiler.h"
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
        const std::shared_ptr<vRenderer::Renderer> Renderer = std::make_shared<vRenderer::Renderer>(deviceManager, executor);
        deviceManager->AddRenderPassToFront(Renderer.get());

        deviceManager->RunMessageLoop();
        
        executor.wait_for_all();
    }

    gCPUProfiler.Shutdown();
    deviceManager->Shutdown();

    delete deviceManager;

    return 0;
}