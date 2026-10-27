#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstring>

// Local create-info storage for the XeFG bridge. Never writes through the application's pNext chain.
struct VkwDx12DeviceSetup
{
    std::vector<const char*> extensions;
    std::vector<std::unique_ptr<unsigned char[]>> storage;
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES };
    bool supported = false;
    bool ready = false;

    static size_t FeatureSize(VkStructureType type)
    {
        switch (type)
        {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: return sizeof(VkPhysicalDeviceFeatures2);
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: return sizeof(VkPhysicalDeviceVulkan11Features);
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: return sizeof(VkPhysicalDeviceVulkan12Features);
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: return sizeof(VkPhysicalDeviceVulkan13Features);
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES: return sizeof(VkPhysicalDeviceTimelineSemaphoreFeatures);
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES: return sizeof(VkPhysicalDeviceBufferDeviceAddressFeatures);
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES: return sizeof(VkPhysicalDeviceShaderFloat16Int8Features);
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES: return sizeof(VkPhysicalDevice8BitStorageFeatures);
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: return sizeof(VkPhysicalDevice16BitStorageFeatures);
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES: return sizeof(VkPhysicalDeviceDescriptorIndexingFeatures);
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES: return sizeof(VkPhysicalDeviceScalarBlockLayoutFeatures);
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES: return sizeof(VkPhysicalDeviceSynchronization2Features);
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES: return sizeof(VkPhysicalDeviceMaintenance4Features);
        default: return 0;
        }
    }

    const void* EnableTimeline(const VkBaseInStructure* node, const VkBaseInStructure* target)
    {
        if (!node) return nullptr;
        const size_t size = FeatureSize(node->sType);
        if (!size) return nullptr; // Unknown prefix: retain the game's create info and decline the bridge.
        auto bytes = std::make_unique<unsigned char[]>(size);
        std::memcpy(bytes.get(), node, size);
        auto copy = reinterpret_cast<VkBaseOutStructure*>(bytes.get());
        if (node == target)
        {
            if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
                reinterpret_cast<VkPhysicalDeviceVulkan12Features*>(copy)->timelineSemaphore = VK_TRUE;
            else
                reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreFeatures*>(copy)->timelineSemaphore = VK_TRUE;
        }
        else
        {
            auto next = EnableTimeline(node->pNext, target);
            if (!next) return nullptr;
            copy->pNext = const_cast<VkBaseOutStructure*>(static_cast<const VkBaseOutStructure*>(next));
        }
        const void* result = copy;
        storage.emplace_back(std::move(bytes));
        return result;
    }

    bool Probe(VkPhysicalDevice physical, PFN_vkGetPhysicalDeviceFeatures2 getFeatures)
    {
        if (!physical || !getFeatures) return false;
        VkPhysicalDeviceTimelineSemaphoreFeatures features { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES };
        VkPhysicalDeviceFeatures2 query { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features };
        getFeatures(physical, &query);
        if (!features.timelineSemaphore) return false;
        uint32_t count = 0;
        if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr) != VK_SUCCESS) return false;
        std::vector<VkExtensionProperties> offered(count);
        if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, offered.data()) != VK_SUCCESS) return false;
        auto has = [&](const char* name) {
            for (const auto& extension : offered) if (!std::strcmp(name, extension.extensionName)) return true;
            return false;
        };
        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(physical, &properties);
        VkPhysicalDeviceIDProperties id { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
        VkPhysicalDeviceProperties2 ids { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id };
        vkGetPhysicalDeviceProperties2(physical, &ids);
        supported = id.deviceLUIDValid && has(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME) &&
                    has(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME) &&
                    (properties.apiVersion >= VK_API_VERSION_1_2 || has(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME));
        return supported;
    }

    bool Prepare(VkDeviceCreateInfo& info)
    {
        if (!supported) return false;
        const VkBaseInStructure* target = nullptr;
        bool enabled = false;
        for (auto next = static_cast<const VkBaseInStructure*>(info.pNext); next; next = next->pNext)
        {
            if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
            {
                target = next;
                enabled = reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(next)->timelineSemaphore != 0;
                break;
            }
            if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES)
            {
                target = next;
                enabled = reinterpret_cast<const VkPhysicalDeviceTimelineSemaphoreFeatures*>(next)->timelineSemaphore != 0;
                break;
            }
        }
        if (target && !enabled)
        {
            auto chain = EnableTimeline(static_cast<const VkBaseInStructure*>(info.pNext), target);
            if (!chain) return false;
            info.pNext = chain;
        }
        else if (!target)
        {
            timeline.timelineSemaphore = VK_TRUE;
            timeline.pNext = const_cast<void*>(info.pNext);
            info.pNext = &timeline;
        }
        extensions.assign(info.ppEnabledExtensionNames, info.ppEnabledExtensionNames + info.enabledExtensionCount);
        // The base dependency extensions are already enabled by ILION's Vulkan spoofing helper.
        auto add = [&](const char* name) {
            for (auto value : extensions) if (!std::strcmp(value, name)) return;
            extensions.push_back(name);
        };
        add(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
        add(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        ready = true;
        return true;
    }
};
