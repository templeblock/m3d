/*
* Class wrapping access to the swap chain
* 
* A swap chain is a collection of framebuffers used for rendering and presentation to the windowing system
*
* Copyright (C) 2017 Tracy Ma
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <assert.h>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#endif

#include <vulkan/vulkan.hpp>

#ifdef __ANDROID__
#include "vulkanandroid.h"
#endif

namespace m3d {
typedef struct _SwapChainBuffers {
    vk::Image image;
    vk::ImageView view;
} SwapChainBuffer;

class VulkanSwapChain {
private:
    vk::Instance instance;
    vk::Device device;
    vk::PhysicalDevice physicalDevice;
    vk::SurfaceKHR surface;

public:
    vk::Format colorFormat;
    vk::ColorSpaceKHR colorSpace;
    /** @brief Handle to the current swap chain, required for recreation */
    vk::SwapchainKHR swapChain = {};

    std::vector<vk::Image> images;
    std::vector<SwapChainBuffer> buffers;
    // Index of the deteced graphics and presenting device queue
    /** @brief Queue family index of the detected graphics and presenting device queue */
    uint32_t queueNodeIndex = UINT32_MAX;

    // Creates an os specific surface
    /**
		* Create the surface object, an abstraction for the native platform window
		*
		* @pre Windows
		* @param platformHandle HINSTANCE of the window to create the surface for
		* @param platformWindow HWND of the window to create the surface for
		*
		* @pre Android
		* @param window A native platform window
		*
		* @pre Linux (XCB)
		* @param connection xcb connection to the X Server
		* @param window The xcb window to create the surface for
		* @note Targets other than XCB ar not yet supported
		*/
    void initSurface(
#ifdef _WIN32
        void* platformHandle, void* platformWindow
#else
#ifdef __ANDROID__
        ANativeWindow* window
#else
#ifdef _DIRECT2DISPLAY
        uint32_t width, uint32_t height
#else
        xcb_connection_t* connection, xcb_window_t window
#endif
#endif
#endif
        )
    {
//VkResult err;

// Create the os-specific surface
#ifdef _WIN32
        vk::Win32SurfaceCreateInfoKHR surfaceInfo = vk::Win32SurfaceCreateInfoKHR()
                                                        .setHinstance((HINSTANCE)platformHandle)
                                                        .setHwnd((HWND)platformWindow);
        surface = instance.createWin32SurfaceKHR(surfaceInfo);
#else
#ifdef __ANDROID__
        VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo = {};
        surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.window = window;
        err = vkCreateAndroidSurfaceKHR(instance, &surfaceCreateInfo, NULL, &surface);
#else
#if defined(_DIRECT2DISPLAY)
        createDirect2DisplaySurface(width, height);
#else
        VkXcbSurfaceCreateInfoKHR surfaceCreateInfo = {};
        surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.connection = connection;
        surfaceCreateInfo.window = window;
        err = vkCreateXcbSurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface);
