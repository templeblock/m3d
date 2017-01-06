/*
* Copyright (C) 2017 Tracy Ma
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "RendererVulkan.hpp"
#include "CommandBuffer.hpp"
#include "File.hpp"
#include "Matrix.h"
#include "Pipeline.hpp"
#include "Scene.hpp"
#include "VulkanHelper.hpp"
#include "VulkanSwapchain.hpp"
#include "vulkanTextureLoader.hpp"
#include <chrono>
#include <iostream>

#define VERTEX_BUFFER_BIND_ID 0
namespace m3d {

/*
	 * Utils
	 */
std::vector<const char*> RendererVulkan::getAvailableWSIExtensions()
{
    std::vector<const char*> extensions = { VK_KHR_SURFACE_EXTENSION_NAME };

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_MIR_KHR)
    extensions.push_back(VK_KHR_MIR_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif

    //#if defined(_DEBUG)
    //	extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    //#endif

    return extensions;
}

void RendererVulkan::createWin32Window(HINSTANCE hinstance, WNDPROC wndproc, uint32_t w, uint32_t h)
{
    const std::string class_name("RendererVulkanWindowClass");

    hinstance_ = hinstance;

    WNDCLASSEX win_class = {};
    win_class.cbSize = sizeof(WNDCLASSEX);
    win_class.style = CS_HREDRAW | CS_VREDRAW;
    win_class.lpfnWndProc = wndproc;
    win_class.hInstance = hinstance_;
    win_class.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    win_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    win_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    win_class.lpszMenuName = nullptr;
    win_class.lpszClassName = class_name.c_str();
    //win_class.hIconSm = LoadIcon(win_class.hInstance, MAKEINTRESOURCE(IDI_APPLICATION));
    RegisterClassEx(&win_class);

    const DWORD win_style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE | WS_OVERLAPPEDWINDOW;

    RECT win_rect = { 0, 0, w, h };
    AdjustWindowRect(&win_rect, win_style, false);

    hwnd_ = CreateWindowEx(WS_EX_APPWINDOW,
        class_name.c_str(),
        "RendererVulkan",
        win_style,
        0,
        0,
        win_rect.right - win_rect.left,
        win_rect.bottom - win_rect.top,
        nullptr,
        nullptr,
        hinstance_,
        nullptr);

    if (!hwnd_) {
        printf("could NOT create window!\n");
        fflush(stdout);
        exit(1);
    }

    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);

    // TODO:
    SetWindowLongPtr(hwnd_, GWLP_USERDATA, (LONG_PTR)this);
}

LRESULT RendererVulkan::handle_message(UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_CLOSE:
        inited = false;
        DestroyWindow(hwnd_);
        PostQuitMessage(0);
        break;
    case WM_PAINT:
        ValidateRect(hwnd_, NULL);
        break;
    case WM_KEYDOWN:

        break;
    case WM_KEYUP:

        break;
    case WM_RBUTTONDOWN:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
        break;
    case WM_MOUSEWHEEL: {

        break;
    }
    case WM_MOUSEMOVE:

        break;
    case WM_SIZE:
        OnWindowSizeChanged();
        break;
    case WM_ENTERSIZEMOVE:

        break;
    case WM_EXITSIZEMOVE:
        break;
    }
    return 0;
}

/*
	 * Setup Vulkan
	 */
bool RendererVulkan::CreateInstance()
{
    // Use validation layers if this is a debug build, and use WSI extensions regardless
    std::vector<const char*> extensions = getAvailableWSIExtensions();

    //#if defined(_DEBUG)
    //	std::vector<const char*> layers;
    //    layers.push_back("VK_LAYER_LUNARG_standard_validation");
    //#endif

    // vk::ApplicationInfo allows the programmer to specifiy some basic information about the
    // program, which can be useful for layers and tools to provide more debug information.
    vk::ApplicationInfo appInfo = vk::ApplicationInfo()
                                      .setPApplicationName("m3d example")
                                      .setApplicationVersion(1)
                                      .setPEngineName("m3d")
                                      .setEngineVersion(1)
                                      .setApiVersion(VK_API_VERSION_1_0);

    // vk::InstanceCreateInfo is where the programmer specifies the layers and/or extensions that
    // are needed.
    vk::InstanceCreateInfo instInfo = vk::InstanceCreateInfo()
                                          .setFlags(vk::InstanceCreateFlags())
                                          .setPApplicationInfo(&appInfo)
                                          .setEnabledExtensionCount(static_cast<uint32_t>(extensions.size()))
                                          .setPpEnabledExtensionNames(extensions.data())
        //#if defined(_DEBUG)
        //                                          .setEnabledLayerCount(static_cast<uint32_t>(layers.size()))
        //                                          .setPpEnabledLayerNames(layers.data())
        //#endif
        ;

    // Create the Vulkan instance.
    try {
        instance = vk::createInstance(instInfo);
    } catch (const std::exception& e) {
        std::cout << "Could not create a Vulkan instance: " << e.what() << std::endl;
        return false;
    }
    return true;
}

