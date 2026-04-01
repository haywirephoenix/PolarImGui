#pragma once
#include <vulkan/vulkan.h>
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_impl_android.h"
#include "../ImGui/imgui_impl_vulkan.h"
#include "../ByNameModding/external/include/dobby.h"
#include "Misc/Utils.h"

namespace VulkanHook {

    // Captured Vulkan state
    static VkDevice             g_Device        = VK_NULL_HANDLE;
    static VkPhysicalDevice     g_PhysicalDevice = VK_NULL_HANDLE;
    static VkQueue              g_Queue         = VK_NULL_HANDLE;
    static uint32_t             g_QueueFamily   = 0;
    static VkRenderPass         g_RenderPass    = VK_NULL_HANDLE;
    static VkCommandPool        g_CommandPool   = VK_NULL_HANDLE;
    static VkDescriptorPool     g_DescriptorPool = VK_NULL_HANDLE;
    static VkSwapchainKHR       g_Swapchain     = VK_NULL_HANDLE;
    static uint32_t             g_ImageCount    = 0;
    static VkExtent2D           g_SwapchainExtent{};
    static bool                 g_Initialized   = false;
    static bool                 g_SwapchainReady = false;

    // Per-frame resources
    struct FrameData {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFramebuffer   framebuffer   = VK_NULL_HANDLE;
        VkImageView     imageView     = VK_NULL_HANDLE;
        VkFence         fence         = VK_NULL_HANDLE;
        VkSemaphore     semaphore     = VK_NULL_HANDLE;
    };
    static std::vector<FrameData> g_Frames;
    static std::vector<VkImage>   g_SwapchainImages;

    // Original function pointers
    static PFN_vkCreateDevice           orig_vkCreateDevice         = nullptr;
    static PFN_vkCreateSwapchainKHR     orig_vkCreateSwapchainKHR   = nullptr;
    static PFN_vkQueuePresentKHR        orig_vkQueuePresentKHR      = nullptr;

    static void CleanupFrameData() {
        if (g_Device == VK_NULL_HANDLE) return;
        vkDeviceWaitIdle(g_Device);
        for (auto& f : g_Frames) {
            if (f.framebuffer) vkDestroyFramebuffer(g_Device, f.framebuffer, nullptr);
            if (f.imageView)   vkDestroyImageView(g_Device, f.imageView, nullptr);
            if (f.commandBuffer && g_CommandPool)
                vkFreeCommandBuffers(g_Device, g_CommandPool, 1, &f.commandBuffer);
            if (f.fence)       vkDestroyFence(g_Device, f.fence, nullptr);
            if (f.semaphore)   vkDestroySemaphore(g_Device, f.semaphore, nullptr);
        }
        g_Frames.clear();
        if (g_RenderPass)    { vkDestroyRenderPass(g_Device, g_RenderPass, nullptr);    g_RenderPass    = VK_NULL_HANDLE; }
        if (g_CommandPool)   { vkDestroyCommandPool(g_Device, g_CommandPool, nullptr);   g_CommandPool   = VK_NULL_HANDLE; }
        if (g_DescriptorPool){ vkDestroyDescriptorPool(g_Device, g_DescriptorPool, nullptr); g_DescriptorPool = VK_NULL_HANDLE; }
        g_SwapchainReady = false;
    }

