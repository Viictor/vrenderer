#include "Editor.h"
#include "ImGuizmo.h"
#include "../profiler/Profiler.h"

#include "donut/app/UserInterfaceUtils.h"

using namespace vRenderer;

Editor::Editor(donut::app::DeviceManager* deviceManager, std::shared_ptr<donut::vfs::IFileSystem> rootFS, Renderer& renderer)
	: ImGui_Renderer(deviceManager)
	, m_Renderer(renderer)
{
	ImPlot::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;

	ImGuiIO& io = ImGui::GetIO();
	io.KeyMap[ImGuiKey_Space] = GLFW_KEY_SPACE;
}

Editor::~Editor()
{
	ImPlot::DestroyContext();
}

void Editor::Animate(float fElapsedTimeSeconds)
{
	m_ElapsedTime = fElapsedTimeSeconds;
	ImGui_Renderer::Animate(fElapsedTimeSeconds);
	ImGuizmo::BeginFrame();
	ImGuizmo::Enable(true);
}

void Editor::Render(nvrhi::IFramebuffer* framebuffer)
{
	PROFILE_CPU_SCOPE("ImGui_Renderer::Render");
	ImGui_Renderer::Render(framebuffer);
}

void Editor::buildUI()
{
	ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), 0);
	ImGui::Begin("Settings", 0, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_MenuBar);

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Menu"))
		{
			ImGui::MenuItem("Profiler", NULL, &GetUIData().m_ProfilerOpen);
			ImGui::MenuItem("Open", 0, &GetUIData().m_FileOpen);
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	ImGui::Text("Renderer: %s", GetDeviceManager()->GetRendererString());
	double frameTime = GetDeviceManager()->GetAverageFrameTimeSeconds();
	if (frameTime > 0.0)
		ImGui::Text("%.3f ms/frame (%.1f FPS)", frameTime * 1e3, 1.0 / frameTime);

	ImVec2 viewportSize = ImGui::GetMainViewport()->WorkSize;
	/*ImGui::SetNextWindowPos(ImVec2(380.f, 10.0f + GetUIData().m_ProfilerWindowHeight));
	ImGui::SetNextWindowSize(ImVec2(viewportSize.x - 390.0f, donut::math::max(290.0f, GetUIData().m_ProfilerWindowHeight)));
	ImGui::Begin("Frames", 0);*/
	static std::vector<float> frameTimes;
	static std::vector<float> frameTimesGPU;
	if (ImPlot::BeginPlot("Frame Times", ImVec2(-1, 0), ImPlotFlags_NoInputs | ImPlotFlags_NoTitle))
	{
		//Update FrameTimes
		frameTimes.push_back(m_ElapsedTime * 1e3);
		if (frameTimes.size() > 1500) frameTimes.erase(frameTimes.begin());

		ImPlot::SetupAxes("time", "ms", ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
		ImPlot::PlotLine("Frame Times", frameTimes.data(), static_cast<int>(frameTimes.size()), 1, 0, ImPlotLineFlags_Shaded);

		URange gpuRange = gGPUProfiler.GetFrameRange();
		for (const GPUProfiler::QueueInfo& queue : gGPUProfiler.GetQueues())
		{
			for (uint32 i = gpuRange.Begin; i < gpuRange.End; ++i)
			{
				Span<const GPUProfiler::EventData::Event> events = gGPUProfiler.GetEventsForQueue(queue, i);
				if (!events.empty())
				{
					frameTimesGPU.push_back(queue.TicksToMS(events[0].TicksEnd - events[0].TicksBegin));
					if (frameTimesGPU.size() > 1500) frameTimesGPU.erase(frameTimesGPU.begin());

				}
			}
		}
		ImPlot::PlotLine("GPU Frame Times", frameTimesGPU.data(), static_cast<int>(frameTimesGPU.size()), 1, 0, ImPlotLineFlags_Shaded);

		ImPlot::EndPlot();
	}
	//ImGui::End();

	if (GetUIData().m_ProfilerOpen)
	{
		ImGui::SetNextWindowPos(ImVec2(380.f, 10.f), 0);
		ImGui::SetNextWindowSize(ImVec2(viewportSize.x - 390.0f, donut::math::max(290.0f, GetUIData().m_ProfilerWindowHeight)));
		ImGui::Begin("Profiler", &GetUIData().m_ProfilerOpen, ImGuiWindowFlags_NoResize);
		DrawProfilerHUD(GetUIData().m_ProfilerWindowHeight);
		ImGui::End();
	}

	if (GetUIData().m_FileOpen)
	{
		std::string filename;
		bool result = donut::app::FileDialog(true, ".gltf", filename);
		GetUIData().m_FileOpen = false;

		if (result)
		{
			std::string token = filename.substr(filename.find_last_of("\\") + 1, filename.length());

			std::filesystem::path scenePath = "/media/gltfScenes";
			m_SceneFilesAvailable = donut::app::FindScenes(*m_Renderer.GetRootFs(), scenePath);
			std::string sceneName = donut::app::FindPreferredScene(m_SceneFilesAvailable, token);

			if (!sceneName.empty())
			{
				m_Renderer.BeginLoadingScene(m_Renderer.GetRootFs(), sceneName);
			}
		}
	}

	for (const auto& editorWindow : m_EditorWindows)
	{
		editorWindow->m_Callback();
	}

	ImGui::End();
}

void Editor::AddEditorWindow(EditorWindowCallback* function)
{
	m_EditorWindows.remove(function);
	m_EditorWindows.push_back(function);
}

void Editor::RemoveEditorWindow(EditorWindowCallback* function)
{
	m_EditorWindows.remove(function);
}
