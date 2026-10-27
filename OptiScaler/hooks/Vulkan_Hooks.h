#pragma once
#include "SysUtils.h"

class VulkanHooks
{
  public:
    static PFN_vkCreateSemaphore o_vkCreateSemaphore;
    static PFN_vkSignalSemaphore o_vkSignalSemaphore;
    static PFN_vkAntiLagUpdateAMD o_vkAntiLagUpdateAMD;

    static PFN_vkVoidFunction GetDeviceProcAddr(VkDevice device, const char* name);
    static PFN_vkVoidFunction GetInstanceProcAddr(VkInstance instance, const char* name);
    static void Hook(HMODULE vulkan1);
    static void Unhook();
};
