#pragma once

#include "SysUtils.h"

#include "../../plugin_sdk/optiscaler_plugin.h"

#include <imgui/imgui.h>

#include <filesystem>
#include <string>
#include <vector>

// ILION v1.4.0 fase 2 exp4: OptiScaler v10's plugin API (plugin_sdk/optiscaler_plugin.h, MIT), the host side taken from
// OptiScaler's plugins/PluginApi (GPL-3.0) for ILION's menu and config. A plugin (an .asi of the plugins folder or a
// custom DLSSG replacement such as realdody's XeMFG XMX) exports OptiPluginInit, keeps its settings in
// [Plugin.<name>] of OptiScaler.ini and can draw a tab in ILION's menu
class PluginApi
{
  public:
    struct TabInfo
    {
        std::string plugin; // the plugin's name, its [Plugin.<name>] section
        std::string name;
        std::string description;
    };

    // Calls OptiPluginInit of a loaded ASI plugin, once per module; its name is the file name without the extension
    static void InitPlugin(HMODULE module, PFN_OptiPluginInit init, const std::filesystem::path& path);

    // Same for any other dll, name is its [Plugin.<name>] section (a custom replacement: its folder name, as OptiScaler)
    static void InitPlugin(HMODULE module, PFN_OptiPluginInit init, const std::string& name);

    // True once after a plugin asked for an upscaler re-init
    static bool TakeReInitRequest();

    static std::vector<TabInfo> GetTabs();

    // Draws a registered tab (an index of GetTabs) where the menu is; colorMap may be null
    static void DrawTab(size_t index, float menuScale, ImVec4 (*colorMap)(const ImVec4&) = nullptr);
};
