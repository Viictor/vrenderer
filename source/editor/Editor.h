#pragma once

#include <donut/app/imgui_renderer.h>

namespace vRenderer
{
	struct UIData
	{
		bool m_ShaderReoladRequested = false;
		// Terrain params
		bool m_RenderTerrain = true;
		bool m_Wireframe = false;
		bool m_LockView = false;
		float m_MaxHeight = 10.0f;
		uint32_t m_NumChunks = 0;

		bool m_ProfilerOpen = true;
		bool m_FileOpen = false;
		float m_ProfilerWindowHeight = 0.0f;

		float m_AmbientIntensity = 0.01f;
	};

	class Editor;
	struct EditorWindowCallback
	{
		explicit EditorWindowCallback(const std::function<void(Editor&)>& callback)
			: m_Callback(callback)
		{}

		std::function<void(Editor&)> m_Callback;
	};

	class Editor : public donut::app::ImGui_Renderer
	{
	private:

		UIData m_UIData;

		ImFont* m_FontOpenSans = nullptr;
		ImFont* m_FontDroidMono = nullptr;

		std::list<EditorWindowCallback*> m_EditorWindows;

	public:
		Editor(donut::app::DeviceManager* deviceManager, std::shared_ptr<donut::vfs::IFileSystem> rootFS);

		void Render(nvrhi::IFramebuffer* framebuffer) override;

		void buildUI() override;

		void AddEditorWindow(EditorWindowCallback* function);
		void RemoveEditorWindow(EditorWindowCallback* function);

		UIData& GetUIData() { return m_UIData; }
		const UIData& GetUIData() const { return m_UIData; }
	};
}