#endif
#endif
#endif

        // Get available queue family properties
        std::vector<vk::QueueFamilyProperties> queueProps;
        queueProps = physicalDevice.getQueueFamilyProperties();
        assert(queueProps.size() > 0);

        // Iterate over each queue to learn whether it supports presenting:
        // Find a queue with present support
        // Will be used to present the swap chain images to the windowing system
        std::vector<vk::Bool32> supportsPresent(queueProps.size());
        for (uint32_t i = 0; i < queueProps.size(); i++) {
            supportsPresent[i] = physicalDevice.getSurfaceSupportKHR(i, surface);
        }

        // Search for a graphics and a present queue in the array of queue
        // families, try to find one that supports both
        uint32_t graphicsQueueNodeIndex = UINT32_MAX;
        uint32_t presentQueueNodeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < queueProps.size(); i++) {
            if (queueProps[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                if (graphicsQueueNodeIndex == UINT32_MAX) {
                    graphicsQueueNodeIndex = i;
                }

                if (supportsPresent[i] == VK_TRUE) {
                    graphicsQueueNodeIndex = i;
                    presentQueueNodeIndex = i;
                    break;
                }
            }
        }
        if (presentQueueNodeIndex == UINT32_MAX) {
            // If there's no queue that supports both present and graphics
            // try to find a separate present queue
            for (uint32_t i = 0; i < queueProps.size(); ++i) {
                if (supportsPresent[i] == VK_TRUE) {
                    presentQueueNodeIndex = i;
                    break;
                }
            }
        }

        // Exit if either a graphics or a presenting queue hasn't been found
        if (graphicsQueueNodeIndex == UINT32_MAX || presentQueueNodeIndex == UINT32_MAX) {
            assert("Could not find a graphics and/or presenting queue!");
        }

        // todo : Add support for separate graphics and presenting queue
        if (graphicsQueueNodeIndex != presentQueueNodeIndex) {
            assert("Separate graphics and presenting queues are not supported yet!");
        }

        queueNodeIndex = graphicsQueueNodeIndex;

        // Get list of supported surface formats
        std::vector<vk::SurfaceFormatKHR> surfaceFormats;
        surfaceFormats = physicalDevice.getSurfaceFormatsKHR(surface);
        assert(surfaceFormats.size() > 0);

        // If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
        // there is no preferered format, so we assume VK_FORMAT_B8G8R8A8_UNORM
        if ((surfaceFormats.size() == 1) && (surfaceFormats[0].format == vk::Format::eUndefined)) {
            colorFormat = vk::Format::eB8G8R8A8Unorm;
        } else {
            // Always select the first available color format
            // If you need a specific format (e.g. SRGB) you'd need to
            // iterate over the list of available surface format and
            // check for it's presence
            colorFormat = surfaceFormats[0].format;
        }
        colorSpace = surfaceFormats[0].colorSpace;
    }

    /**
		* Set instance, physical and logical device to use for the swapchain and get all required function pointers
		*
		* @param instance Vulkan instance to use
		* @param physicalDevice Physical device used to query properties and formats relevant to the swapchain
		* @param device Logical representation of the device to create the swapchain for
		*
		*/
    void connect(vk::Instance& instance, vk::PhysicalDevice& physicalDevice, vk::Device& device)
    {
        this->instance = instance;
        this->physicalDevice = physicalDevice;
        this->device = device;
    }

    /**
		* Create the swapchain and get it's images with given width and height
		*
		* @param width Pointer to the width of the swapchain (may be adjusted to fit the requirements of the swapchain)
		* @param height Pointer to the height of the swapchain (may be adjusted to fit the requirements of the swapchain)
		* @param vsync (Optional) Can be used to force vsync'd rendering (by using VK_PRESENT_MODE_FIFO_KHR as presentation mode)
		*/
    void create(uint32_t* width, uint32_t* height, bool vsync = false)
    {
        //VkResult err;
        vk::SwapchainKHR oldSwapchain = swapChain;

        // Get physical device surface properties and formats
        vk::SurfaceCapabilitiesKHR surfCaps = {};
        surfCaps = physicalDevice.getSurfaceCapabilitiesKHR(surface);

        // Get available present modes
        std::vector<vk::PresentModeKHR> presentModes = {};
        presentModes = physicalDevice.getSurfacePresentModesKHR(surface);

        vk::Extent2D swapchainExtent = {};
        // If width (and height) equals the special value 0xFFFFFFFF, the size of the surface will be set by the swapchain
        if (surfCaps.currentExtent.width == (uint32_t)-1 || surfCaps.currentExtent.height == (uint32_t)-1) {
            // If the surface size is undefined, the size is set to
            // the size of the images requested.
            swapchainExtent.width = *width;
            swapchainExtent.height = *height;
        } else {
            // If the surface size is defined, the swap chain size must match
            swapchainExtent = surfCaps.currentExtent;
            *width = surfCaps.currentExtent.width;
            *height = surfCaps.currentExtent.height;
        }

        // Select a present mode for the swapchain

        // The VK_PRESENT_MODE_FIFO_KHR mode must always be present as per spec
        // This mode waits for the vertical blank ("v-sync")
        vk::PresentModeKHR swapchainPresentMode = vk::PresentModeKHR::eFifo;

        // If v-sync is not requested, try to find a mailbox mode
        // It's the lowest latency non-tearing present mode available
        if (!vsync) {
            for (size_t i = 0; i < presentModes.size(); i++) {
                if (presentModes[i] == vk::PresentModeKHR::eMailbox) {
                    swapchainPresentMode = vk::PresentModeKHR::eMailbox;
                    break;
                }
                if ((swapchainPresentMode != vk::PresentModeKHR::eMailbox) && (presentModes[i] == vk::PresentModeKHR::eImmediate)) {
                    swapchainPresentMode = vk::PresentModeKHR::eImmediate;
                }
            }
        }

        // Determine the number of images
        uint32_t desiredNumberOfSwapchainImages = surfCaps.minImageCount + 1;
        if ((surfCaps.maxImageCount > 0) && (desiredNumberOfSwapchainImages > surfCaps.maxImageCount)) {
            desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
        }

        // Find the transformation of the surface
        vk::SurfaceTransformFlagBitsKHR preTransform;
        if (surfCaps.supportedTransforms == vk::SurfaceTransformFlagBitsKHR::eIdentity) {
            // We prefer a non-rotated transform
            preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
        } else {
            preTransform = surfCaps.currentTransform;
        }

        vk::SwapchainCreateInfoKHR swapchainCI = {};
        //swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainCI.pNext = NULL;
        swapchainCI.surface = surface;
        swapchainCI.minImageCount = desiredNumberOfSwapchainImages;
        swapchainCI.imageFormat = colorFormat;
        swapchainCI.imageColorSpace = colorSpace;
        swapchainCI.imageExtent = { swapchainExtent.width, swapchainExtent.height };
        swapchainCI.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
        swapchainCI.preTransform = preTransform;
        swapchainCI.imageArrayLayers = 1;
        swapchainCI.imageSharingMode = vk::SharingMode::eExclusive;
        swapchainCI.queueFamilyIndexCount = 0;
        swapchainCI.pQueueFamilyIndices = NULL;
        swapchainCI.presentMode = swapchainPresentMode;
        swapchainCI.oldSwapchain = oldSwapchain;
        // Setting clipped to VK_TRUE allows the implementation to discard rendering outside of the surface area
        swapchainCI.clipped = VK_TRUE;
        swapchainCI.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;

        // Set additional usage flag for blitting from the swapchain images if supported
        vk::FormatProperties formatProps;
        //vkGetPhysicalDeviceFormatProperties(physicalDevice, colorFormat, &formatProps);
        formatProps = physicalDevice.getFormatProperties(colorFormat);
        if (formatProps.optimalTilingFeatures == vk::FormatFeatureFlagBits::eBlitDst) {
            swapchainCI.imageUsage |= vk::ImageUsageFlagBits::eTransferSrc;
        }

        //err = fpCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapChain);
        swapChain = device.createSwapchainKHR(swapchainCI);
        //assert(!err);

        // If an existing swap chain is re-created, destroy the old swap chain
        // This also cleans up all the presentable images
        if (oldSwapchain) {
            for (uint32_t i = 0; i < images.size(); i++) {
                //vkDestroyImageView(device, buffers[i].view, nullptr);
                device.destroyImageView(buffers[i].view);
            }
            //fpDestroySwapchainKHR(device, oldSwapchain, nullptr);
            device.destroySwapchainKHR(oldSwapchain);
        }

        // Get the swap chain images
        images = device.getSwapchainImagesKHR(swapChain);

        // Get the swap chain buffers containing the image and imageview
        buffers.resize(images.size());
        for (uint32_t i = 0; i < images.size(); i++) {
            vk::ImageViewCreateInfo colorAttachmentView = {};
            //colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            colorAttachmentView.pNext = NULL;
            colorAttachmentView.format = colorFormat;
            colorAttachmentView.components = {
                VK_COMPONENT_SWIZZLE_R,
                VK_COMPONENT_SWIZZLE_G,
                VK_COMPONENT_SWIZZLE_B,
                VK_COMPONENT_SWIZZLE_A
            };
            colorAttachmentView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            colorAttachmentView.subresourceRange.baseMipLevel = 0;
            colorAttachmentView.subresourceRange.levelCount = 1;
            colorAttachmentView.subresourceRange.baseArrayLayer = 0;
            colorAttachmentView.subresourceRange.layerCount = 1;
            colorAttachmentView.viewType = vk::ImageViewType::e2D;
            //colorAttachmentView.flags = 0;

            buffers[i].image = images[i];

            colorAttachmentView.image = buffers[i].image;

            buffers[i].view = device.createImageView(colorAttachmentView);
            //assert(!err);
        }
    }

    /**
		* Acquires the next image in the swap chain
		*
		* @param presentCompleteSemaphore (Optional) Semaphore that is signaled when the image is ready for use
		* @param imageIndex Pointer to the image index that will be increased if the next image could be acquired
		*
		* @note The function will always wait until the next image has been acquired by setting timeout to UINT64_MAX
		*
		* @return VkResult of the image acquisition
		*/
    vk::Result acquireNextImage(vk::Semaphore presentCompleteSemaphore, uint32_t* imageIndex)
    {
        // By setting timeout to UINT64_MAX we will always wait until the next image has been acquired or an actual error is thrown
        // With that we don't have to handle VK_NOT_READY
        return device.acquireNextImageKHR(swapChain, UINT64_MAX, presentCompleteSemaphore, vk::Fence(), imageIndex);
    }

    /**
		* Queue an image for presentation
		*
		* @param queue Presentation queue for presenting the image
		* @param imageIndex Index of the swapchain image to queue for presentation
		* @param waitSemaphore (Optional) Semaphore that is waited on before the image is presented (only used if != VK_NULL_HANDLE)
		*
		* @return VkResult of the queue presentation
		*/
    vk::Result queuePresent(vk::Queue queue, uint32_t imageIndex, vk::Semaphore waitSemaphore)
    {
        vk::PresentInfoKHR presentInfo = {};
        //presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pNext = NULL;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapChain;
        presentInfo.pImageIndices = &imageIndex;
        // Check if a wait semaphore has been specified to wait for before presenting the image
        if (waitSemaphore) {
            presentInfo.pWaitSemaphores = &waitSemaphore;
            presentInfo.waitSemaphoreCount = 1;
        }
        return queue.presentKHR(presentInfo);
    }

    /**
		* Destroy and free Vulkan resources used for the swapchain
		*/
    void cleanup()
    {
        if (swapChain) {
            for (uint32_t i = 0; i < images.size(); i++) {
                device.destroyImageView(buffers[i].view, nullptr);
            }
        }
        if (surface) {
            device.destroySwapchainKHR(swapChain, nullptr);
            instance.destroySurfaceKHR(surface, nullptr);
        }
        //surface = VK_NULL_HANDLE;
        //swapChain = VK_NULL_HANDLE;
    }

