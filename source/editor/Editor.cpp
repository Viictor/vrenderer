#include "Editor.h"
#include "../profiler/Profiler.h"

using namespace vRenderer;

Editor::Editor(donut::app::DeviceManager* deviceManager, std::shared_ptr<donut::vfs::IFileSystem> rootFS)
	: ImGui_Renderer(deviceManager)
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
	for (const auto& editorWindow : m_EditorWindows)
	{
		editorWindow->m_Callback(*this);
	}
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
