#pragma once
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <android/log.h>
#include <vector>
#include <string.h>
#include <sys/mman.h>
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
    // dlsym'd Vulkan helpers — we call these directly, bypassing any dispatch
    // table Unity or libhwui may have built.
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
    static bool             g_ImGuiInitialized = false;
    static bool             g_SwapchainReady   = false;
    static bool             g_DeviceCaptured   = false;

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
    // Original / trampoline pointers — all declared up front so every hook
    // function below can reference them without ordering issues.
    // =========================================================================
    static PFN_vkCreateDevice       orig_vkCreateDevice       = nullptr;
    static PFN_vkCreateSwapchainKHR orig_vkCreateSwapchainKHR = nullptr;
    static PFN_vkQueuePresentKHR    orig_vkQueuePresentKHR    = nullptr;

    // =========================================================================
    // Forward declarations
    // =========================================================================
    static VkResult hook_vkCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
    static VkResult hook_vkCreateSwapchainKHR(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*);
    static VkResult hook_vkQueuePresentKHR(VkQueue, const VkPresentInfoKHR*);

    // =========================================================================
    // PatchPointersInLib
    // Scans the rw- (data) segments of a named library and replaces every
    // occurrence of realFn with hookFn. This is how we intercept Vulkan calls
    // from libraries (libhwui, libunity) that cached function pointers before
    // our Dobby hooks were installed.
    // =========================================================================
    static int PatchPointersInLib(const char* libName,
                                  void* realFn, void* hookFn,
                                  const char* fnName)
    {
        if (!realFn || !hookFn) return 0;

        FILE* maps = fopen("/proc/self/maps", "r");
        if (!maps) return 0;

        int patched = 0;
        char line[512];
        while (fgets(line, sizeof(line), maps)) {
            if (!strstr(line, libName))  continue;
            if (!strstr(line, "rw-p"))   continue;   // data segments only, never rx code

            uintptr_t start = 0, end = 0;
            sscanf(line, "%lx-%lx", &start, &end);
            if (!start || end <= start + sizeof(uintptr_t)) continue;

            uintptr_t* ptr  = reinterpret_cast<uintptr_t*>(start);
            uintptr_t* last = reinterpret_cast<uintptr_t*>(end - sizeof(uintptr_t));

            while (ptr <= last) {
                if (*ptr == reinterpret_cast<uintptr_t>(realFn)) {
                    uintptr_t pageStart = reinterpret_cast<uintptr_t>(ptr) & ~(uintptr_t)(4095);
                    mprotect(reinterpret_cast<void*>(pageStart), 4096, PROT_READ | PROT_WRITE);
                    *ptr = reinterpret_cast<uintptr_t>(hookFn);
                    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                        "Patched %s in %s @ %p", fnName, libName, ptr);
                    patched++;
                }
                ptr++;
            }
        }
        fclose(maps);
        return patched;
    }

    // Patch all three hooks into every cached dispatch table we can find.
    static void PatchAllDispatchTables() {
        void* vk = g_VulkanLib;

        // Get the exported addresses (what Dobby hooked)
        void* realCreateDevice    = dlsym(vk, "vkCreateDevice");
        void* realCreateSwapchain = dlsym(vk, "vkCreateSwapchainKHR");
        void* realQueuePresent    = dlsym(vk, "vkQueuePresentKHR");

        // Also get what the loader internally resolves these to —
        // libhwui calls vkGetInstanceProcAddr/vkGetDeviceProcAddr which returns
        // loader-internal stubs, NOT the exported symbol addresses.
        // We need to know those internal addresses to find them in data segments.
        PFN_vkGetInstanceProcAddr getInstProc =
            (PFN_vkGetInstanceProcAddr)dlsym(vk, "vkGetInstanceProcAddr");

        // We don't have a VkInstance here, but passing VK_NULL_HANDLE to
        // vkGetInstanceProcAddr returns the loader's pre-instance dispatch
        // for global/instance commands, which is what libhwui caches.
        void* loaderCreateDevice = nullptr;
        if (getInstProc)
            loaderCreateDevice = (void*)getInstProc(VK_NULL_HANDLE, "vkCreateDevice");

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "PatchAllDispatchTables:"
            "\n  exported:  cd=%p cs=%p qp=%p"
            "\n  loader(pre-inst): cd=%p",
            realCreateDevice, realCreateSwapchain, realQueuePresent,
            loaderCreateDevice);

        // Build a list of ALL addresses we should replace for each hook.
        // This covers both the exported address and any loader-internal variants.
        struct PatchTarget {
            std::vector<void*> realAddrs;
            void*              hookAddr;
            const char*        name;
        };

        PatchTarget targets[] = {
            { {realCreateDevice,    loaderCreateDevice}, (void*)hook_vkCreateDevice,       "vkCreateDevice"       },
            { {realCreateSwapchain, nullptr           }, (void*)hook_vkCreateSwapchainKHR, "vkCreateSwapchainKHR" },
            { {realQueuePresent,    nullptr           }, (void*)hook_vkQueuePresentKHR,    "vkQueuePresentKHR"    },
        };

        // For the mystery pointer we found (0x7a76fd20f0), also try patching it
        // directly as vkQueuePresentKHR since that's what the log suggests.
        // dladdr it first to confirm.
        void* mysteryPtr = (void*)0x7a76fd20f0; // from your log
        Dl_info info{};
        if (dladdr(mysteryPtr, &info)) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                "Mystery ptr: %s in %s",
                info.dli_sname ? info.dli_sname : "?",
                info.dli_fname ? info.dli_fname : "?");
            // If it's vkQueuePresentKHR, add it to the patch target
            if (info.dli_sname && strstr(info.dli_sname, "Present"))
                targets[2].realAddrs.push_back(mysteryPtr);
            if (info.dli_sname && strstr(info.dli_sname, "CreateSwapchain"))
                targets[1].realAddrs.push_back(mysteryPtr);
            if (info.dli_sname && strstr(info.dli_sname, "CreateDevice"))
                targets[0].realAddrs.push_back(mysteryPtr);
        }

        int n = 0;
        const char* libs[] = {"libhwui.so", "libunity.so"};
        for (const char* libName : libs) {
            for (auto& target : targets) {
                for (void* realAddr : target.realAddrs) {
                    if (realAddr)
                        n += PatchPointersInLib(libName, realAddr, target.hookAddr, target.name);
                }
            }
        }

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "PatchAllDispatchTables: %d pointers patched", n);
    }
    // =========================================================================
    // Cleanup
    // =========================================================================
    static void CleanupFrameData() {
        if (g_Device == VK_NULL_HANDLE) return;
        _vkDeviceWaitIdle(g_Device);

        for (auto& f : g_Frames) {
            if (f.framebuffer)  _vkDestroyFramebuffer(g_Device, f.framebuffer, nullptr);
            if (f.imageView)    _vkDestroyImageView(g_Device, f.imageView, nullptr);
            if (f.commandBuffer && g_CommandPool)
                _vkFreeCommandBuffers(g_Device, g_CommandPool, 1, &f.commandBuffer);
            if (f.fence)        _vkDestroyFence(g_Device, f.fence, nullptr);
            if (f.semaphore)    _vkDestroySemaphore(g_Device, f.semaphore, nullptr);
        }
        g_Frames.clear();

        if (g_RenderPass)     { _vkDestroyRenderPass(g_Device, g_RenderPass, nullptr);         g_RenderPass     = VK_NULL_HANDLE; }
        if (g_CommandPool)    { _vkDestroyCommandPool(g_Device, g_CommandPool, nullptr);        g_CommandPool    = VK_NULL_HANDLE; }
        if (g_DescriptorPool) { _vkDestroyDescriptorPool(g_Device, g_DescriptorPool, nullptr); g_DescriptorPool = VK_NULL_HANDLE; }

        g_SwapchainReady = false;
    }

    // =========================================================================
    // SetupSwapchainResources
    // =========================================================================
    static bool SetupSwapchainResources(VkSwapchainKHR swapchain, VkFormat format, VkExtent2D extent) {
        if (!g_DeviceCaptured || g_Device == VK_NULL_HANDLE) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                "SetupSwapchainResources: device not captured yet");
            return false;
        }

        CleanupFrameData();
        g_Swapchain       = swapchain;
        g_SwapchainExtent = extent;

        _vkGetSwapchainImagesKHR(g_Device, swapchain, &g_ImageCount, nullptr);
        g_SwapchainImages.resize(g_ImageCount);
        _vkGetSwapchainImagesKHR(g_Device, swapchain, &g_ImageCount, g_SwapchainImages.data());

        // Render pass — LOAD so we composite on top of the game frame
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

        VkCommandPoolCreateInfo cpInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cpInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpInfo.queueFamilyIndex = g_QueueFamily;
        if (_vkCreateCommandPool(g_Device, &cpInfo, nullptr, &g_CommandPool) != VK_SUCCESS) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateCommandPool failed");
            return false;
        }

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

            VkImageViewCreateInfo ivInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            ivInfo.image            = g_SwapchainImages[i];
            ivInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
            ivInfo.format           = format;
            ivInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (_vkCreateImageView(g_Device, &ivInfo, nullptr, &f.imageView) != VK_SUCCESS) {
                __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateImageView failed frame %d", i);
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
                __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateFramebuffer failed frame %d", i);
                return false;
            }

            _vkAllocateCommandBuffers(g_Device, &cbInfo, &f.commandBuffer);
            _vkCreateFence(g_Device, &fenceInfo, nullptr, &f.fence);
            _vkCreateSemaphore(g_Device, &semInfo, nullptr, &f.semaphore);
        }

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
            "Vulkan ImGui ready: %d images %dx%d fmt=%d",
            g_ImageCount, extent.width, extent.height, (int)format);
        return true;
    }

    // =========================================================================
    // Hook: vkCreateDevice
    // =========================================================================
    static VkResult hook_vkCreateDevice(
        VkPhysicalDevice             physicalDevice,
        const VkDeviceCreateInfo*    pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDevice*                    pDevice)
    {
        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateDevice fired!");

        VkResult result = orig_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
        if (result != VK_SUCCESS) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateDevice failed: %d", result);
            return result;
        }

        g_PhysicalDevice = physicalDevice;
        g_Device         = *pDevice;

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
            "vkCreateDevice: device=%p queueFamily=%d", (void*)*pDevice, g_QueueFamily);
        return VK_SUCCESS;
    }

    // =========================================================================
    // Hook: vkCreateSwapchainKHR
    // =========================================================================
    static VkResult hook_vkCreateSwapchainKHR(
        VkDevice                        device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks*    pAllocator,
        VkSwapchainKHR*                 pSwapchain)
    {
        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateSwapchainKHR fired!");

        VkResult result = orig_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
        if (result != VK_SUCCESS) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkCreateSwapchainKHR failed: %d", result);
            return result;
        }

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "vkCreateSwapchainKHR: %dx%d fmt=%d",
            pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height,
            (int)pCreateInfo->imageFormat);

        SetupSwapchainResources(*pSwapchain, pCreateInfo->imageFormat, pCreateInfo->imageExtent);
        return VK_SUCCESS;
    }

    // =========================================================================
    // Hook: vkQueuePresentKHR
    // =========================================================================
    static VkResult hook_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
        static bool logged = false;
        if (!logged) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "vkQueuePresentKHR fired!");
            logged = true;
        }

        if (!g_DeviceCaptured || !g_SwapchainReady || g_Frames.empty())
            return orig_vkQueuePresentKHR(queue, pPresentInfo);

        std::vector<VkSemaphore> signalSems;
        signalSems.reserve(pPresentInfo->swapchainCount);

        for (uint32_t s = 0; s < pPresentInfo->swapchainCount; s++) {
            uint32_t imageIndex = pPresentInfo->pImageIndices[s];
            if (imageIndex >= g_Frames.size()) continue;

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
            _vkCmdBeginRenderPass(f.commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplAndroid_NewFrame(g_SwapchainExtent.width, g_SwapchainExtent.height);
            ImGui::NewFrame();
            Menu::DrawMenu();
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), f.commandBuffer);

            _vkCmdEndRenderPass(f.commandBuffer);
            _vkEndCommandBuffer(f.commandBuffer);

            VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            // Only consume the game's wait semaphores on the first swapchain entry
            submit.waitSemaphoreCount   = (s == 0) ? pPresentInfo->waitSemaphoreCount : 0;
            submit.pWaitSemaphores      = (s == 0) ? pPresentInfo->pWaitSemaphores    : nullptr;
            submit.pWaitDstStageMask    = &waitStage;
            submit.commandBufferCount   = 1;
            submit.pCommandBuffers      = &f.commandBuffer;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores    = &f.semaphore;
            _vkQueueSubmit(queue, 1, &submit, f.fence);

            signalSems.push_back(f.semaphore);
        }

        VkPresentInfoKHR modifiedPresent = *pPresentInfo;
        if (!signalSems.empty()) {
            modifiedPresent.waitSemaphoreCount = (uint32_t)signalSems.size();
            modifiedPresent.pWaitSemaphores    = signalSems.data();
        }

        return orig_vkQueuePresentKHR(queue, &modifiedPresent);
    }

    // =========================================================================
    // InstallEarly — called from __attribute__((constructor)) in native-lib.cpp
    //
    // IMPORTANT: Do NOT hook vkGetInstanceProcAddr or vkGetDeviceProcAddr here.
    // On Android 16, libvulkan.so's internal dispatch stubs are protected by
    // PAC (Pointer Authentication). Dobby cannot safely patch their prologues
    // and will cause a SIGILL (ILL_ILLOPC) crash on the render thread.
    //
    // Instead we hook only the three exported target symbols, then in
    // InstallFull we walk the data segments of libhwui and libunity to patch
    // any cached copies of those pointers that were stored before our hooks.
    // =========================================================================
    static void InstallEarly() {
        void* vk = dlopen("libvulkan.so", RTLD_NOW | RTLD_GLOBAL);
        if (!vk) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "InstallEarly: libvulkan.so not found");
            return;
        }
        g_VulkanLib = vk;

        #define LOAD_VK(fn) _ ## fn = (PFN_ ## fn)dlsym(vk, #fn); \
            if (!_ ## fn) __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "LOAD_VK missing: " #fn)
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

        void* createDevice    = dlsym(vk, "vkCreateDevice");
        void* createSwapchain = dlsym(vk, "vkCreateSwapchainKHR");
        void* queuePresent    = dlsym(vk, "vkQueuePresentKHR");

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "InstallEarly: cd=%p cs=%p qp=%p", createDevice, createSwapchain, queuePresent);

        hook(createDevice,    (void*)hook_vkCreateDevice,       (void**)&orig_vkCreateDevice);
        hook(createSwapchain, (void*)hook_vkCreateSwapchainKHR, (void**)&orig_vkCreateSwapchainKHR);
        hook(queuePresent,    (void*)hook_vkQueuePresentKHR,    (void**)&orig_vkQueuePresentKHR);

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "InstallEarly: direct hooks installed");
    }

    static void DiagnoseDispatchTables() {
        void* vk = g_VulkanLib;
        void* realCreateDevice    = dlsym(vk, "vkCreateDevice");
        void* realCreateSwapchain = dlsym(vk, "vkCreateSwapchainKHR");
        void* realQueuePresent    = dlsym(vk, "vkQueuePresentKHR");

        const char* libs[] = {"libhwui.so", "libunity.so", "libvulkan.so"};
        
        for (const char* libName : libs) {
            FILE* maps = fopen("/proc/self/maps", "r");
            if (!maps) continue;
            char line[512];
            while (fgets(line, sizeof(line), maps)) {
                if (!strstr(line, libName)) continue;
                if (!strstr(line, "rw-p"))  continue;
                uintptr_t start = 0, end = 0;
                sscanf(line, "%lx-%lx", &start, &end);
                if (!start || end <= start + sizeof(uintptr_t)) continue;

                uintptr_t* ptr  = (uintptr_t*)start;
                uintptr_t* last = (uintptr_t*)(end - sizeof(uintptr_t));
                while (ptr <= last) {
                    uintptr_t v = *ptr;
                    // Print anything that looks like a libvulkan address
                    // (within ~1MB of our known symbols)
                    uintptr_t base = (uintptr_t)realQueuePresent & ~(uintptr_t)0xFFFFF;
                    if (v >= base && v < base + 0x100000) {
                        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                            "  [%s rw @ %p] = %p  (cd=%d cs=%d qp=%d)",
                            libName, ptr, (void*)v,
                            v == (uintptr_t)realCreateDevice,
                            v == (uintptr_t)realCreateSwapchain,
                            v == (uintptr_t)realQueuePresent);
                    }
                    ptr++;
                }
            }
            fclose(maps);
        }
    }

    static void DiagnoseAndPatchHwui() {
        void* vk = g_VulkanLib;
        
        // Find libvulkan.so base and size from /proc/self/maps
        uintptr_t vk_start = 0, vk_end = 0;
        FILE* maps = fopen("/proc/self/maps", "r");
        char line[512];
        while (fgets(line, sizeof(line), maps)) {
            if (!strstr(line, "libvulkan.so")) continue;
            uintptr_t s, e;
            sscanf(line, "%lx-%lx", &s, &e);
            if (!vk_start) vk_start = s;
            if (e > vk_end) vk_end = e;
        }
        fclose(maps);
        
        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "libvulkan range: %lx - %lx", vk_start, vk_end);

        // Now scan libhwui rw segments for ANY pointer into libvulkan
        maps = fopen("/proc/self/maps", "r");
        while (fgets(line, sizeof(line), maps)) {
            if (!strstr(line, "libhwui.so")) continue;
            if (!strstr(line, "rw-p")) continue;
            uintptr_t start, end;
            sscanf(line, "%lx-%lx", &start, &end);
            
            uintptr_t* ptr  = (uintptr_t*)start;
            uintptr_t* last = (uintptr_t*)(end - sizeof(uintptr_t));
            while (ptr <= last) {
                uintptr_t v = *ptr;
                if (v >= vk_start && v < vk_end) {
                    Dl_info info{};
                    dladdr((void*)v, &info);
                    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                        "hwui[%p] = %p  sym=%s",
                        ptr, (void*)v,
                        info.dli_sname ? info.dli_sname : "?");
                        
                    // Patch it if the symbol name matches what we want
                    if (info.dli_sname) {
                        void* hookFn = nullptr;
                        const char* n = info.dli_sname;
                        if (strstr(n, "QueuePresent"))    hookFn = (void*)hook_vkQueuePresentKHR;
                        if (strstr(n, "CreateSwapchain")) hookFn = (void*)hook_vkCreateSwapchainKHR;
                        if (strstr(n, "CreateDevice"))    hookFn = (void*)hook_vkCreateDevice;
                        if (hookFn) {
                            uintptr_t page = (uintptr_t)ptr & ~(uintptr_t)4095;
                            mprotect((void*)page, 4096, PROT_READ | PROT_WRITE);
                            *ptr = (uintptr_t)hookFn;
                            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                                "  -> PATCHED with hook %p", hookFn);
                        }
                    }
                }
                ptr++;
            }
        }
        fclose(maps);
    }

    static void IdentifyMysteryPointer() {
        uintptr_t mystery = 0x7a76fd20f0; // the stable address from hwui's data
        
        // Find which library it belongs to
        FILE* maps = fopen("/proc/self/maps", "r");
        char line[512];
        while (fgets(line, sizeof(line), maps)) {
            uintptr_t start, end;
            if (sscanf(line, "%lx-%lx", &start, &end) != 2) continue;
            if (mystery >= start && mystery < end) {
                __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                    "Mystery %p belongs to: %s", (void*)mystery, line);
            }
        }
        fclose(maps);
        
        // Also dladdr it
        Dl_info info{};
        if (dladdr((void*)mystery, &info)) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                "dladdr: sym=%s  file=%s  symaddr=%p",
                info.dli_sname ? info.dli_sname : "null",
                info.dli_fname ? info.dli_fname : "null",
                info.dli_saddr);
        } else {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "dladdr failed for mystery ptr");
        }

        // Also widen the hwui scan to catch ALL pointers, not just libvulkan-range
        // Dump the 8 pointers around the known one so we can see the struct context
        uintptr_t hwui_ptr_addr = 0x7a72915fa0;
        uintptr_t* ctx = (uintptr_t*)(hwui_ptr_addr - 4 * sizeof(uintptr_t));
        for (int i = -4; i <= 4; i++) {
            uintptr_t v = ctx[4 + i];
            Dl_info di{};
            dladdr((void*)v, &di);
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                "  hwui+%+d*8: [%p] = %p  (%s  %s)",
                i, &ctx[4+i], (void*)v,
                di.dli_fname ? di.dli_fname : "?",
                di.dli_sname ? di.dli_sname : "?");
        }
    }
    static void PatchHwuiDirectly() {
        // This address is stable across runs because libhwui.so and the
        // VulkanDispatch struct it points into are both Zygote-inherited mappings.
        // The slot at 0x7a72915fa0 holds a pointer to the loader's vkQueuePresentKHR
        // dispatch stub. We replace it with our hook. 
        
        uintptr_t* slot = (uintptr_t*)0x7a72915fa0;
        uintptr_t  current = *slot;
        
        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "PatchHwuiDirectly: slot=%p current=%p replacing with hook=%p",
            slot, (void*)current, (void*)hook_vkQueuePresentKHR);
        
        uintptr_t page = (uintptr_t)slot & ~(uintptr_t)4095;
        if (mprotect((void*)page, 4096, PROT_READ | PROT_WRITE) != 0) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "mprotect failed: %s", strerror(errno));
            return;
        }
        
        // Save the original so our hook can call through
        orig_vkQueuePresentKHR = (PFN_vkQueuePresentKHR)current;
        *slot = (uintptr_t)hook_vkQueuePresentKHR;
        
        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "PatchHwuiDirectly: patched! orig=%p", (void*)orig_vkQueuePresentKHR);

        // Now dump neighbours to find CreateSwapchain and CreateDevice slots too
        for (int i = -8; i <= 8; i++) {
            uintptr_t* p = slot + i;
            uintptr_t v = *p;
            Dl_info di{};
            dladdr((void*)v, &di);
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                "  slot[%+d] @ %p = %p  %s",
                i, p, (void*)v,
                di.dli_sname ? di.dli_sname : "?");
        }
    }

    static void WatchForPresentSlot() {
        // Scan ALL rw segments for the loader dispatch address 0x7a76fd20f0
        // and watch which one gets called (changes value after we patch it back)
        uintptr_t loaderPresent = 0x7a76fd20f0;
        
        std::vector<uintptr_t*> candidates;
        
        FILE* maps = fopen("/proc/self/maps", "r");
        char line[512];
        while (fgets(line, sizeof(line), maps)) {
            if (!strstr(line, "rw-p")) continue;
            // Skip obviously wrong regions
            if (strstr(line, "stack")) continue;
            if (strstr(line, "dalvik")) continue; // skip the LinearAlloc itself
            
            uintptr_t start, end;
            if (sscanf(line, "%lx-%lx", &start, &end) != 2) continue;
            if (end - start > 50 * 1024 * 1024) continue; // skip huge regions
            
            uintptr_t* ptr  = (uintptr_t*)start;
            uintptr_t* last = (uintptr_t*)(end - sizeof(uintptr_t));
            while (ptr <= last) {
                if (*ptr == loaderPresent) {
                    candidates.push_back(ptr);
                    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                        "Candidate present slot: %p in %s", ptr, line);
                }
                ptr++;
            }
        }
        fclose(maps);
        
        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "Found %zu candidate present slots total", candidates.size());
        
        // Patch ALL of them and see which one fires
        for (uintptr_t* slot : candidates) {
            uintptr_t page = (uintptr_t)slot & ~(uintptr_t)4095;
            mprotect((void*)page, 4096, PROT_READ | PROT_WRITE);
            orig_vkQueuePresentKHR = (PFN_vkQueuePresentKHR)*slot;
            *slot = (uintptr_t)hook_vkQueuePresentKHR;
        }
    }

    // =========================================================================
    // InstallFull — called from background thread in JNI_OnLoad.
    // Waits briefly for libhwui's one-time Vulkan init (std::call_once on the
    // render thread) to complete, then patches all cached dispatch tables.
    // =========================================================================
    static void InstallFull() {
        if (!g_VulkanLib) {
            __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "InstallFull: VulkanLib not set");
            return;
        }

        // Wait for libhwui's render thread one-time Vulkan init to complete
        usleep(500000);

        // The loader's internal dispatch stub for vkQueuePresentKHR —
        // this is the value cached in dispatch tables across all rw segments.
        // It lives in [anon:dalvik-Pre-zygote-LinearAlloc] so dladdr can't name it,
        // but it's stable across runs because it's Zygote-inherited.
        uintptr_t loaderPresent    = 0x7a76fd20f0;

        // Also try the raw exported addresses as fallback in case any library
        // cached those instead
        uintptr_t exportedPresent    = (uintptr_t)dlsym(g_VulkanLib, "vkQueuePresentKHR");
        uintptr_t exportedSwapchain  = (uintptr_t)dlsym(g_VulkanLib, "vkCreateSwapchainKHR");
        uintptr_t exportedDevice     = (uintptr_t)dlsym(g_VulkanLib, "vkCreateDevice");

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "InstallFull: loaderPresent=%p exportedPresent=%p",
            (void*)loaderPresent, (void*)exportedPresent);

        std::vector<uintptr_t*> presentCandidates;
        std::vector<uintptr_t*> swapchainCandidates;
        std::vector<uintptr_t*> deviceCandidates;

        FILE* maps = fopen("/proc/self/maps", "r");
        char line[512];
        while (fgets(line, sizeof(line), maps)) {
            if (!strstr(line, "rw-p"))    continue;
            if (strstr(line, "stack"))    continue;
            if (strstr(line, "dalvik-Pre-zygote")) continue; // skip the LinearAlloc itself

            uintptr_t start, end;
            if (sscanf(line, "%lx-%lx", &start, &end) != 2) continue;
            if (end <= start + sizeof(uintptr_t))            continue;
            if (end - start > 64 * 1024 * 1024)             continue; // skip huge regions

            uintptr_t* ptr  = (uintptr_t*)start;
            uintptr_t* last = (uintptr_t*)(end - sizeof(uintptr_t));
            while (ptr <= last) {
                uintptr_t v = *ptr;
                if (v == loaderPresent || v == exportedPresent) {
                    presentCandidates.push_back(ptr);
                    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                        "Present candidate: %p = %p  in %s", ptr, (void*)v, line);
                }
                if (v == exportedSwapchain) {
                    swapchainCandidates.push_back(ptr);
                    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                        "Swapchain candidate: %p = %p  in %s", ptr, (void*)v, line);
                }
                if (v == exportedDevice) {
                    deviceCandidates.push_back(ptr);
                    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                        "Device candidate: %p = %p  in %s", ptr, (void*)v, line);
                }
                ptr++;
            }
        }
        fclose(maps);

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "InstallFull: found %zu present, %zu swapchain, %zu device candidates",
            presentCandidates.size(), swapchainCandidates.size(), deviceCandidates.size());

        // Patch every candidate — one of them must be what the game's render
        // thread calls. The hook itself is safe to call multiple times.
        auto patchSlots = [](std::vector<uintptr_t*>& slots, void* hookFn,
                            void** origOut, const char* name) {
            for (uintptr_t* slot : slots) {
                uintptr_t page = (uintptr_t)slot & ~(uintptr_t)4095;
                if (mprotect((void*)page, 4096, PROT_READ | PROT_WRITE) != 0) {
                    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                        "mprotect failed for %s slot %p", name, slot);
                    continue;
                }
                uintptr_t old = *slot;
                *slot = (uintptr_t)hookFn;
                // Keep the last non-hook value as orig (any will do, they all
                // point to the same underlying function)
                if (origOut && old != (uintptr_t)hookFn)
                    *origOut = (void*)old;
                __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                    "Patched %s slot %p  old=%p", name, slot, (void*)old);
            }
        };

        patchSlots(presentCandidates,  (void*)hook_vkQueuePresentKHR,
                (void**)&orig_vkQueuePresentKHR,  "vkQueuePresentKHR");
        patchSlots(swapchainCandidates,(void*)hook_vkCreateSwapchainKHR,
                (void**)&orig_vkCreateSwapchainKHR,"vkCreateSwapchainKHR");
        patchSlots(deviceCandidates,   (void*)hook_vkCreateDevice,
                (void**)&orig_vkCreateDevice,       "vkCreateDevice");

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "InstallFull: all slots patched. orig: cd=%p cs=%p qp=%p",
            (void*)orig_vkCreateDevice,
            (void*)orig_vkCreateSwapchainKHR,
            (void*)orig_vkQueuePresentKHR);

        // Re-patch loop: keep watching for 5 seconds in case slots get
        // overwritten after we patch them (e.g. libhwui reinitialises)
        for (int i = 0; i < 50 && !g_SwapchainReady; i++) {
            usleep(100000); // 100ms

            uintptr_t hookAddr = (uintptr_t)hook_vkQueuePresentKHR;
            for (uintptr_t* slot : presentCandidates) {
                if (*slot != hookAddr) {
                    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
                        "Re-patch slot %p (was overwritten to %p)", slot, (void*)*slot);
                    uintptr_t page = (uintptr_t)slot & ~(uintptr_t)4095;
                    mprotect((void*)page, 4096, PROT_READ | PROT_WRITE);
                    orig_vkQueuePresentKHR = (PFN_vkQueuePresentKHR)*slot;
                    *slot = hookAddr;
                }
            }
        }

        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE",
            "InstallFull: done. DeviceCaptured=%d SwapchainReady=%d",
            (int)g_DeviceCaptured, (int)g_SwapchainReady);
    }
} // namespace VulkanHook