#pragma once

#include <nvrhi/utils.h>
#include <nvrhi/common/misc.h>
#include <donut/app/DeviceManager.h>
#include <donut/app/imgui_renderer.h>
#include <donut/core/vfs/VFS.h>

using namespace donut::app;

struct UIData
{
	bool m_Wireframe = false;
	bool m_LockView = false;
};

class UIRenderer : public donut::app::ImGui_Renderer
{
private:

	nvrhi::CommandListHandle m_CommandList;
	ImFont* m_FontOpenSans = nullptr;
	ImFont* m_FontDroidMono = nullptr;

	UIData& m_UIData;

public:

	UIRenderer(donut::app::DeviceManager* deviceManager, std::shared_ptr<donut::vfs::IFileSystem> rootFS, UIData& uiData)
		: ImGui_Renderer(deviceManager)
		, m_UIData(uiData)
	{
		m_CommandList = GetDevice()->createCommandList();

		m_FontOpenSans = this->LoadFont(*rootFS, "/media/fonts/OpenSans/OpenSans-Regular.ttf", 17.f);
		m_FontDroidMono = this->LoadFont(*rootFS, "/media/fonts/DroidSans/DroidSans-Mono.ttf", 14.f);

		ImGui::GetIO().IniFilename = nullptr;
	}

	void UIRenderer::buildUI(void) override
	{
		ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), 0);
		ImGui::Begin("Settings", 0, ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::Text("Renderer: %s", GetDeviceManager()->GetRendererString());
		double frameTime = GetDeviceManager()->GetAverageFrameTimeSeconds();
		if (frameTime > 0.0)
			ImGui::Text("%.3f ms/frame (%.1f FPS)", frameTime * 1e3, 1.0 / frameTime);

		ImGui::Checkbox("Wireframe", &m_UIData.m_Wireframe);
		ImGui::Checkbox("Lock View", &m_UIData.m_LockView);

		ImGui::End();
	}
};