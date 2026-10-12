#pragma once

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// The pictures of the comparator are textures the renderer of the menu makes from pixels kept here, the same
// way it does with the font. That works the same in DX11, DX12 and Vulkan.
struct VisualPanel
{
    ImTextureData* texture = nullptr;
    std::string shown; // what its pixels were drawn from
};

// 0 and 1 are the two pictures of the comparator, 2 and 3 what its magnifier shows of each of them
inline VisualPanel visualPanels[4];
inline std::vector<ImTextureData*> visualRetiredTextures;

inline void RetireVisualTexture(ImTextureData*& texture)
{
    if (texture == nullptr)
        return;

    if (texture->TexID == ImTextureID_Invalid && texture->BackendUserData == nullptr)
    {
        // Never reached the renderer, or it already let go of it
        ImGui::UnregisterUserTexture(texture);
        IM_DELETE(texture);
    }
    else
    {
        // Frames in flight may still use it, the renderer releases it a few frames later
        texture->SetStatus(ImTextureStatus_WantDestroy);
        texture->UnusedFrames = 0;
        visualRetiredTextures.push_back(texture);
    }

    texture = nullptr;
}

inline void MaintainVisualTextures()
{
    auto& registered = ImGui::GetCurrentContext()->UserTextures;

    for (auto it = visualRetiredTextures.begin(); it != visualRetiredTextures.end();)
    {
        ImTextureData* texture = *it;

        if (texture->Status == ImTextureStatus_Destroyed)
        {
            ImGui::UnregisterUserTexture(texture);
            IM_DELETE(texture);
            it = visualRetiredTextures.erase(it);
            continue;
        }

        if (!registered.contains(texture))
            ImGui::RegisterUserTexture(texture);

        texture->UnusedFrames++;
        ++it;
    }

    for (auto& panel : visualPanels)
    {
        if (panel.texture == nullptr)
            continue;

        if (!registered.contains(panel.texture))
            ImGui::RegisterUserTexture(panel.texture);

        // The renderer rebuilt its objects and dropped the texture, the pixels are still here
        if (panel.texture->Status == ImTextureStatus_Destroyed)
            panel.texture->SetStatus(ImTextureStatus_WantCreate);
    }
}

// Draws width x height pixels into the texture of the panel when what it shows has changed
inline void UpdateVisualPanel(VisualPanel& panel, int width, int height, const std::string& shown,
                              const std::function<void(uint8_t*)>& draw)
{
    // Sizes in steps, resizing the window doesn't make a new texture for every pixel
    const int textureWidth = std::clamp((width + 127) / 128 * 128, 128, 8192);
    const int textureHeight = std::clamp((height + 127) / 128 * 128, 128, 8192);

    width = (std::min)(width, textureWidth);
    height = (std::min)(height, textureHeight);

    if (panel.texture == nullptr || panel.texture->Width != textureWidth || panel.texture->Height != textureHeight)
    {
        RetireVisualTexture(panel.texture);

        panel.texture = IM_NEW(ImTextureData)();
        panel.texture->Create(ImTextureFormat_RGBA32, textureWidth, textureHeight);
        panel.texture->UseColors = true;
        panel.texture->RefCount = 1;
        panel.texture->UsedRect = { 0, 0, (unsigned short) textureWidth, (unsigned short) textureHeight };
        panel.texture->SetStatus(ImTextureStatus_WantCreate);
        ImGui::RegisterUserTexture(panel.texture);
        panel.shown.clear();
    }

    if (panel.shown == shown)
        return;

    static std::vector<uint8_t> pixels;
    pixels.resize((size_t) width * height * 4);
    draw(pixels.data());

    for (int y = 0; y < height; y++)
        memcpy(panel.texture->GetPixelsAt(0, y), pixels.data() + (size_t) y * width * 4, (size_t) width * 4);

    // A texture that is waiting to be created is uploaded whole anyway
    if (panel.texture->Status == ImTextureStatus_OK || panel.texture->Status == ImTextureStatus_WantUpdates)
    {
        const ImTextureRect rect { 0, 0, (unsigned short) width, (unsigned short) height };

        panel.texture->UpdateRect = rect;
        panel.texture->Updates.resize(0);
        panel.texture->Updates.push_back(rect);
        panel.texture->SetStatus(ImTextureStatus_WantUpdates);
    }

    panel.shown = shown;
}
