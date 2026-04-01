#pragma once
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <android/log.h>
#include <vector>
#include <string.h>
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_impl_android.h"
#include "../ImGui/imgui_impl_vulkan.h"
#include "../ByNameModding/external/include/dobby.h"
#include "../Misc/Utils.h"
#include "../Misc/ImGuiStuff.h"
#include "../Menu.h"

extern unsigned char Roboto_Regular[];

namespace VulkanHook {

    // =========================================================================
    // dlsym'd Vulkan function pointers (used for our own Vulkan calls so we
    // always bypass any dispatch table Unity may have patched).
    // =========================================================================
    static void* g_VulkanLib = nullptr;

    static PFN_vkGetPhysicalDeviceQueueFamilyProperties _vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
    static PFN_vkGetDeviceQueue                         _vkGetDeviceQueue                         = nullptr;
    static PFN_vkDeviceWaitIdle                         _vkDeviceWaitIdle                         = nullptr;
    static PFN_vkGetSwapchainImagesKHR                  _vkGetSwapchainImagesKHR                  = nullptr;
    static PFN_vkCreateRenderPass                       _vkCreateRenderPass                       = nullptr;
    static PFN_vkCreateDescriptorPool                   _vkCreateDescriptorPool                   = nullptr;
    static PFN_vkCreateCommandPool                      _vkCreateCommandPool                      = nullptr;
    static PFN_vkAllocateCommandBuffers                 _vkAllocateCommandBuffers                 = nullptr;
    static PFN_vkCreateImageView                        _vkCreateImageView                        = nullptr;
    static PFN_vkCreateFramebuffer                      _vkCreateFramebuffer                      = nullptr;
    static PFN_vkCreateFence                            _vkCreateFence                            = nullptr;
    static PFN_vkCreateSemaphore                        _vkCreateSemaphore                        = nullptr;
    static PFN_vkWaitForFences                          _vkWaitForFences                          = nullptr;
    static PFN_vkResetFences                            _vkResetFences                            = nullptr;
    static PFN_vkResetCommandBuffer                     _vkResetCommandBuffer                     = nullptr;
    static PFN_vkBeginCommandBuffer                     _vkBeginCommandBuffer                     = nullptr;
    static PFN_vkCmdBeginRenderPass                     _vkCmdBeginRenderPass                     = nullptr;
    static PFN_vkCmdEndRenderPass                       _vkCmdEndRenderPass                       = nullptr;
    static PFN_vkEndCommandBuffer                       _vkEndCommandBuffer                       = nullptr;
    static PFN_vkQueueSubmit                            _vkQueueSubmit                            = nullptr;
    static PFN_vkDestroyFence                           _vkDestroyFence                           = nullptr;
    static PFN_vkDestroySemaphore                       _vkDestroySemaphore                       = nullptr;
    static PFN_vkDestroyFramebuffer                     _vkDestroyFramebuffer                     = nullptr;
    static PFN_vkDestroyImageView                       _vkDestroyImageView                       = nullptr;
    static PFN_vkFreeCommandBuffers                     _vkFreeCommandBuffers                     = nullptr;
    static PFN_vkDestroyCommandPool                     _vkDestroyCommandPool                     = nullptr;
    static PFN_vkDestroyRenderPass                      _vkDestroyRenderPass                      = nullptr;
    static PFN_vkDestroyDescriptorPool                  _vkDestroyDescriptorPool                  = nullptr;

    // =========================================================================
    // Captured Vulkan state
    // =========================================================================
    static VkDevice         g_Device          = VK_NULL_HANDLE;
    static VkPhysicalDevice g_PhysicalDevice  = VK_NULL_HANDLE;
    static VkQueue          g_Queue           = VK_NULL_HANDLE;
    static uint32_t         g_QueueFamily     = 0;
    static VkRenderPass     g_RenderPass      = VK_NULL_HANDLE;
    static VkCommandPool    g_CommandPool     = VK_NULL_HANDLE;
    static VkDescriptorPool g_DescriptorPool  = VK_NULL_HANDLE;
    static VkSwapchainKHR   g_Swapchain       = VK_NULL_HANDLE;
    static uint32_t         g_ImageCount      = 0;
    static VkExtent2D       g_SwapchainExtent = {};
    static bool             g_ImGuiInitialized  = false;
    static bool             g_SwapchainReady    = false;
    static bool             g_DeviceCaptured    = false;

