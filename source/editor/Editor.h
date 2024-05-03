#pragma once

#include <donut/app/imgui_renderer.h>
#include "../Renderer.h"

namespace vRenderer
{
	struct UIData
	{
		bool m_ProfilerOpen = true;
		bool m_FileOpen = false;
		float m_ProfilerWindowHeight = 0.0f;
	};

	class Editor;
	struct EditorWindowCallback
	{
		explicit EditorWindowCallback(const std::function<void()>& callback)
			: m_Callback(callback)
		{}

		std::function<void()> m_Callback;
	};

	class Editor : public donut::app::ImGui_Renderer
	{
	private:

		Renderer& m_Renderer;
		UIData m_UIData;

		ImFont* m_FontOpenSans = nullptr;
		ImFont* m_FontDroidMono = nullptr;

		std::list<EditorWindowCallback*> m_EditorWindows;
		std::vector<std::string> m_SceneFilesAvailable;

	public:
		Editor(donut::app::DeviceManager* deviceManager, std::shared_ptr<donut::vfs::IFileSystem> rootFS, Renderer& renderer);

		void Animate(float fElapsedTimeSeconds) override;
		void Render(nvrhi::IFramebuffer* framebuffer) override;

		void buildUI() override;

		void AddEditorWindow(EditorWindowCallback* function);
		void RemoveEditorWindow(EditorWindowCallback* function);

		UIData& GetUIData() { return m_UIData; }
		const UIData& GetUIData() const { return m_UIData; }
	};
}