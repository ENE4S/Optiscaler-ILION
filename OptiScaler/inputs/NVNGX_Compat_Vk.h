#pragma once

#include <nvsdk_ngx_vk.h>

// Vulkan entry points OptiScaler implements in NVNGX_DLSS_Vk.cpp and exports, which the headers of the official
// NVIDIA DLSS SDK do not declare. The code that turns FSR, FfxApi and XeSS inputs into NGX calls uses the last one.
// See NVNGX_Compat.h.

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_VULKAN_Init_Ext(unsigned long long InApplicationId,
                                                         const wchar_t* InApplicationDataPath, VkInstance InInstance,
                                                         VkPhysicalDevice InPD, VkDevice InDevice,
                                                         NVSDK_NGX_Version InSDKVersion,
                                                         const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_VULKAN_Init_Ext2(
    unsigned long long InApplicationId, const wchar_t* InApplicationDataPath, VkInstance InInstance,
    VkPhysicalDevice InPD, VkDevice InDevice, PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
    NVSDK_NGX_Version InSDKVersion, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_VULKAN_Init_ProjectID_Ext(
    const char* InProjectId, NVSDK_NGX_EngineType InEngineType, const char* InEngineVersion,
    const wchar_t* InApplicationDataPath, VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
    PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA, NVSDK_NGX_Version InSDKVersion,
    const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