bool RendererVulkan::CreateDevice()
{
    // Create physical device
    std::vector<vk::PhysicalDevice> physicalDevices;

    try {
        physicalDevices = instance.enumeratePhysicalDevices();
    } catch (const std::exception& e) {
        std::cout << "Failed to create physical device: " << e.what() << std::endl;
        instance.destroy();
        return 1;
    }

    physicalDevice = physicalDevices[0];
    vk::PhysicalDeviceFeatures deviceFeatures;
    deviceFeatures = physicalDevice.getFeatures();

    // TODO:
    // check queue family from a physical device support swapchain
    //physicalDevice.getSurfaceSupportKHR();

    // Find a queue that supports graphics operations
    graphicsQueueIndex = vkhelper::findQueue(physicalDevice, vk::QueueFlagBits::eGraphics);
    std::array<float, 1> queuePriorities = { 0.0f };
    vk::DeviceQueueCreateInfo queueCreateInfo;
    queueCreateInfo.queueFamilyIndex = graphicsQueueIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = queuePriorities.data();

    std::vector<const char*> enabledExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    vk::DeviceCreateInfo deviceCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    // enable the debug marker extension if it is present (likely meaning a debugging tool is present)
    if (vkhelper::checkDeviceExtensionPresent(physicalDevice, VK_EXT_DEBUG_MARKER_EXTENSION_NAME)) {
        enabledExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
    }
    if (enabledExtensions.size() > 0) {
        deviceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
        deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
    }

    // Create Vulkan Device
    device = physicalDevice.createDevice(deviceCreateInfo);

    swapChain.connect(instance, physicalDevice, device);

    queue = device.getQueue(graphicsQueueIndex, 0);

    // SubmitInfo
    vk::SemaphoreCreateInfo semaphoreCreateInfo;
    presentComplete = device.createSemaphore(semaphoreCreateInfo);
    renderComplete = device.createSemaphore(semaphoreCreateInfo);

    //submitInfo = vkTools::initializers::submitInfo();
    submitInfo.pWaitDstStageMask = &submitPipelineStages;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &presentComplete;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderComplete;

    return true;
}

/*************** Swapchain ****************/
void RendererVulkan::CreateSwapChain()
{
    swapChain.initSurface(hinstance_, hwnd_);
    swapChain.create(&width, &height, false);
}

void RendererVulkan::CreatePipelineCache()
{
    //vk::PipelineCacheCreateInfo pipelineCacheCreateInfo = {};
    //pipelineCache = device.createPipelineCache(pipelineCacheCreateInfo);
}

void RendererVulkan::InitCommon()
{
    CreateSwapChain();
    CreateCommandPool();
	commandBuffer = new CommandBuffer(device, physicalDevice, queue, swapChain);

	pipeLine = new Pipeline(device, physicalDevice);
}

//void RendererVulkan::SetupVertexInputs()
//{
//
//}

bool RendererVulkan::Init(Scene* scene)
{
    if (!CreateInstance()) {
        return false;
    }

    if (!CreateDevice()) {
        return false;
    }

    this->scene = scene;

    InitCommon();

    // spec init
    {
        CreateVertices();
        SetupVertexInputs();
        CreateUniformBuffers();
        CreatePipelineLayout();
        CreatePipeline();
        CreateDescriptorPool();
        CreateDescriptorSet();
        BuildCommandBuffers();
    }

    // TODO:
    //OnWindowSizeChanged();

    return true;
}

bool RendererVulkan::CreateCommandPool()
{
    vk::CommandPoolCreateInfo cmdPoolInfo;
    cmdPoolInfo.queueFamilyIndex = swapChain.queueNodeIndex;
    cmdPoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    cmdPool = device.createCommandPool(cmdPoolInfo);
    return true;
}

bool RendererVulkan::OnWindowSizeChanged()
{
    if (!inited) {
        return false;
    }
    inited = false;

    // Recreate swapChain
    CreateSwapChain();
    // Recreate framebuffers
    device.destroyImageView(depthStencil.view);
    device.destroyImage(depthStencil.image);
    device.freeMemory(depthStencil.mem);
    CreateDepthStencil();

    for (uint32_t i = 0; i < framebuffers.size(); i++) {
        device.destroyFramebuffer(framebuffers[i]);
    }
    CreateFramebuffers();

    DestroyCommandBuffers();
    CreateCommandBuffers();
    BuildCommandBuffers();

    queue.waitIdle();
    device.waitIdle();

    inited = true;
}

void RendererVulkan::PrepareFrame()
{
    swapChain.acquireNextImage(presentComplete, &currentImage);
}

void RendererVulkan::SubmitFrame()
{
    swapChain.queuePresent(queue, currentImage, renderComplete);
    //queue.waitIdle();
}

/* Draw Loop */
void RendererVulkan::Draw()
{
    PrepareFrame();

    //device.waitForFences(1, &vk::Fence()/*&waitFences[currentImage]*/, true, UINT64_MAX);
    //device.resetFences(1, &vk::Fence() /*&waitFences[currentImage]*/);

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffers[currentImage];
    queue.submit(submitInfo, vk::Fence() /*waitFences[currentImage]*/);

    SubmitFrame();
}

void RendererVulkan::DrawLoop()
{
    while (1) {
        auto tStart = std::chrono::high_resolution_clock::now();
        Draw();
        frameCounter++;

        auto tEnd = std::chrono::high_resolution_clock::now();
        double tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        //frameTimer = (float)tDiff / 1000.0f;
        printf("%f\n", tDiff);
    }
    device.waitIdle();
}

RendererVulkan::~RendererVulkan()
{
	delete pipeLine;
	delete commandBuffer;

    // TODO: destroy texture, Mesh resources
}
}