#if defined(_DIRECT2DISPLAY)
    /**
		* Create direct to display surface
		*/
    void createDirect2DisplaySurface(uint32_t width, uint32_t height)
    {
        uint32_t displayPropertyCount;

        // Get display property
        vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &displayPropertyCount, NULL);
        VkDisplayPropertiesKHR* pDisplayProperties = new VkDisplayPropertiesKHR[displayPropertyCount];
        vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &displayPropertyCount, pDisplayProperties);

        // Get plane property
        uint32_t planePropertyCount;
        vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physicalDevice, &planePropertyCount, NULL);
        VkDisplayPlanePropertiesKHR* pPlaneProperties = new VkDisplayPlanePropertiesKHR[planePropertyCount];
        vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physicalDevice, &planePropertyCount, pPlaneProperties);

        VkDisplayKHR display = VK_NULL_HANDLE;
        VkDisplayModeKHR displayMode;
        VkDisplayModePropertiesKHR* pModeProperties;
        bool foundMode = false;

        for (uint32_t i = 0; i < displayPropertyCount; ++i) {
            display = pDisplayProperties[i].display;
            uint32_t modeCount;
            vkGetDisplayModePropertiesKHR(physicalDevice, display, &modeCount, NULL);
            pModeProperties = new VkDisplayModePropertiesKHR[modeCount];
            vkGetDisplayModePropertiesKHR(physicalDevice, display, &modeCount, pModeProperties);

            for (uint32_t j = 0; j < modeCount; ++j) {
                const VkDisplayModePropertiesKHR* mode = &pModeProperties[j];

                if (mode->parameters.visibleRegion.width == width && mode->parameters.visibleRegion.height == height) {
                    displayMode = mode->displayMode;
                    foundMode = true;
                    break;
                }
            }
            if (foundMode) {
                break;
            }
            delete[] pModeProperties;
        }

        if (!foundMode) {
            vkTools::exitFatal("Can't find a display and a display mode!", "Fatal error");
            return;
        }

        // Search for a best plane we can use
        uint32_t bestPlaneIndex = UINT32_MAX;
        VkDisplayKHR* pDisplays = NULL;
        for (uint32_t i = 0; i < planePropertyCount; i++) {
            uint32_t planeIndex = i;
            uint32_t displayCount;
            vkGetDisplayPlaneSupportedDisplaysKHR(physicalDevice, planeIndex, &displayCount, NULL);
            if (pDisplays) {
                delete[] pDisplays;
            }
            pDisplays = new VkDisplayKHR[displayCount];
            vkGetDisplayPlaneSupportedDisplaysKHR(physicalDevice, planeIndex, &displayCount, pDisplays);

            // Find a display that matches the current plane
            bestPlaneIndex = UINT32_MAX;
            for (uint32_t j = 0; j < displayCount; j++) {
                if (display == pDisplays[j]) {
                    bestPlaneIndex = i;
                    break;
                }
            }
            if (bestPlaneIndex != UINT32_MAX) {
                break;
            }
        }

        if (bestPlaneIndex == UINT32_MAX) {
            vkTools::exitFatal("Can't find a plane for displaying!", "Fatal error");
            return;
        }

        VkDisplayPlaneCapabilitiesKHR planeCap;
        vkGetDisplayPlaneCapabilitiesKHR(physicalDevice, displayMode, bestPlaneIndex, &planeCap);
        VkDisplayPlaneAlphaFlagBitsKHR alphaMode;

        if (planeCap.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR) {
            alphaMode = VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR;
        } else if (planeCap.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR) {

            alphaMode = VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR;
        } else {
            alphaMode = VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR;
        }

        VkDisplaySurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.pNext = NULL;
        surfaceInfo.flags = 0;
        surfaceInfo.displayMode = displayMode;
        surfaceInfo.planeIndex = bestPlaneIndex;
        surfaceInfo.planeStackIndex = pPlaneProperties[bestPlaneIndex].currentStackIndex;
        surfaceInfo.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        surfaceInfo.globalAlpha = 1.0;
        surfaceInfo.alphaMode = alphaMode;
        surfaceInfo.imageExtent.width = width;
        surfaceInfo.imageExtent.height = height;

        VkResult result = vkCreateDisplayPlaneSurfaceKHR(instance, &surfaceInfo, NULL, &surface);
        if (result != VK_SUCCESS) {
            vkTools::exitFatal("Failed to create surface!", "Fatal error");
        }

        delete[] pDisplays;
        delete[] pModeProperties;
        delete[] pDisplayProperties;
        delete[] pPlaneProperties;
    }
#endif
};
}// End of namespace m3d
