#pragma once

// ILION: DLSS 4 through d4r is, for now, only for AMD RDNA 3 (the gfx11 generation its kernels are built for).
// Elsewhere it stays visible but can't be chosen, and a saved d4r choice is ignored for the run (not erased).
// The generation comes from AMD's device table (device_info) and the PCI ids Windows lists for the display
// adapters, which works even while OptiScaler's dll is loading (no DXGI or D3D12 there); the menu uses IdentifyGpu.

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include <include/device_info/device_info.hpp>

namespace IlionD4rGuard
{
inline bool IsRdna3(device_info::HwGeneration generation) { return generation == device_info::HwGeneration::kGfx11; }

// "PCI\VEN_1002&DEV_744C&SUBSYS_...&REV_C8": AMD and RDNA 3 by its device id
inline bool IsRdna3PciId(const char* pciId)
{
    if (pciId == nullptr)
        return false;

    const std::string id(pciId);
    const auto vendorAt = id.find("VEN_");
    const auto deviceAt = id.find("DEV_");

    if (vendorAt == std::string::npos || deviceAt == std::string::npos)
        return false;

    const auto vendor = (uint32_t) std::strtoul(id.substr(vendorAt + 4, 4).c_str(), nullptr, 16);
    const auto device = (uint32_t) std::strtoul(id.substr(deviceAt + 4, 4).c_str(), nullptr, 16);

    if (vendor != device_info::kAmdVendorId)
        return false;

    const auto card = device_info::GetCardInfo({ vendor, device, device_info::kRevisionIdAny });
    return card.has_value() && IsRdna3(card->generation);
}

// A display adapter of this system is AMD RDNA 3
inline bool SystemHasRdna3()
{
    static const bool found = []
    {
        DISPLAY_DEVICEA device {};

        for (DWORD i = 0;; i++)
        {
            device = {};
            device.cb = sizeof(device);

            if (!EnumDisplayDevicesA(nullptr, i, &device, 0))
                return false;

            if (IsRdna3PciId(device.DeviceID))
                return true;
        }
    }();

    return found;
}

// A d4r backend folder: d4r's loader keeps its d4r.ini next to its _nvngx.dll
inline bool IsD4rFolder(const std::wstring& folder)
{
    if (folder.empty())
        return false;

    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(folder) / L"d4r.ini", ec);
}
} // namespace IlionD4rGuard
