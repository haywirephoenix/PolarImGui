#include <vulkan/vulkan.h>
#include <fstream>
#include <sstream>
#include "nlohmann/json.hpp"
#include "http/cpr/cpr.h"
#include "Misc/Logging.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_android.h"
#include "ImGui/imgui_impl_vulkan.h"
#include "Obfuscation/Obfuscate.h"
#include <stdio.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <string>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include "Misc/JNIStuff.h"
#include "Misc/FileWrapper.h"
#include "Misc/Utils.h"
#include "BNM/BNM.hpp"
#include "Obfuscation/Custom_Obfuscate.h"
#include "Unity/Unity.h"
#include "Misc/FunctionPointers.h"
#include "Hooking/Hooks.h"
#include "Misc/ImGuiStuff.h"
#include "Menu.h"
#include "Hooking/JNIHooks.h"
#include "Hooking/VulkanHook.h"
#include "Unity/Screen.h"
#include "Unity/Input.h"

void OnBNMLoaded()
{
    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "OnBNMLoaded called!");
    Unity::Screen::Setup();
    Unity::Input::Setup();
    Pointers::LoadPointers();
}

bool emulator = true;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "JNI_OnLoad called!");
    JNIEnv *env;
    vm->GetEnv((void **) &env, JNI_VERSION_1_6);

    std::thread([](){
    // Wait for Unity's render thread to load Vulkan
    void* vk = nullptr;
    for (int i = 0; i < 100 && !vk; i++) {
        vk = dlopen("libvulkan.so", RTLD_NOLOAD | RTLD_NOW);
        if (!vk) usleep(50000); // 50ms
    }
    if (vk) {
        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "libvulkan.so found after wait, installing hooks");
        VulkanHook::Install();
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "libvulkan.so never loaded");
    }
    }).detach();

    BNM::Loading::AddOnLoadedEvent(OnBNMLoaded);
    bool bnmLoaded = BNM::Loading::TryLoadByJNI(env);
    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "BNM TryLoadByJNI result: %d", bnmLoaded);

    if (!emulator) {
        UnityPlayer_cls = env->FindClass(OBFUSCATE("com/unity3d/player/UnityPlayer"));
        UnityPlayer_CurrentActivity_fid = env->GetStaticFieldID(UnityPlayer_cls,
                                                                OBFUSCATE("currentActivity"),
                                                                OBFUSCATE("Landroid/app/Activity;"));
        hook((void *) env->functions->RegisterNatives, (void *) hook_RegisterNatives,
             (void **) &old_RegisterNatives);
    }

    return JNI_VERSION_1_6;
}

__attribute__((constructor))
void lib_main()
{
    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "lib_main called");
}