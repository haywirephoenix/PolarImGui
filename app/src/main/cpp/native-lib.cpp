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

// InstallEarly runs at .so load time — before any other thread including
// Unity's render thread. It hooks the three Vulkan exported symbols directly.
// It deliberately does NOT hook vkGetInstanceProcAddr / vkGetDeviceProcAddr
// because those are PAC-protected on Android 16 and will SIGILL under Dobby.
__attribute__((constructor))
void lib_main()
{
    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "lib_main called");
    VulkanHook::InstallEarly();
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    __android_log_print(ANDROID_LOG_ERROR, "HAYWIRE", "JNI_OnLoad called!");
    JNIEnv *env;
    vm->GetEnv((void **) &env, JNI_VERSION_1_6);

    // InstallFull waits 500ms then patches cached function pointers in
    // libhwui.so and libunity.so data segments, covering the case where those
    // libraries called vkGetInstanceProcAddr before our hooks were in place.
    std::thread([]() {
        VulkanHook::InstallFull();
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