    static bool SetupSwapchainResources(VkSwapchainKHR swapchain, VkFormat format, VkExtent2D extent) {
        CleanupFrameData();

        g_Swapchain = swapchain;
        g_SwapchainExtent = extent;

        // Get swapchain images
        vkGetSwapchainImagesKHR(g_Device, swapchain, &g_ImageCount, nullptr);
        g_SwapchainImages.resize(g_ImageCount);
        vkGetSwapchainImagesKHR(g_Device, swapchain, &g_ImageCount, g_SwapchainImages.data());

        // Create render pass
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

        if (vkCreateRenderPass(g_Device, &rpInfo, nullptr, &g_RenderPass) != VK_SUCCESS)
            return false;

        // Create descriptor pool
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000}
        };
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets       = 1000;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes    = poolSizes;
        vkCreateDescriptorPool(g_Device, &poolInfo, nullptr, &g_DescriptorPool);

        // Create command pool
        VkCommandPoolCreateInfo cpInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cpInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpInfo.queueFamilyIndex = g_QueueFamily;
        vkCreateCommandPool(g_Device, &cpInfo, nullptr, &g_CommandPool);

        // Create per-frame resources
        g_Frames.resize(g_ImageCount);
        VkCommandBufferAllocateInfo cbInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbInfo.commandPool        = g_CommandPool;
        cbInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbInfo.commandBufferCount = 1;

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

        for (uint32_t i = 0; i < g_ImageCount; i++) {
            auto& f = g_Frames[i];

            // Image view
            VkImageViewCreateInfo ivInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            ivInfo.image            = g_SwapchainImages[i];
            ivInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
            ivInfo.format           = format;
            ivInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCreateImageView(g_Device, &ivInfo, nullptr, &f.imageView);

            // Framebuffer
            VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fbInfo.renderPass      = g_RenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments    = &f.imageView;
            fbInfo.width           = extent.width;
            fbInfo.height          = extent.height;
            fbInfo.layers          = 1;
            vkCreateFramebuffer(g_Device, &fbInfo, nullptr, &f.framebuffer);

            vkAllocateCommandBuffers(g_Device, &cbInfo, &f.commandBuffer);
            vkCreateFence(g_Device, &fenceInfo, nullptr, &f.fence);
            vkCreateSemaphore(g_Device, &semInfo, nullptr, &f.semaphore);
        }

        // Init ImGui Vulkan backend
        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.Instance        = VK_NULL_HANDLE; // not needed for basic rendering
        initInfo.PhysicalDevice  = g_PhysicalDevice;
        initInfo.Device          = g_Device;
        initInfo.QueueFamily     = g_QueueFamily;
        initInfo.Queue           = g_Queue;
        initInfo.DescriptorPool  = g_DescriptorPool;
        initInfo.RenderPass      = g_RenderPass;
        initInfo.MinImageCount   = g_ImageCount;
        initInfo.ImageCount      = g_ImageCount;
        initInfo.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init(&initInfo);
        ImGui_ImplVulkan_CreateFontsTexture();

        g_SwapchainReady = true;
        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "Vulkan ImGui initialized, %d images", g_ImageCount);
        return true;
    }

    // Hook: vkCreateDevice — capture device + queue
    static VkResult hook_vkCreateDevice(
        VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDevice* pDevice)
    {
        VkResult result = orig_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
        if (result == VK_SUCCESS) {
            g_PhysicalDevice = physicalDevice;
            g_Device = *pDevice;
            // Find graphics queue family
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
            std::vector<VkQueueFamilyProperties> props(count);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, props.data());
            for (uint32_t i = 0; i < count; i++) {
                if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    g_QueueFamily = i;
                    break;
                }
            }
            vkGetDeviceQueue(*pDevice, g_QueueFamily, 0, &g_Queue);

            // Init ImGui (context only, no backend yet)
            if (!g_Initialized) {
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.IniFilename = nullptr;
                // Load font
                extern unsigned int Roboto_Regular[];
                io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, 22, 22.0f);
                ImGui_ImplAndroid_Init(nullptr);
                SetupImGui();
                SetBlackGoldTheme();
                ImGui::GetStyle().ScaleAllSizes(3.0f);
                g_Initialized = true;
                __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateDevice hooked, queue family %d", g_QueueFamily);
            }
        }
        return result;
    }

    // Hook: vkCreateSwapchainKHR — set up per-frame resources
    static VkResult hook_vkCreateSwapchainKHR(
        VkDevice device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSwapchainKHR* pSwapchain)
    {
        VkResult result = orig_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
        if (result == VK_SUCCESS) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateSwapchainKHR hooked %dx%d fmt=%d",
                pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height, pCreateInfo->imageFormat);
            SetupSwapchainResources(*pSwapchain, pCreateInfo->imageFormat, pCreateInfo->imageExtent);
        }
        return result;
    }

    // Hook: vkQueuePresentKHR — render ImGui each frame
    static VkResult hook_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
        if (!g_SwapchainReady || g_Frames.empty())
            return orig_vkQueuePresentKHR(queue, pPresentInfo);

        for (uint32_t s = 0; s < pPresentInfo->swapchainCount; s++) {
            uint32_t imageIndex = pPresentInfo->pImageIndices[s];
            if (imageIndex >= g_Frames.size()) continue;

            auto& f = g_Frames[imageIndex];

            vkWaitForFences(g_Device, 1, &f.fence, VK_TRUE, UINT64_MAX);
            vkResetFences(g_Device, 1, &f.fence);
            vkResetCommandBuffer(f.commandBuffer, 0);

            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(f.commandBuffer, &beginInfo);

            VkRenderPassBeginInfo rpBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rpBegin.renderPass        = g_RenderPass;
            rpBegin.framebuffer       = f.framebuffer;
            rpBegin.renderArea.extent = g_SwapchainExtent;
            vkCmdBeginRenderPass(f.commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

            // ImGui frame
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplAndroid_NewFrame(g_SwapchainExtent.width, g_SwapchainExtent.height);
            ImGui::NewFrame();
            Menu::DrawMenu();
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), f.commandBuffer);

            vkCmdEndRenderPass(f.commandBuffer);
            vkEndCommandBuffer(f.commandBuffer);

            VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.waitSemaphoreCount   = pPresentInfo->waitSemaphoreCount;
            submit.pWaitSemaphores      = pPresentInfo->pWaitSemaphores;
            submit.pWaitDstStageMask    = &waitStage;
            submit.commandBufferCount   = 1;
            submit.pCommandBuffers      = &f.commandBuffer;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores    = &f.semaphore;
            vkQueueSubmit(g_Queue, 1, &submit, f.fence);
        }

        // Replace wait semaphores with our signal semaphores
        VkPresentInfoKHR modifiedPresent = *pPresentInfo;
        std::vector<VkSemaphore> signalSems;
        for (uint32_t s = 0; s < pPresentInfo->swapchainCount; s++) {
            uint32_t idx = pPresentInfo->pImageIndices[s];
            if (idx < g_Frames.size())
                signalSems.push_back(g_Frames[idx].semaphore);
        }
        if (!signalSems.empty()) {
            modifiedPresent.waitSemaphoreCount = (uint32_t)signalSems.size();
            modifiedPresent.pWaitSemaphores    = signalSems.data();
        }

        return orig_vkQueuePresentKHR(queue, &modifiedPresent);
    }

    static void Install() {
        void *vulkan = dlopen("libvulkan.so", RTLD_NOW | RTLD_NOLOAD);
        if (!vulkan) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "libvulkan.so not found");
            return;
        }

        auto createDevice    = dlsym(vulkan, "vkCreateDevice");
        auto createSwapchain = dlsym(vulkan, "vkCreateSwapchainKHR");
        auto queuePresent    = dlsym(vulkan, "vkQueuePresentKHR");

        hook(createDevice,    (void*)hook_vkCreateDevice,    (void**)&orig_vkCreateDevice);
        hook(createSwapchain, (void*)hook_vkCreateSwapchainKHR, (void**)&orig_vkCreateSwapchainKHR);
        hook(queuePresent,    (void*)hook_vkQueuePresentKHR, (void**)&orig_vkQueuePresentKHR);

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "Vulkan hooks installed");
    }
}