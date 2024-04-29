#include "Editor.h"
#include "../profiler/Profiler.h"

#include "donut/app/UserInterfaceUtils.h"

using namespace vRenderer;

Editor::Editor(donut::app::DeviceManager* deviceManager, std::shared_ptr<donut::vfs::IFileSystem> rootFS, Renderer& renderer)
	: ImGui_Renderer(deviceManager)
	, m_Renderer(renderer)
{
	ImGui::GetIO().IniFilename = nullptr;

	ImGuiIO& io = ImGui::GetIO();
	io.KeyMap[ImGuiKey_Space] = GLFW_KEY_SPACE;
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

	if (GetUIData().m_ProfilerOpen)
	{
		ImVec2 viewportSize = ImGui::GetMainViewport()->WorkSize;
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
