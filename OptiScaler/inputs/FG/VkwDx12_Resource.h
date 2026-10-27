#pragma once
#include <framegen/IFGFeature.h>
#include <vulkan/vulkan.h>

// Only the Vulkan-to-D3D12 bridge uses this transport contract. Existing FG backends stay unchanged.
struct VkwDx12Resource
{
    FG_ResourceType type;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t imageWidth = 0, imageHeight = 0, width = 0, height = 0;
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    FG_ResourceValidity validity = FG_ResourceValidity::ValidNow;
    int frameIndex = -1;
};
