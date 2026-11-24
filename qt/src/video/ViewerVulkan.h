#pragma once

#include <QVulkanInstance>
#include <QVulkanWindow>

#include "video/ViewerInterface.h"

class ViewerVulkan;

class VulkanWindow : public QVulkanWindow
{
	public:
		explicit VulkanWindow(ViewerVulkan *owner);

		QVulkanWindowRenderer *createRenderer(void) override;

	private:
		ViewerVulkan *viewer;
};

// Vulkan viewer. QVulkanWindow provides the swap chain, the frame is
// uploaded to a texture and drawn as a textured quad.
class ViewerVulkan : public ViewerBase
{
	public:
		ViewerVulkan(const Options &options, QWidget *parent);
		~ViewerVulkan(void) override;

		Driver driver(void) const override { return DriverVulkan; }
		QString driverInfo(void) const override;
		QWidget *widget(void) override { return container; }
		bool init(void) override;
		void queueRedraw(void) override;

	protected:
		void optionsChanged(void) override { queueRedraw(); }

	private:
		friend class VulkanRenderer;

		QWidget *parentWidget;
		// The instance has to outlive the window
		QVulkanInstance instance;
		VulkanWindow *window;
		QWidget *container;
};