    // =========================================================================
    // Per-frame resources
    // =========================================================================
    struct FrameData {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFramebuffer   framebuffer   = VK_NULL_HANDLE;
        VkImageView     imageView     = VK_NULL_HANDLE;
        VkFence         fence         = VK_NULL_HANDLE;
        VkSemaphore     semaphore     = VK_NULL_HANDLE;
    };
    static std::vector<FrameData> g_Frames;
    static std::vector<VkImage>   g_SwapchainImages;

    // =========================================================================
    // Original / trampoline function pointers
    // Declared all together at the top so every hook below can reference them.
    // =========================================================================
    static PFN_vkGetInstanceProcAddr orig_vkGetInstanceProcAddr = nullptr;
    static PFN_vkGetDeviceProcAddr   orig_vkGetDeviceProcAddr   = nullptr;
    static PFN_vkCreateDevice        orig_vkCreateDevice        = nullptr;
    static PFN_vkCreateSwapchainKHR  orig_vkCreateSwapchainKHR  = nullptr;
    static PFN_vkQueuePresentKHR     orig_vkQueuePresentKHR     = nullptr;

    // =========================================================================
    // Forward declarations so hooks can reference each other freely
    // =========================================================================
    static PFN_vkVoidFunction hook_vkGetInstanceProcAddr(VkInstance instance, const char* pName);
    static PFN_vkVoidFunction hook_vkGetDeviceProcAddr(VkDevice device, const char* pName);
    static VkResult           hook_vkCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
    static VkResult           hook_vkCreateSwapchainKHR(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*);
    static VkResult           hook_vkQueuePresentKHR(VkQueue, const VkPresentInfoKHR*);

    // =========================================================================
    // Cleanup
    // =========================================================================
    static void CleanupFrameData() {
        if (g_Device == VK_NULL_HANDLE) return;
        _vkDeviceWaitIdle(g_Device);

        for (auto& f : g_Frames) {
            if (f.framebuffer  && g_Device) _vkDestroyFramebuffer(g_Device, f.framebuffer, nullptr);
            if (f.imageView    && g_Device) _vkDestroyImageView(g_Device, f.imageView, nullptr);
            if (f.commandBuffer && g_CommandPool)
                _vkFreeCommandBuffers(g_Device, g_CommandPool, 1, &f.commandBuffer);
            if (f.fence        && g_Device) _vkDestroyFence(g_Device, f.fence, nullptr);
            if (f.semaphore    && g_Device) _vkDestroySemaphore(g_Device, f.semaphore, nullptr);
        }
        g_Frames.clear();

        if (g_RenderPass)     { _vkDestroyRenderPass(g_Device, g_RenderPass, nullptr);         g_RenderPass     = VK_NULL_HANDLE; }
        if (g_CommandPool)    { _vkDestroyCommandPool(g_Device, g_CommandPool, nullptr);        g_CommandPool    = VK_NULL_HANDLE; }
        if (g_DescriptorPool) { _vkDestroyDescriptorPool(g_Device, g_DescriptorPool, nullptr); g_DescriptorPool = VK_NULL_HANDLE; }

        g_SwapchainReady = false;
    }

    // =========================================================================
    // Swapchain + ImGui backend setup
    // =========================================================================
    static bool SetupSwapchainResources(VkSwapchainKHR swapchain, VkFormat format, VkExtent2D extent) {
        if (!g_DeviceCaptured || g_Device == VK_NULL_HANDLE) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "SetupSwapchainResources: device not captured yet, skipping");
            return false;
        }

        CleanupFrameData();

        g_Swapchain       = swapchain;
        g_SwapchainExtent = extent;

        // --- Swapchain images ---
        _vkGetSwapchainImagesKHR(g_Device, swapchain, &g_ImageCount, nullptr);
        g_SwapchainImages.resize(g_ImageCount);
        _vkGetSwapchainImagesKHR(g_Device, swapchain, &g_ImageCount, g_SwapchainImages.data());

        // --- Render pass ---
        // loadOp = LOAD so we composite on top of the game frame.
        VkAttachmentDescription attachment{};
        attachment.format         = format;
        attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout  = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;

        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments    = &attachment;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies   = &dep;

        if (_vkCreateRenderPass(g_Device, &rpInfo, nullptr, &g_RenderPass) != VK_SUCCESS) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateRenderPass failed");
            return false;
        }

        // --- Descriptor pool ---
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets       = 1000;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes    = &poolSize;
        if (_vkCreateDescriptorPool(g_Device, &poolInfo, nullptr, &g_DescriptorPool) != VK_SUCCESS) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateDescriptorPool failed");
            return false;
        }

        // --- Command pool ---
        VkCommandPoolCreateInfo cpInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cpInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpInfo.queueFamilyIndex = g_QueueFamily;
        if (_vkCreateCommandPool(g_Device, &cpInfo, nullptr, &g_CommandPool) != VK_SUCCESS) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateCommandPool failed");
            return false;
        }

        // --- Per-frame resources ---
        g_Frames.resize(g_ImageCount);

        VkCommandBufferAllocateInfo cbInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbInfo.commandPool        = g_CommandPool;
        cbInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbInfo.commandBufferCount = 1;

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // start signalled so first WaitForFences returns immediately

        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

        for (uint32_t i = 0; i < g_ImageCount; i++) {
            auto& f = g_Frames[i];

            VkImageViewCreateInfo ivInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            ivInfo.image            = g_SwapchainImages[i];
            ivInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
            ivInfo.format           = format;
            ivInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (_vkCreateImageView(g_Device, &ivInfo, nullptr, &f.imageView) != VK_SUCCESS) {
                __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateImageView failed for frame %d", i);
                return false;
            }

            VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fbInfo.renderPass      = g_RenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments    = &f.imageView;
            fbInfo.width           = extent.width;
            fbInfo.height          = extent.height;
            fbInfo.layers          = 1;
            if (_vkCreateFramebuffer(g_Device, &fbInfo, nullptr, &f.framebuffer) != VK_SUCCESS) {
                __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateFramebuffer failed for frame %d", i);
                return false;
            }

            _vkAllocateCommandBuffers(g_Device, &cbInfo, &f.commandBuffer);
            _vkCreateFence(g_Device, &fenceInfo, nullptr, &f.fence);
            _vkCreateSemaphore(g_Device, &semInfo, nullptr, &f.semaphore);
        }

        // --- ImGui Vulkan backend ---
        // Feed our dlsym'd pointers into the ImGui backend so it doesn't go
        // through Unity's (potentially patched) dispatch table.
        ImGui_ImplVulkan_LoadFunctions([](const char* name, void* userdata) -> PFN_vkVoidFunction {
            return (PFN_vkVoidFunction)dlsym(userdata, name);
        }, g_VulkanLib);

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.Instance       = VK_NULL_HANDLE;
        initInfo.PhysicalDevice = g_PhysicalDevice;
        initInfo.Device         = g_Device;
        initInfo.QueueFamily    = g_QueueFamily;
        initInfo.Queue          = g_Queue;
        initInfo.DescriptorPool = g_DescriptorPool;
        initInfo.RenderPass     = g_RenderPass;
        initInfo.MinImageCount  = g_ImageCount;
        initInfo.ImageCount     = g_ImageCount;
        initInfo.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init(&initInfo);
        ImGui_ImplVulkan_CreateFontsTexture();

        g_SwapchainReady = true;
        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "Vulkan ImGui initialized: %d images, %dx%d",
            g_ImageCount, extent.width, extent.height);
        return true;
    }

    // =========================================================================
    // Hook: vkGetInstanceProcAddr
    // Intercepts Unity building its instance-level dispatch table.
    // vkCreateDevice and vkGetDeviceProcAddr are instance-level lookups.
    // =========================================================================
    static PFN_vkVoidFunction hook_vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
        if (!pName) return orig_vkGetInstanceProcAddr(instance, pName);

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkGetInstanceProcAddr: %s", pName);

        if (strcmp(pName, "vkGetInstanceProcAddr") == 0) return (PFN_vkVoidFunction)hook_vkGetInstanceProcAddr;
        if (strcmp(pName, "vkGetDeviceProcAddr")   == 0) return (PFN_vkVoidFunction)hook_vkGetDeviceProcAddr;
        if (strcmp(pName, "vkCreateDevice")        == 0) return (PFN_vkVoidFunction)hook_vkCreateDevice;
        if (strcmp(pName, "vkCreateSwapchainKHR")  == 0) return (PFN_vkVoidFunction)hook_vkCreateSwapchainKHR;
        if (strcmp(pName, "vkQueuePresentKHR")     == 0) return (PFN_vkVoidFunction)hook_vkQueuePresentKHR;

        return orig_vkGetInstanceProcAddr(instance, pName);
    }

    // =========================================================================
    // Hook: vkGetDeviceProcAddr
    // Intercepts Unity building its device-level dispatch table.
    // NOTE: vkCreateDevice is instance-level; do NOT intercept it here or
    //       most drivers will return nullptr and crash Unity.
    // =========================================================================
    static PFN_vkVoidFunction hook_vkGetDeviceProcAddr(VkDevice device, const char* pName) {
        if (!pName) return orig_vkGetDeviceProcAddr(device, pName);

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkGetDeviceProcAddr: %s", pName);

        if (strcmp(pName, "vkGetDeviceProcAddr")  == 0) return (PFN_vkVoidFunction)hook_vkGetDeviceProcAddr;
        if (strcmp(pName, "vkCreateSwapchainKHR") == 0) return (PFN_vkVoidFunction)hook_vkCreateSwapchainKHR;
        if (strcmp(pName, "vkQueuePresentKHR")    == 0) return (PFN_vkVoidFunction)hook_vkQueuePresentKHR;

        return orig_vkGetDeviceProcAddr(device, pName);
    }

    // =========================================================================
    // Hook: vkCreateDevice
    // Captures the VkDevice, physical device, queue family, and queue.
    // Also initialises the ImGui context (once).
    // =========================================================================
    static VkResult hook_vkCreateDevice(
        VkPhysicalDevice              physicalDevice,
        const VkDeviceCreateInfo*     pCreateInfo,
        const VkAllocationCallbacks*  pAllocator,
        VkDevice*                     pDevice)
    {
        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateDevice fired!");

        VkResult result = orig_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
        if (result != VK_SUCCESS) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateDevice failed: %d", result);
            return result;
        }

        g_PhysicalDevice = physicalDevice;
        g_Device         = *pDevice;

        // Find graphics queue family
        uint32_t count = 0;
        _vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
        std::vector<VkQueueFamilyProperties> props(count);
        _vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, props.data());
        for (uint32_t i = 0; i < count; i++) {
            if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                g_QueueFamily = i;
                break;
            }
        }
        _vkGetDeviceQueue(*pDevice, g_QueueFamily, 0, &g_Queue);

        g_DeviceCaptured = true;

        // Initialise the ImGui context once (no Vulkan backend yet — that
        // happens in SetupSwapchainResources after vkCreateSwapchainKHR).
        if (!g_ImGuiInitialized) {
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, 22, 22.0f);
            ImGui_ImplAndroid_Init(nullptr);
            SetupImGui();
            g_ImGuiInitialized = true;
        }

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "vkCreateDevice hooked — device=%p queueFamily=%d queue=%p",
            (void*)g_Device, g_QueueFamily, (void*)g_Queue);

        return VK_SUCCESS;
    }

    // =========================================================================
    // Hook: vkCreateSwapchainKHR
    // Sets up per-frame resources and initialises the ImGui Vulkan backend.
    // =========================================================================
    static VkResult hook_vkCreateSwapchainKHR(
        VkDevice                        device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks*    pAllocator,
        VkSwapchainKHR*                 pSwapchain)
    {
        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateSwapchainKHR fired!");

        // Lazily resolve the real pointer via the device proc addr chain if
        // our direct-symbol hook trampoline is somehow nullptr.
        if (!orig_vkCreateSwapchainKHR) {
            orig_vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)
                orig_vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");
            if (!orig_vkCreateSwapchainKHR) {
                __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "Could not resolve vkCreateSwapchainKHR, aborting");
                return VK_ERROR_INITIALIZATION_FAILED;
            }
        }

        VkResult result = orig_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
        if (result != VK_SUCCESS) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateSwapchainKHR failed: %d", result);
            return result;
        }

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "vkCreateSwapchainKHR: %dx%d fmt=%d",
            pCreateInfo->imageExtent.width,
            pCreateInfo->imageExtent.height,
            pCreateInfo->imageFormat);

        SetupSwapchainResources(*pSwapchain, pCreateInfo->imageFormat, pCreateInfo->imageExtent);
        return VK_SUCCESS;
    }

    // =========================================================================
    // Hook: vkQueuePresentKHR
    // Renders ImGui on top of each swapchain image before it is presented.
    // =========================================================================
    static VkResult hook_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
        static bool logged = false;
        if (!logged) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkQueuePresentKHR fired!");
            logged = true;
        }

        // Lazily resolve the real pointer if necessary.
        if (!orig_vkQueuePresentKHR) {
            if (!g_DeviceCaptured) {
                // Can't resolve without a device — just pass through.
                __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkQueuePresentKHR: device not captured yet, passing through");
                // We don't have orig yet either — this path should only hit if
                // our direct symbol hook fired but the proc addr path hasn't.
                // Nothing we can do safely; return an error to surface the issue.
                return VK_ERROR_DEVICE_LOST;
            }
            orig_vkQueuePresentKHR = (PFN_vkQueuePresentKHR)
                orig_vkGetDeviceProcAddr(g_Device, "vkQueuePresentKHR");
        }

        if (!g_DeviceCaptured) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkQueuePresentKHR: device not captured");
            return orig_vkQueuePresentKHR(queue, pPresentInfo);
        }

        if (!g_SwapchainReady || g_Frames.empty()) {
            return orig_vkQueuePresentKHR(queue, pPresentInfo);
        }

        // Sanity check: warn if the present queue differs from the captured queue.
        // This is legal in Vulkan but means we must submit on the correct queue.
        if (queue != g_Queue) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                "WARNING: present queue %p != captured queue %p, using present queue",
                (void*)queue, (void*)g_Queue);
        }

        std::vector<VkSemaphore> signalSems;
        signalSems.reserve(pPresentInfo->swapchainCount);

        for (uint32_t s = 0; s < pPresentInfo->swapchainCount; s++) {
            uint32_t imageIndex = pPresentInfo->pImageIndices[s];
            if (imageIndex >= g_Frames.size()) {
                __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                    "imageIndex %d out of range (frames=%zu)", imageIndex, g_Frames.size());
                continue;
            }

            auto& f = g_Frames[imageIndex];

            _vkWaitForFences(g_Device, 1, &f.fence, VK_TRUE, UINT64_MAX);
            _vkResetFences(g_Device, 1, &f.fence);
            _vkResetCommandBuffer(f.commandBuffer, 0);

            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            _vkBeginCommandBuffer(f.commandBuffer, &beginInfo);

            VkRenderPassBeginInfo rpBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rpBegin.renderPass        = g_RenderPass;
            rpBegin.framebuffer       = f.framebuffer;
            rpBegin.renderArea.extent = g_SwapchainExtent;
            // clearValueCount intentionally 0 — loadOp=LOAD, we don't clear
            _vkCmdBeginRenderPass(f.commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

            // ImGui frame
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplAndroid_NewFrame(g_SwapchainExtent.width, g_SwapchainExtent.height);
            ImGui::NewFrame();
            Menu::DrawMenu();
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), f.commandBuffer);

            _vkCmdEndRenderPass(f.commandBuffer);
            _vkEndCommandBuffer(f.commandBuffer);

            // Wait on whatever semaphores the game signalled, then signal
            // our own semaphore so the present knows rendering is done.
            VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.waitSemaphoreCount   = (s == 0) ? pPresentInfo->waitSemaphoreCount : 0;
            submit.pWaitSemaphores      = (s == 0) ? pPresentInfo->pWaitSemaphores    : nullptr;
            submit.pWaitDstStageMask    = &waitStage;
            submit.commandBufferCount   = 1;
            submit.pCommandBuffers      = &f.commandBuffer;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores    = &f.semaphore;

            // Use the queue that was passed in to this present call — it is
            // the queue Unity actually uses and may differ from g_Queue.
            _vkQueueSubmit(queue, 1, &submit, f.fence);

            signalSems.push_back(f.semaphore);
        }

        // Replace the wait semaphores with our own signal semaphores so the
        // driver waits for our ImGui draw before flipping.
        VkPresentInfoKHR modifiedPresent = *pPresentInfo;
        if (!signalSems.empty()) {
            modifiedPresent.waitSemaphoreCount = (uint32_t)signalSems.size();
            modifiedPresent.pWaitSemaphores    = signalSems.data();
        }

        return orig_vkQueuePresentKHR(queue, &modifiedPresent);
    }

    // =========================================================================
    // InstallEarly — called from __attribute__((constructor)) in native-lib.cpp
    // Hooks vkGetInstanceProcAddr and vkGetDeviceProcAddr as early as possible
    // so we intercept Unity's dispatch table construction.
    // =========================================================================
    static void InstallEarly() {
        void* vk = dlopen("libvulkan.so", RTLD_NOW | RTLD_GLOBAL);
        if (!vk) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "InstallEarly: libvulkan.so not found");
            return;
        }
        g_VulkanLib = vk;

        void* fnGetInstanceProcAddr = dlsym(vk, "vkGetInstanceProcAddr");
        void* fnGetDeviceProcAddr   = dlsym(vk, "vkGetDeviceProcAddr");

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "InstallEarly: GetInstanceProcAddr=%p GetDeviceProcAddr=%p",
            fnGetInstanceProcAddr, fnGetDeviceProcAddr);

        hook(fnGetInstanceProcAddr, (void*)hook_vkGetInstanceProcAddr, (void**)&orig_vkGetInstanceProcAddr);
        hook(fnGetDeviceProcAddr,   (void*)hook_vkGetDeviceProcAddr,   (void**)&orig_vkGetDeviceProcAddr);

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "InstallEarly: proc addr hooks installed");
    }

    // =========================================================================
    // InstallFull — called from the background thread in JNI_OnLoad once
    // libvulkan.so is confirmed loaded.
    // Loads all helper VK function pointers and installs the direct-symbol
    // hooks as a fallback in case the proc addr hooks missed anything.
    // =========================================================================
    static void InstallFull() {
        void* vk = g_VulkanLib;
        if (!vk) {
            vk = dlopen("libvulkan.so", RTLD_NOW | RTLD_GLOBAL);
            if (!vk) {
                __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "InstallFull: libvulkan.so not found");
                return;
            }
            g_VulkanLib = vk;
        }

        // Load every VK function we call ourselves so we're not reliant on
        // Unity's dispatch table.
        #define LOAD_VK(fn) _ ## fn = (PFN_ ## fn)dlsym(vk, #fn); \
            if (!_ ## fn) __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "LOAD_VK: " #fn " not found!")
        LOAD_VK(vkGetPhysicalDeviceQueueFamilyProperties);
        LOAD_VK(vkGetDeviceQueue);
        LOAD_VK(vkDeviceWaitIdle);
        LOAD_VK(vkGetSwapchainImagesKHR);
        LOAD_VK(vkCreateRenderPass);
        LOAD_VK(vkCreateDescriptorPool);
        LOAD_VK(vkCreateCommandPool);
        LOAD_VK(vkAllocateCommandBuffers);
        LOAD_VK(vkCreateImageView);
        LOAD_VK(vkCreateFramebuffer);
        LOAD_VK(vkCreateFence);
        LOAD_VK(vkCreateSemaphore);
        LOAD_VK(vkWaitForFences);
        LOAD_VK(vkResetFences);
        LOAD_VK(vkResetCommandBuffer);
        LOAD_VK(vkBeginCommandBuffer);
        LOAD_VK(vkCmdBeginRenderPass);
        LOAD_VK(vkCmdEndRenderPass);
        LOAD_VK(vkEndCommandBuffer);
        LOAD_VK(vkQueueSubmit);
        LOAD_VK(vkDestroyFence);
        LOAD_VK(vkDestroySemaphore);
        LOAD_VK(vkDestroyFramebuffer);
        LOAD_VK(vkDestroyImageView);
        LOAD_VK(vkFreeCommandBuffers);
        LOAD_VK(vkDestroyCommandPool);
        LOAD_VK(vkDestroyRenderPass);
        LOAD_VK(vkDestroyDescriptorPool);
        #undef LOAD_VK

        // Direct symbol hooks — belt-and-braces fallback.
        // If Unity called vkGetInstanceProcAddr before our early hook fired
        // (very unlikely given constructor timing) these catch it anyway.
        void* createDevice    = dlsym(vk, "vkCreateDevice");
        void* createSwapchain = dlsym(vk, "vkCreateSwapchainKHR");
        void* queuePresent    = dlsym(vk, "vkQueuePresentKHR");

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "InstallFull: createDevice=%p createSwapchain=%p queuePresent=%p",
            createDevice, createSwapchain, queuePresent);

        // Only install these if the early hooks didn't already capture them.
        if (!orig_vkCreateDevice)
            hook(createDevice,    (void*)hook_vkCreateDevice,       (void**)&orig_vkCreateDevice);
        if (!orig_vkCreateSwapchainKHR)
            hook(createSwapchain, (void*)hook_vkCreateSwapchainKHR, (void**)&orig_vkCreateSwapchainKHR);
        if (!orig_vkQueuePresentKHR)
            hook(queuePresent,    (void*)hook_vkQueuePresentKHR,    (void**)&orig_vkQueuePresentKHR);

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "InstallFull: all Vulkan hooks installed");
    }

} // namespace VulkanHook