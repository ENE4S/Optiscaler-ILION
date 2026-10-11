#pragma once

#include "SysUtils.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <inputs/FfxApi_Dx12.h>
#include <inputs/FfxApi_Vk.h>

#include <fsr4/FSR4ModelSelection.h>
#include <fsr4/FSR4VisualVerify.h>
#include <fsr4/FSR4Versions.h>
#include <menu/ilion_lang.h>

#include "ffx_api.h"
#include <detours/detours.h>
#include <ffx_framegeneration.h>
#include <ffx_upscale.h>

#include <magic_enum.hpp>
#include <mutex>
#include <array>
#include <atomic>

// A mess to be able to import both
#define FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_WAITCALLBACK FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_WAITCALLBACK_DX12
#define FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING_DX12

#include <dx12/ffx_api_dx12.h>

#undef FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_WAITCALLBACK
#undef FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING

#define FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_WAITCALLBACK FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_WAITCALLBACK_VK
#define FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING_VK

#include <vk/ffx_api_vk.h>
#include <imgui/ImGuiNotify.hpp>

#undef FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_WAITCALLBACK
#undef FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING

enum class FFXStructType
{
    General,
    Upscaling,
    FG,
    SwapchainDX12,
    SwapchainVulkan,
    Denoiser,
    RadianceCache,
    Unknown,
};

struct FfxModule
{
    HMODULE dll = nullptr;
    feature_version version { 0, 0, 0 };
    std::wstring filePath;

    bool skipCreateCalls = false;
    bool skipConfigureCalls = false;
    bool skipQueryCalls = false;
    bool skipDispatchCalls = false;

    bool isLoader = false;
    bool hooked = false;

    PfnFfxCreateContext CreateContext = nullptr;
    PfnFfxDestroyContext DestroyContext = nullptr;
    PfnFfxConfigure Configure = nullptr;
    PfnFfxQuery Query = nullptr;
    PfnFfxDispatch Dispatch = nullptr;
};

class FfxApiProxy
{
  private:
    inline static FfxModule main_dx12;
    inline static FfxModule upscaling_dx12;
    inline static FfxModule fg_dx12;
    inline static FfxModule denoiser_dx12;
    inline static FfxModule radiance_dx12;
    inline static FfxModule main_vk;

    inline static FfxModule main_dx12_hooked;
    inline static FfxModule upscaling_dx12_hooked;
    inline static FfxModule fg_dx12_hooked;
    inline static FfxModule denoiser_dx12_hooked;
    inline static FfxModule radiance_dx12_hooked;
    inline static FfxModule main_vk_hooked;

    inline static ankerl::unordered_dense::map<ffxContext, FFXStructType> contextToType;

    inline static bool _skipDestroyCalls = false;

    // FSR upscaler dll variants
    // Every upscaler dll that got loaded stays loaded, switching only changes which one is upscaling_dx12.
    // Contexts remember the module that created them as the old feature gets destroyed after the switch.
    struct SrContextFns
    {
        PfnFfxDestroyContext DestroyContext = nullptr;
        PfnFfxConfigure Configure = nullptr;
        PfnFfxQuery Query = nullptr;
        PfnFfxDispatch Dispatch = nullptr;

        // Dll that created the context, only read by the visual verification
        HMODULE owner = nullptr;
    };

    inline static std::mutex _srMutex;
    inline static std::vector<FfxModule> _srLoaded;
    inline static ankerl::unordered_dense::map<ffxContext, SrContextFns> _srContexts;
    inline static std::optional<std::wstring> _srPending;
    inline static std::atomic<bool> _srSwitchLocked { false };

    // Dll that was in use before the last switch, kept until the new one has created a context
    inline static std::optional<FfxModule> _srRevertTarget;

    static bool IsSamePath(const std::wstring& a, const std::wstring& b)
    {
        auto pathA = std::filesystem::path(a).lexically_normal().wstring();
        auto pathB = std::filesystem::path(b).lexically_normal().wstring();

        return _wcsicmp(pathA.c_str(), pathB.c_str()) == 0;
    }

    static void RegisterSrContext(ffxContext* context)
    {
        if (context == nullptr || *context == nullptr)
            return;

        std::scoped_lock lock(_srMutex);
        _srContexts[*context] = { upscaling_dx12.DestroyContext, upscaling_dx12.Configure, upscaling_dx12.Query,
                                  upscaling_dx12.Dispatch, upscaling_dx12.dll };
    }

    static std::optional<SrContextFns> FindSrContext(ffxContext* context, bool remove = false)
    {
        if (context == nullptr || *context == nullptr)
            return std::nullopt;

        std::scoped_lock lock(_srMutex);

        auto it = _srContexts.find(*context);
        if (it == _srContexts.end())
            return std::nullopt;

        auto result = it->second;

        if (remove)
            _srContexts.erase(it);

        return result;
    }

    // Hot-swap diagnostics, observation only
    // A slot is created per distinct ffxDispatch pointer that was actually called for an upscale dispatch.
    // The pointer is resolved to the module that owns the code it leads to, not to what was selected or loaded.
    struct SrDiagSlot
    {
        std::atomic<void*> fn { nullptr };
        HMODULE module = nullptr;
        std::wstring path;
        std::string name;
        bool viaTrampoline = false;
        std::atomic<uint64_t> dispatchCount { 0 };
        std::atomic<uint64_t> errorCount { 0 };
    };

    inline static std::array<SrDiagSlot, 16> _srDiagSlots;
    inline static std::atomic<int> _srDiagSlotCount { 0 };
    inline static std::atomic<int> _srDiagLastSlot { -1 };
    inline static std::atomic<uint64_t> _srDiagLastDispatchTick { 0 };
    inline static std::atomic<uint64_t> _srDiagRequestTick { 0 };
    inline static std::atomic<int> _srDiagLastResult { 0 };

    // Rarely written text, guarded by _srDiagMutex
    inline static std::mutex _srDiagMutex;
    inline static bool _srDiagSelectedSet = false;
    inline static std::string _srDiagSelectedName;
    inline static std::wstring _srDiagSelectedPath;
    inline static std::string _srDiagSwitchResult;
    inline static std::string _srDiagObservedSwitch;
    inline static std::string _srDiagCreateResult;
    inline static std::string _srDiagDriverProvider;

    static std::wstring SrDiagModulePath(HMODULE module)
    {
        if (module == nullptr)
            return L"";

        wchar_t modulePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(module, modulePath, MAX_PATH);

        return std::wstring(modulePath);
    }

    // Variant folder name for a dll path, based on the path only
    static std::string SrDiagNameFromPath(const std::wstring& dllPath)
    {
        if (dllPath.empty())
            return "Unknown";

        auto path = std::filesystem::path(dllPath);
        auto optiPath = std::filesystem::path(Config::Instance()->MainDllPath.value_or(L""));

        if (IsSamePath(path.parent_path().wstring(), optiPath.wstring()))
            return "Default";

        if (const auto versionName = FSR4Versions::NameFromPath(dllPath); !versionName.empty())
            return wstring_to_string(versionName);

        return "Custom path";
    }

    // Function pointers of a dll that got hooked by us point to a Detours trampoline instead of the dll.
    // x64 trampoline layout: rbCode[30], cbCode, cbCodeBreak, rbRestore[30], cbRestore, cbRestoreBreak, rAlign[8],
    // pbRemain (rest of the original function), pbDetour (our hook).
    // Only trusted when pbDetour is inside OptiScaler and pbRemain is inside a loaded module.
    static HMODULE SrDiagModuleFromTrampoline(void* fn)
    {
        constexpr size_t remainOffset = 72;
        constexpr size_t detourOffset = 80;

        MEMORY_BASIC_INFORMATION mbi {};
        if (VirtualQuery(fn, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT || mbi.Type == MEM_IMAGE ||
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
        {
            return nullptr;
        }

        auto start = (const BYTE*) fn;
        auto regionEnd = (const BYTE*) mbi.BaseAddress + mbi.RegionSize;

        if (start + detourOffset + sizeof(void*) > regionEnd)
            return nullptr;

        auto remain = *(void* const*) (start + remainOffset);
        auto detour = *(void* const*) (start + detourOffset);

        if (remain == nullptr || detour == nullptr || Util::GetCallerModule(detour) != dllModule)
            return nullptr;

        auto module = Util::GetCallerModule(remain);

        return module != dllModule ? module : nullptr;
    }

    static int SrDiagSlotFor(void* fn)
    {
        const int count = _srDiagSlotCount.load(std::memory_order_acquire);

        for (int i = 0; i < count; i++)
        {
            if (_srDiagSlots[i].fn.load(std::memory_order_relaxed) == fn)
                return i;
        }

        std::scoped_lock lock(_srDiagMutex);

        const int lockedCount = _srDiagSlotCount.load(std::memory_order_acquire);

        for (int i = count; i < lockedCount; i++)
        {
            if (_srDiagSlots[i].fn.load(std::memory_order_relaxed) == fn)
                return i;
        }

        if (lockedCount >= (int) _srDiagSlots.size())
            return -1;

        auto& slot = _srDiagSlots[lockedCount];

        slot.module = Util::GetCallerModule(fn);

        if (slot.module == nullptr)
        {
            slot.module = SrDiagModuleFromTrampoline(fn);
            slot.viaTrampoline = slot.module != nullptr;
        }

        slot.path = SrDiagModulePath(slot.module);
        slot.name = SrDiagNameFromPath(slot.path);
        slot.fn.store(fn, std::memory_order_relaxed);

        if (slot.module != nullptr)
        {
            LOG_INFO("[SR-DIAG] ffxDispatch {:X} belongs to module {:X} ({}), resolved by {}", (size_t) fn,
                     (size_t) slot.module, wstring_to_string(slot.path),
                     slot.viaTrampoline ? "detour trampoline target" : "code address");
        }
        else
        {
            LOG_ERROR("[SR-DIAG] ffxDispatch {:X} can't be resolved to a module", (size_t) fn);
        }

        _srDiagSlotCount.store(lockedCount + 1, std::memory_order_release);

        return lockedCount;
    }

    // Called after every upscale dispatch with the pointer that was called
    static void SrDiagOnDispatch(void* fn, ffxReturnCode_t result)
    {
        const int slot = SrDiagSlotFor(fn);

        if (slot >= 0)
        {
            _srDiagSlots[slot].dispatchCount.fetch_add(1, std::memory_order_relaxed);

            if (result != FFX_API_RETURN_OK)
                _srDiagSlots[slot].errorCount.fetch_add(1, std::memory_order_relaxed);
        }

        _srDiagLastDispatchTick.store(GetTickCount64(), std::memory_order_relaxed);

        // Logged only when the result changes, not per frame
        if (_srDiagLastResult.exchange((int) result, std::memory_order_relaxed) != (int) result &&
            result != FFX_API_RETURN_OK)
        {
            LOG_ERROR("[SR-DIAG] upscale dispatch failed: {} ({})", ReturnCodeToString(result),
                      slot >= 0 ? wstring_to_string(_srDiagSlots[slot].path) : std::string("unknown module"));
        }

        const int previous = _srDiagLastSlot.exchange(slot, std::memory_order_relaxed);

        if (previous == slot)
            return;

        const auto describe = [](int index)
        {
            if (index < 0)
                return std::string("none");

            return _srDiagSlots[index].module != nullptr ? _srDiagSlots[index].name : std::string("unresolved");
        };

        auto observed = std::format("{} -> {}", describe(previous), describe(slot));

        LOG_INFO("[SR-DIAG] first upscale dispatch executed by {} ({}), previous: {}", describe(slot),
                 slot >= 0 ? wstring_to_string(_srDiagSlots[slot].path) : std::string(""), describe(previous));

        std::scoped_lock lock(_srDiagMutex);
        _srDiagObservedSwitch = observed;
    }

    // Calls the given ffxDispatch of an upscaler dll and feeds the diagnostics with what was really called.
    // owner is the dll that created the context, nullptr when that isn't known.
    static ffxReturnCode_t DispatchSrObserved(PfnFfxDispatch dispatchFn, HMODULE owner, ffxContext* context,
                                              const ffxDispatchDescHeader* desc)
    {
        if (desc->type != FFX_API_DISPATCH_DESC_TYPE_UPSCALE)
            return dispatchFn(context, desc);

        if (!FSR4VisualVerify::Enabled())
        {
            const auto result = dispatchFn(context, desc);
            SrDiagOnDispatch((void*) dispatchFn, result);
            return result;
        }

        const auto upscaleDesc = (const ffxDispatchDescUpscale*) desc;

        // Module that owns the code of the function that's about to be called
        const int slot = SrDiagSlotFor((void*) dispatchFn);
        const HMODULE execModule = slot >= 0 ? _srDiagSlots[slot].module : nullptr;
        const std::string execName = execModule != nullptr ? _srDiagSlots[slot].name : std::string("Unresolved");

        FSR4VisualVerify::Begin(upscaleDesc->commandList, execModule);

        const auto result = dispatchFn(context, desc);

        FSR4VisualVerify::End(upscaleDesc->commandList, result == FFX_API_RETURN_OK, execModule, owner, execName,
                              (ID3D12Resource*) upscaleDesc->output.resource,
                              upscaleDesc->output.state == FFX_API_RESOURCE_STATE_UNORDERED_ACCESS,
                              upscaleDesc->upscaleSize.width, upscaleDesc->upscaleSize.height);

        SrDiagOnDispatch((void*) dispatchFn, result);

        return result;
    }

    static void SrDiagSetSwitchResult(const std::string& from, const std::string& to, const std::string& result)
    {
        LOG_INFO("[SR-DIAG] switch {} -> {}: {}", from, to, result);

        std::scoped_lock lock(_srDiagMutex);
        _srDiagSwitchResult = std::format("{} -> {}: {}", from, to, result);
    }

    static void SrDiagOnCreate(ffxReturnCode_t result)
    {
        auto path = Dx12Module_SR_Path();
        auto text = std::format("{} with {}", ReturnCodeToString(result), SrDiagNameFromPath(path));

        if (result == FFX_API_RETURN_OK)
            LOG_INFO("[SR-DIAG] upscale context created with {}", wstring_to_string(path));
        else
            LOG_ERROR("[SR-DIAG] upscale context creation failed: {}, dll: {}", ReturnCodeToString(result),
                      wstring_to_string(path));

        std::scoped_lock lock(_srDiagMutex);
        _srDiagCreateResult = text;
    }

    static bool IsLoader(const std::wstring& filePath)
    {
        auto size = std::filesystem::file_size(filePath);

        // < 1 MB
        return size < 1048576;
    }

  public:
    static HMODULE Dx12Module() { return main_dx12.dll; }
    static HMODULE Dx12Module_SR() { return upscaling_dx12.dll; }
    static HMODULE Dx12Module_FG() { return fg_dx12.dll; }
    static HMODULE Dx12Module_Denoiser() { return denoiser_dx12.dll; }
    static HMODULE Dx12Module_Radiance() { return radiance_dx12.dll; }

    static std::wstring Dx12Module_Path() { return main_dx12.filePath; }
    static std::wstring Dx12Module_SR_Path()
    {
        std::scoped_lock lock(_srMutex);
        return upscaling_dx12.filePath;
    }
    static std::wstring Dx12Module_FG_Path() { return fg_dx12.filePath; }
    static std::wstring Dx12Module_Denoiser_Path() { return denoiser_dx12.filePath; }
    static std::wstring Dx12Module_Radiance_Path() { return radiance_dx12.filePath; }

    static bool IsFGReady(bool sendNotification = true)
    {
        bool result = (main_dx12.dll && !main_dx12.isLoader) || fg_dx12.dll != nullptr;

        if (!result && sendNotification)
            ImGui::InsertNotification({ ImGuiToastType::Error, 10000,
                                        "Can't load amd_fidelityfx_dx12\nDid you forget to extract that dll?" });

        return result;
    }

    static bool IsSRReady(bool sendNotification = true)
    {
        bool result = (main_dx12.dll && !main_dx12.isLoader) || upscaling_dx12.dll != nullptr;

        if (!result && sendNotification)
            ImGui::InsertNotification({ ImGuiToastType::Error, 10000,
                                        "Can't load amd_fidelityfx_dx12\nDid you forget to extract that dll?" });

        return result;
    }

    static bool IsDenoiserReady(bool sendNotification = true)
    {
        bool result = (main_dx12.dll && !main_dx12.isLoader) || denoiser_dx12.dll != nullptr;

        if (!result && sendNotification)
            ImGui::InsertNotification({ ImGuiToastType::Error, 10000,
                                        "Can't load amd_fidelityfx_dx12\nDid you forget to extract that dll?" });

        return result;
    }

    static bool IsRadianceReady(bool sendNotification = true)
    {
        bool result = (main_dx12.dll && !main_dx12.isLoader) || radiance_dx12.dll != nullptr;

        if (!result && sendNotification)
            ImGui::InsertNotification({ ImGuiToastType::Error, 10000,
                                        "Can't load amd_fidelityfx_dx12\nDid you forget to extract that dll?" });

        return result;
    }

    // Versions found by FSR4Versions, every folder with its own amd_fidelityfx_upscaler_dx12.dll
    using SrVariant = FSR4Versions::Entry;

    static std::vector<SrVariant> SrVariants() { return FSR4Versions::Scan(); }

    // Empty name is the dll in OptiDllPath, returns empty when the dll doesn't exist
    static std::wstring SrVariantDllPath(const std::wstring& name)
    {
        if (!name.empty())
            return FSR4Versions::DllPathFor(name);

        std::error_code ec;
        auto dllPath = std::filesystem::path(Config::Instance()->MainDllPath.value_or(L""));

        dllPath /= L"amd_fidelityfx_upscaler_dx12.dll";

        if (!std::filesystem::exists(dllPath, ec))
            return L"";

        return dllPath.wstring();
    }

    static bool IsActiveSrDll(const std::wstring& dllPath) { return IsSamePath(dllPath, Dx12Module_SR_Path()); }

    struct SrDiagModule
    {
        HMODULE module = nullptr;
        std::string name;
        std::wstring path;
        uint64_t dispatchCount = 0;
        uint64_t errorCount = 0;
    };

    struct SrDiagSnapshot
    {
        std::string selectedName;
        std::wstring selectedPath;

        // Module that executed the most recent upscale dispatch
        std::string activeName = "-";
        std::wstring activePath;
        std::string resolvedBy = "-";
        uint64_t activeDispatchCount = 0;
        uint64_t activeErrorCount = 0;

        // 0 = verified, 1 = can't tell, 2 = mismatch
        int statusLevel = 1;
        std::string status;

        std::vector<SrDiagModule> modules;

        std::string switchResult;
        std::string observedSwitch;
        std::string createResult;
        std::string driverProvider;
    };

    static SrDiagSnapshot SrDiagGetSnapshot()
    {
        SrDiagSnapshot snapshot;
        const auto now = GetTickCount64();

        {
            std::scoped_lock lock(_srDiagMutex);

            // Nothing was selected in this session yet, selection is what the config asked for at startup
            if (!_srDiagSelectedSet)
            {
                std::error_code ec;
                auto overridePath = Config::Instance()->FfxDx12SRPath.value_or(L"");

                if (!overridePath.empty())
                {
                    auto path = std::filesystem::absolute(overridePath, ec);

                    if (std::filesystem::is_directory(path, ec))
                        path /= L"amd_fidelityfx_upscaler_dx12.dll";

                    _srDiagSelectedName = "FfxDx12SRPath";
                    _srDiagSelectedPath = path.wstring();
                }
                else
                {
                    auto name = Config::Instance()->Fsr4SrVariant.value_or(L"");

                    _srDiagSelectedName = name.empty() ? "Default" : wstring_to_string(name);
                    _srDiagSelectedPath = SrVariantDllPath(name);
                }

                _srDiagSelectedSet = true;
            }

            snapshot.selectedName = _srDiagSelectedName;
            snapshot.selectedPath = _srDiagSelectedPath;
            snapshot.switchResult = _srDiagSwitchResult;
            snapshot.observedSwitch = _srDiagObservedSwitch;
            snapshot.createResult = _srDiagCreateResult;
            snapshot.driverProvider = _srDiagDriverProvider;
        }

        // A hooked and a not hooked pointer can lead to the same dll, merge by module
        const int count = _srDiagSlotCount.load(std::memory_order_acquire);

        for (int i = 0; i < count; i++)
        {
            const auto& slot = _srDiagSlots[i];
            SrDiagModule* entry = nullptr;

            for (auto& existing : snapshot.modules)
            {
                if (existing.module == slot.module)
                    entry = &existing;
            }

            if (entry == nullptr)
            {
                snapshot.modules.push_back(
                    { slot.module, slot.module != nullptr ? slot.name : "Unresolved", slot.path, 0, 0 });
                entry = &snapshot.modules.back();
            }

            entry->dispatchCount += slot.dispatchCount.load(std::memory_order_relaxed);
            entry->errorCount += slot.errorCount.load(std::memory_order_relaxed);
        }

        const int last = _srDiagLastSlot.load(std::memory_order_relaxed);
        const auto lastTick = _srDiagLastDispatchTick.load(std::memory_order_relaxed);

        if (lastTick == 0)
        {
            snapshot.status = "UNVERIFIED (no upscale dispatch seen yet)";
            return snapshot;
        }

        if (last < 0 || _srDiagSlots[last].module == nullptr)
        {
            snapshot.status = "UNVERIFIED (dispatch function can't be traced to a dll)";
            return snapshot;
        }

        const auto& slot = _srDiagSlots[last];

        snapshot.activeName = slot.name;
        snapshot.activePath = slot.path;
        snapshot.resolvedBy = slot.viaTrampoline ? "hook trampoline target" : "code address";

        for (const auto& entry : snapshot.modules)
        {
            if (entry.module == slot.module)
            {
                snapshot.activeDispatchCount = entry.dispatchCount;
                snapshot.activeErrorCount = entry.errorCount;
            }
        }

        if (now - lastTick > 1000)
        {
            snapshot.status = "UNVERIFIED (no upscale dispatch in the last second)";
        }
        else if (snapshot.selectedPath.empty())
        {
            snapshot.status = "UNVERIFIED (selected dll not found)";
        }
        else if (IsSamePath(snapshot.selectedPath, snapshot.activePath))
        {
            snapshot.status = "ACTIVE";
            snapshot.statusLevel = 0;
        }
        else if (now - _srDiagRequestTick.load(std::memory_order_relaxed) < 2000)
        {
            snapshot.status = "SWITCHING (waiting for a dispatch with the selected dll)";
        }
        else
        {
            snapshot.status = "MISMATCH";
            snapshot.statusLevel = 2;
        }

        return snapshot;
    }

    // Dll that executed the most recent upscale dispatch, read only and lock free.
    // dispatchCount / errorCount are the totals of the function that was called, they move with every dispatch.
    struct SrDiagActive
    {
        HMODULE module = nullptr;
        const char* name = nullptr;
        int slot = -1;
        uint64_t dispatchCount = 0;
        uint64_t errorCount = 0;
        uint64_t requestTick = 0;
    };

    static SrDiagActive SrDiagGetActive()
    {
        SrDiagActive active;
        active.requestTick = _srDiagRequestTick.load(std::memory_order_relaxed);
        active.slot = _srDiagLastSlot.load(std::memory_order_relaxed);

        if (active.slot < 0 || active.slot >= _srDiagSlotCount.load(std::memory_order_acquire))
        {
            active.slot = -1;
            return active;
        }

        // Slot fields other than the counters never change after the slot got published
        const auto& slot = _srDiagSlots[active.slot];

        active.module = slot.module;
        active.name = slot.name.c_str();
        active.dispatchCount = slot.dispatchCount.load(std::memory_order_relaxed);
        active.errorCount = slot.errorCount.load(std::memory_order_relaxed);

        return active;
    }

    // Variant name and path of a loaded module, for display
    static std::string SrDiagDescribeModule(HMODULE module, std::wstring* path = nullptr)
    {
        auto modulePath = SrDiagModulePath(module);

        if (path != nullptr)
            *path = modulePath;

        if (module == nullptr)
            return "-";

        auto name = SrDiagNameFromPath(modulePath);

        // Not one of the upscaler dll locations, show the file name instead
        if (name == "Custom path")
            return wstring_to_string(std::filesystem::path(modulePath).filename().wstring());

        return name;
    }

    // Result of the driver's (amdxcffx64) provider update asked by an upscaler dll
    static void SrDiagOnDriverProvider(HMODULE sdkModule, HRESULT result)
    {
        auto text = std::format("{}: 0x{:X} ({})", SrDiagNameFromPath(SrDiagModulePath(sdkModule)), (UINT) result,
                                SUCCEEDED(result) ? "succeeded" : "failed");

        std::scoped_lock lock(_srDiagMutex);

        if (text == _srDiagDriverProvider)
            return;

        LOG_INFO("[SR-DIAG] amdxcffx64 provider update for upscaler dll {}", text);
        _srDiagDriverProvider = text;
    }

    static void SrDiagOnRequest(const std::wstring& name)
    {
        auto displayName = name.empty() ? std::string("Default") : wstring_to_string(name);
        auto path = SrVariantDllPath(name);

        LOG_INFO("[SR-DIAG] switch requested: {} ({})", displayName,
                 path.empty() ? std::string("dll not found") : wstring_to_string(path));

        _srDiagRequestTick.store(GetTickCount64(), std::memory_order_relaxed);

        std::scoped_lock lock(_srDiagMutex);
        _srDiagSelectedSet = true;
        _srDiagSelectedName = displayName;
        _srDiagSelectedPath = path;
    }

    // While set, switching the upscaler dll is refused. Used by the benchmark, a test belongs to one dll.
    static void SetSrSwitchLocked(bool locked) { _srSwitchLocked.store(locked, std::memory_order_relaxed); }
    static bool SrSwitchLocked() { return _srSwitchLocked.load(std::memory_order_relaxed); }

    // Applied by ApplyPendingSrVariant when the next FFX feature is created
    static void RequestSrVariant(const std::wstring& name)
    {
        if (SrSwitchLocked())
        {
            LOG_WARN("Upscaler dll switch refused, a benchmark is running");
            return;
        }

        SrDiagOnRequest(name);

        std::scoped_lock lock(_srMutex);
        _srPending = name;
    }

    static void ApplyPendingSrVariant()
    {
        std::optional<std::wstring> pending;

        {
            std::scoped_lock lock(_srMutex);
            pending.swap(_srPending);
        }

        if (!pending.has_value())
            return;

        const auto diagFrom = SrDiagNameFromPath(upscaling_dx12.filePath);
        const auto diagTo = pending->empty() ? std::string("Default") : wstring_to_string(*pending);

        // Nothing loaded yet, InitFfxDx12_SR will use the variant from the config
        if (upscaling_dx12.dll == nullptr)
        {
            SrDiagSetSwitchResult("none", diagTo, "deferred, no upscaler dll is loaded yet");
            return;
        }

        auto variantName = pending->empty() ? std::string("default") : wstring_to_string(*pending);
        auto targetPath = SrVariantDllPath(*pending);

        if (targetPath.empty())
        {
            LOG_ERROR("Can't find amd_fidelityfx_upscaler_dx12.dll for variant: {}", variantName);
            ImGui::InsertNotification(
                { ImGuiToastType::Error, 10000, Ilion::Tr("Can't find the dll of the selected FSR variant") });
            SrDiagSetSwitchResult(diagFrom, diagTo, "FAILED, dll not found");
            return;
        }

        if (IsSamePath(targetPath, upscaling_dx12.filePath))
        {
            SrDiagSetSwitchResult(diagFrom, diagTo, "already in use, nothing changed");
            return;
        }

        FfxModule target {};

        {
            std::scoped_lock lock(_srMutex);

            for (const auto& loaded : _srLoaded)
            {
                if (IsSamePath(targetPath, loaded.filePath))
                {
                    target = loaded;
                    break;
                }
            }
        }

        if (target.dll == nullptr)
        {
            // Checked from the file, a dll that isn't a 64 bit FSR upscaler never gets loaded
            if (std::string problem; !FSR4Versions::Validate(targetPath, &problem))
            {
                LOG_ERROR("{} can't be used: {}", wstring_to_string(targetPath), problem);
                ImGui::InsertNotification(
                    { ImGuiToastType::Error, 10000, Ilion::Tr("Selected FSR version isn't a compatible upscaler dll") });
                SrDiagSetSwitchResult(diagFrom, diagTo, "FAILED, " + problem);
                return;
            }

            target.dll = NtdllProxy::LoadLibraryExW_Ldr(targetPath.c_str(), NULL, 0);

            if (target.dll == nullptr)
            {
                LOG_ERROR("Can't load {}", wstring_to_string(targetPath));
                ImGui::InsertNotification(
                    { ImGuiToastType::Error, 10000, Ilion::Tr("Can't load the dll of the selected FSR variant") });
                SrDiagSetSwitchResult(diagFrom, diagTo, "FAILED, dll can't be loaded");
                return;
            }

            wchar_t modulePath[MAX_PATH];
            GetModuleFileNameW(target.dll, modulePath, MAX_PATH);
            target.filePath = std::wstring(modulePath);

            target.Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(target.dll, "ffxConfigure");
            target.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(target.dll, "ffxCreateContext");
            target.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(target.dll, "ffxDestroyContext");
            target.Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(target.dll, "ffxDispatch");
            target.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(target.dll, "ffxQuery");

            if (target.CreateContext == nullptr || target.DestroyContext == nullptr || target.Configure == nullptr ||
                target.Dispatch == nullptr || target.Query == nullptr)
            {
                LOG_ERROR("Missing ffx exports in {}", wstring_to_string(target.filePath));
                ImGui::InsertNotification(
                    { ImGuiToastType::Error, 10000, Ilion::Tr("Selected FSR variant is not a valid upscaler dll") });
                SrDiagSetSwitchResult(diagFrom, diagTo, "FAILED, dll is missing ffx exports");
                return;
            }

            LOG_INFO("Variant {} loaded from {}, module: {:X}", variantName, wstring_to_string(target.filePath),
                     (size_t) target.dll);
        }

        // Model selection hooks are per source, move them to the module that's going to be used
        FSR4ModelSelection::Hook(target.dll, FSR4Source::SDK, true);

        {
            std::scoped_lock lock(_srMutex);

            bool currentStored = false;

            for (const auto& loaded : _srLoaded)
                currentStored = currentStored || loaded.dll == upscaling_dx12.dll;

            if (!currentStored)
                _srLoaded.push_back(upscaling_dx12);

            LOG_INFO("Switching upscaler dll: {} -> {}", wstring_to_string(upscaling_dx12.filePath),
                     wstring_to_string(target.filePath));

            _srRevertTarget = upscaling_dx12;
            upscaling_dx12 = target;
        }

        // Only tells that the function table was switched, execution is confirmed by SrDiagOnDispatch
        SrDiagSetSwitchResult(diagFrom, diagTo, "function table switched");
    }

    // The dll that was just switched to couldn't create a context. Nothing of it is in use at this point,
    // so the previous dll becomes the active one again. Both stay loaded.
    static void RevertSrVariantAfterFailure()
    {
        if (!_srRevertTarget.has_value())
            return;

        const FfxModule previous = *_srRevertTarget;
        _srRevertTarget.reset();

        const auto failedName = SrDiagNameFromPath(upscaling_dx12.filePath);
        const auto previousName = SrDiagNameFromPath(previous.filePath);

        FSR4ModelSelection::Hook(previous.dll, FSR4Source::SDK, true);

        {
            std::scoped_lock lock(_srMutex);

            bool failedStored = false;

            for (const auto& loaded : _srLoaded)
                failedStored = failedStored || loaded.dll == upscaling_dx12.dll;

            if (!failedStored)
                _srLoaded.push_back(upscaling_dx12);

            upscaling_dx12 = previous;
        }

        // Selection follows what's really in use again
        const auto previousVersion = FSR4Versions::NameFromPath(previous.filePath);

        if (previousVersion.empty())
            Config::Instance()->Fsr4SrVariant.reset();
        else
            Config::Instance()->Fsr4SrVariant = previousVersion;

        {
            std::scoped_lock lock(_srDiagMutex);
            _srDiagSelectedSet = true;
            _srDiagSelectedName = previousName;
            _srDiagSelectedPath = previous.filePath;
        }

        LOG_ERROR("FSR version {} couldn't create a context, {} is active again", failedName, previousName);
        ImGui::InsertNotification({ ImGuiToastType::Error, 10000,
                                    Ilion::Tr("Selected FSR version failed to start, the previous one was restored.\n"
                                              "Select FSR again to keep using it.") });

        SrDiagSetSwitchResult(failedName, previousName, "REVERTED, the new dll couldn't create a context");
    }

    static FFXStructType GetType(ffxStructType_t type)
    {
        switch (type & FFX_API_EFFECT_MASK) // type without the specific effect
        {
        case FFX_API_EFFECT_ID_GENERAL:
            return FFXStructType::General;

        case FFX_API_EFFECT_ID_UPSCALE:
            return FFXStructType::Upscaling;

        case FFX_API_EFFECT_ID_FRAMEGENERATION:
            return FFXStructType::FG;

        case FFX_API_EFFECT_ID_FRAMEGENERATIONSWAPCHAIN_DX12:
            return FFXStructType::SwapchainDX12;

        case FFX_API_EFFECT_ID_FGSC_VK:
            return FFXStructType::SwapchainVulkan;

        case 0x00050000u:
            return FFXStructType::Denoiser;

        case 0x00060000u:
            return FFXStructType::RadianceCache;

        default:
            return FFXStructType::Unknown;
        }
    }

    // Can't directly check for type when query is used
    // might apply to FFX_API_DESC_TYPE_OVERRIDE_VERSION as well
    static FFXStructType GetIndirectType(ffxQueryDescHeader* header)
    {
        ffxStructType_t type = header->type;

        if (header->type == FFX_API_QUERY_DESC_TYPE_GET_VERSIONS ||
            header->type == FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION)
        {
            type = header[1].type;
        }

        return GetType(type);
    }

    static bool InitFfxDx12(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (main_dx12.dll != nullptr && main_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        // This is module loaded by game, need to hook it to use inputs
        if (module != nullptr && main_dx12_hooked.dll == nullptr)
        {
            main_dx12_hooked.dll = module;
        }

        // Try loading Opti's dlls first
        if (main_dx12.dll == nullptr)
        {
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_loader_dx12.dll", L"amd_fidelityfx_dx12.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->FfxDx12Path.value_or(L"");

                if (main_dx12_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &main_dx12_hooked.dll, &main_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &main_dx12.dll);
                }

                if (main_dx12.dll != nullptr)
                {
                    break;
                }
            }
        }

        // Can't find Opti dlls, use just loaded module
        if (main_dx12.dll == nullptr && main_dx12_hooked.dll != nullptr)
        {
            main_dx12 = main_dx12_hooked;
        }

        if (main_dx12.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(main_dx12.dll, modulePath, MAX_PATH);
            main_dx12.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(main_dx12.filePath));

            // hacky but works for now
            main_dx12.isLoader = IsLoader(main_dx12.filePath);

            if (!main_dx12.isLoader)
            {
                FSR4ModelSelection::Hook(main_dx12.dll, FSR4Source::SDK);
            }
        }

        // Get methods of Opti's dlls
        if (main_dx12.dll != nullptr && main_dx12.Configure == nullptr)
        {
            main_dx12.Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxConfigure");
            main_dx12.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxCreateContext");
            main_dx12.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxDestroyContext");
            main_dx12.Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxDispatch");
            main_dx12.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(main_dx12.dll, "ffxQuery");
        }

        // Get methods of game's dlls
        if (main_dx12_hooked.dll != nullptr && main_dx12_hooked.Configure == nullptr)
        {
            main_dx12_hooked.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(main_dx12_hooked.dll, "ffxConfigure");
            main_dx12_hooked.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(main_dx12_hooked.dll, "ffxCreateContext");
            main_dx12_hooked.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(main_dx12_hooked.dll, "ffxDestroyContext");
            main_dx12_hooked.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(main_dx12_hooked.dll, "ffxDispatch");
            main_dx12_hooked.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(main_dx12_hooked.dll, "ffxQuery");
        }
        else if (main_dx12_hooked.Configure == nullptr)
        {
            main_dx12_hooked = main_dx12;
        }

        // Hook game's dlls
        if (Config::Instance()->EnableFfxInputs.value_or_default() && main_dx12_hooked.dll != nullptr &&
            main_dx12_hooked.CreateContext != nullptr && !main_dx12_hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (main_dx12_hooked.Configure != nullptr)
                DetourAttach(&(PVOID&) main_dx12_hooked.Configure, ffxConfigure_Dx12);

            if (main_dx12_hooked.CreateContext != nullptr)
                DetourAttach(&(PVOID&) main_dx12_hooked.CreateContext, ffxCreateContext_Dx12);

            if (main_dx12_hooked.DestroyContext != nullptr)
                DetourAttach(&(PVOID&) main_dx12_hooked.DestroyContext, ffxDestroyContext_Dx12);

            if (main_dx12_hooked.Dispatch != nullptr)
                DetourAttach(&(PVOID&) main_dx12_hooked.Dispatch, ffxDispatch_Dx12);

            if (main_dx12_hooked.Query != nullptr)
                DetourAttach(&(PVOID&) main_dx12_hooked.Query, ffxQuery_Dx12);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                main_dx12_hooked.Configure = nullptr;
                main_dx12_hooked.CreateContext = nullptr;
                main_dx12_hooked.DestroyContext = nullptr;
                main_dx12_hooked.Dispatch = nullptr;
                main_dx12_hooked.Query = nullptr;
            }
            else
            {
                // If we are hooking Opti's dlls then we need to use original method ptrs
                if (main_dx12_hooked.dll == main_dx12.dll)
                    main_dx12 = main_dx12_hooked;

                main_dx12_hooked.hooked = true;
                State::Instance().fsrHooks = true;
            }
        }

        InitFfxDx12_SR();
        InitFfxDx12_FG();
        InitFfxDx12_Denoiser();
        InitFfxDx12_Radiance();

        bool loadResult = main_dx12.CreateContext != nullptr || upscaling_dx12.CreateContext != nullptr ||
                          fg_dx12.CreateContext != nullptr || denoiser_dx12.CreateContext != nullptr ||
                          radiance_dx12.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (!loadResult)
            main_dx12.dll = nullptr;

        return loadResult;
    }

    static bool InitFfxDx12_SR(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (upscaling_dx12.dll != nullptr && upscaling_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        // This is module loaded by game, need to hook it to use inputs
        if (module != nullptr && upscaling_dx12_hooked.dll == nullptr)
        {
            upscaling_dx12_hooked.dll = module;
        }

        if (upscaling_dx12.dll == nullptr)
        {
            // Try new api first
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_upscaler_dx12.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->FfxDx12SRPath.value_or(L"");

                if (overridePath.empty() && Config::Instance()->Fsr4SrVariant.has_value())
                {
                    overridePath = SrVariantDllPath(Config::Instance()->Fsr4SrVariant.value());

                    // A dll that doesn't pass the checks isn't loaded, the default one gets used
                    if (std::string problem; !overridePath.empty() && !FSR4Versions::Validate(overridePath, &problem))
                    {
                        LOG_ERROR("Saved FSR version can't be used ({}): {}", problem, wstring_to_string(overridePath));
                        overridePath.clear();
                    }
                }

                if (upscaling_dx12_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &upscaling_dx12_hooked.dll,
                                           &upscaling_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &upscaling_dx12.dll);
                }

                if (upscaling_dx12.dll != nullptr)
                {
                    break;
                }
            }
        }

        // Can't find Opti dlls, use just module loaded by game
        if (upscaling_dx12.dll == nullptr && upscaling_dx12_hooked.dll != nullptr)
        {
            upscaling_dx12 = upscaling_dx12_hooked;
        }

        if (upscaling_dx12.dll != nullptr)
        {
            FSR4ModelSelection::Hook(upscaling_dx12.dll, FSR4Source::SDK);

            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(upscaling_dx12.dll, modulePath, MAX_PATH);
            upscaling_dx12.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(upscaling_dx12.filePath));
        }

        if (upscaling_dx12.dll != nullptr && upscaling_dx12.Configure == nullptr)
        {
            upscaling_dx12.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxConfigure");
            upscaling_dx12.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxCreateContext");
            upscaling_dx12.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxDestroyContext");
            upscaling_dx12.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxDispatch");
            upscaling_dx12.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(upscaling_dx12.dll, "ffxQuery");
        }

        if (upscaling_dx12_hooked.dll != nullptr && upscaling_dx12_hooked.Configure == nullptr)
        {
            upscaling_dx12_hooked.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(upscaling_dx12_hooked.dll, "ffxConfigure");
            upscaling_dx12_hooked.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(upscaling_dx12_hooked.dll, "ffxCreateContext");
            upscaling_dx12_hooked.DestroyContext = (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(
                upscaling_dx12_hooked.dll, "ffxDestroyContext");
            upscaling_dx12_hooked.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(upscaling_dx12_hooked.dll, "ffxDispatch");
            upscaling_dx12_hooked.Query =
                (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(upscaling_dx12_hooked.dll, "ffxQuery");
        }
        else if (upscaling_dx12_hooked.Configure == nullptr)
        {
            upscaling_dx12_hooked = upscaling_dx12;
        }

        if (Config::Instance()->EnableFfxInputs.value_or_default() && upscaling_dx12_hooked.dll != nullptr &&
            upscaling_dx12_hooked.CreateContext != nullptr && !upscaling_dx12_hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (upscaling_dx12_hooked.Configure != nullptr)
                DetourAttach(&(PVOID&) upscaling_dx12_hooked.Configure, ffxConfigure_Dx12);

            if (upscaling_dx12_hooked.CreateContext != nullptr)
                DetourAttach(&(PVOID&) upscaling_dx12_hooked.CreateContext, ffxCreateContext_Dx12);

            if (upscaling_dx12_hooked.DestroyContext != nullptr)
                DetourAttach(&(PVOID&) upscaling_dx12_hooked.DestroyContext, ffxDestroyContext_Dx12);

            if (upscaling_dx12_hooked.Dispatch != nullptr)
                DetourAttach(&(PVOID&) upscaling_dx12_hooked.Dispatch, ffxDispatch_Dx12);

            if (upscaling_dx12_hooked.Query != nullptr)
                DetourAttach(&(PVOID&) upscaling_dx12_hooked.Query, ffxQuery_Dx12);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                upscaling_dx12_hooked.Configure = nullptr;
                upscaling_dx12_hooked.CreateContext = nullptr;
                upscaling_dx12_hooked.DestroyContext = nullptr;
                upscaling_dx12_hooked.Dispatch = nullptr;
                upscaling_dx12_hooked.Query = nullptr;
            }
            else
            {
                if (upscaling_dx12.dll == upscaling_dx12_hooked.dll)
                    upscaling_dx12 = upscaling_dx12_hooked;

                upscaling_dx12_hooked.hooked = true;
                State::Instance().fsrHooks = true;
            }
        }

        bool loadResult = upscaling_dx12.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (!loadResult)
            upscaling_dx12.dll = nullptr;

        return loadResult;
    }

    static bool InitFfxDx12_FG(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (fg_dx12.dll != nullptr && fg_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        // This is module loaded by game, need to hook it to use inputs
        if (module != nullptr && fg_dx12_hooked.dll == nullptr)
        {
            fg_dx12_hooked.dll = module;
        }

        if (fg_dx12.dll == nullptr)
        {
            // Try new api first
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_framegeneration_dx12.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->FfxDx12FGPath.value_or(L"");

                if (fg_dx12_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &fg_dx12_hooked.dll, &fg_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &fg_dx12.dll);
                }

                if (fg_dx12.dll != nullptr)
                {
                    break;
                }
            }
        }

        // Can't find Opti dlls, use just module loaded by game
        if (fg_dx12.dll == nullptr && fg_dx12_hooked.dll != nullptr)
        {
            fg_dx12 = fg_dx12_hooked;
        }

        if (fg_dx12.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(fg_dx12.dll, modulePath, MAX_PATH);
            fg_dx12.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(fg_dx12.filePath));
        }

        if (fg_dx12.dll != nullptr && fg_dx12.Configure == nullptr)
        {
            fg_dx12.Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxConfigure");
            fg_dx12.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxCreateContext");
            fg_dx12.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxDestroyContext");
            fg_dx12.Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxDispatch");
            fg_dx12.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(fg_dx12.dll, "ffxQuery");
        }

        if (fg_dx12_hooked.dll != nullptr && fg_dx12_hooked.Configure == nullptr)
        {
            fg_dx12_hooked.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(fg_dx12_hooked.dll, "ffxConfigure");
            fg_dx12_hooked.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(fg_dx12_hooked.dll, "ffxCreateContext");
            fg_dx12_hooked.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(fg_dx12_hooked.dll, "ffxDestroyContext");
            fg_dx12_hooked.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(fg_dx12_hooked.dll, "ffxDispatch");
            fg_dx12_hooked.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(fg_dx12_hooked.dll, "ffxQuery");
        }
        else if (fg_dx12_hooked.Configure == nullptr)
        {
            fg_dx12_hooked = fg_dx12;
        }

        if (Config::Instance()->EnableFfxInputs.value_or_default() && fg_dx12_hooked.dll != nullptr &&
            fg_dx12_hooked.CreateContext != nullptr && !fg_dx12_hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (fg_dx12_hooked.Configure != nullptr)
                DetourAttach(&(PVOID&) fg_dx12_hooked.Configure, ffxConfigure_Dx12);

            if (fg_dx12_hooked.CreateContext != nullptr)
                DetourAttach(&(PVOID&) fg_dx12_hooked.CreateContext, ffxCreateContext_Dx12);

            if (fg_dx12_hooked.DestroyContext != nullptr)
                DetourAttach(&(PVOID&) fg_dx12_hooked.DestroyContext, ffxDestroyContext_Dx12);

            if (fg_dx12_hooked.Dispatch != nullptr)
                DetourAttach(&(PVOID&) fg_dx12_hooked.Dispatch, ffxDispatch_Dx12);

            if (fg_dx12_hooked.Query != nullptr)
                DetourAttach(&(PVOID&) fg_dx12_hooked.Query, ffxQuery_Dx12);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                fg_dx12.Configure = nullptr;
                fg_dx12.CreateContext = nullptr;
                fg_dx12.DestroyContext = nullptr;
                fg_dx12.Dispatch = nullptr;
                fg_dx12.Query = nullptr;
            }
            else
            {
                if (fg_dx12.dll == fg_dx12_hooked.dll)
                    fg_dx12 = fg_dx12_hooked;

                fg_dx12_hooked.hooked = true;
                State::Instance().fsrHooks = true;
            }
        }

        bool loadResult = fg_dx12.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (!loadResult)
            fg_dx12.dll = nullptr;

        return loadResult;
    }

    static bool InitFfxDx12_Denoiser(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (denoiser_dx12.dll != nullptr && denoiser_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        // This is module loaded by game, need to hook it to use inputs
        if (module != nullptr && denoiser_dx12_hooked.dll == nullptr)
        {
            denoiser_dx12_hooked.dll = module;
        }

        if (denoiser_dx12.dll == nullptr)
        {
            // Try new api first
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_denoiser_dx12.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->FfxDx12RRPath.value_or(L"");

                if (denoiser_dx12_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &denoiser_dx12_hooked.dll,
                                           &denoiser_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &denoiser_dx12.dll);
                }

                if (denoiser_dx12.dll != nullptr)
                {
                    break;
                }
            }
        }

        // Can't find Opti dlls, use just module loaded by game
        if (denoiser_dx12.dll == nullptr && denoiser_dx12_hooked.dll != nullptr)
        {
            denoiser_dx12 = denoiser_dx12_hooked;
        }

        if (denoiser_dx12.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(denoiser_dx12.dll, modulePath, MAX_PATH);
            denoiser_dx12.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(denoiser_dx12.filePath));
        }

        if (denoiser_dx12.dll != nullptr && denoiser_dx12.Configure == nullptr)
        {
            denoiser_dx12.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(denoiser_dx12.dll, "ffxConfigure");
            denoiser_dx12.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(denoiser_dx12.dll, "ffxCreateContext");
            denoiser_dx12.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(denoiser_dx12.dll, "ffxDestroyContext");
            denoiser_dx12.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(denoiser_dx12.dll, "ffxDispatch");
            denoiser_dx12.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(denoiser_dx12.dll, "ffxQuery");
        }

        if (denoiser_dx12_hooked.dll != nullptr && denoiser_dx12_hooked.Configure == nullptr)
        {
            denoiser_dx12_hooked.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(denoiser_dx12_hooked.dll, "ffxConfigure");
            denoiser_dx12_hooked.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(denoiser_dx12_hooked.dll, "ffxCreateContext");
            denoiser_dx12_hooked.DestroyContext = (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(
                denoiser_dx12_hooked.dll, "ffxDestroyContext");
            denoiser_dx12_hooked.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(denoiser_dx12_hooked.dll, "ffxDispatch");
            denoiser_dx12_hooked.Query =
                (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(denoiser_dx12_hooked.dll, "ffxQuery");
        }
        else if (denoiser_dx12_hooked.Configure == nullptr)
        {
            denoiser_dx12_hooked = denoiser_dx12;
        }

        if (Config::Instance()->EnableFfxInputs.value_or_default() && denoiser_dx12_hooked.dll != nullptr &&
            denoiser_dx12_hooked.CreateContext != nullptr && !denoiser_dx12_hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (denoiser_dx12_hooked.Configure != nullptr)
                DetourAttach(&(PVOID&) denoiser_dx12_hooked.Configure, ffxConfigure_Dx12);

            if (denoiser_dx12_hooked.CreateContext != nullptr)
                DetourAttach(&(PVOID&) denoiser_dx12_hooked.CreateContext, ffxCreateContext_Dx12);

            if (denoiser_dx12_hooked.DestroyContext != nullptr)
                DetourAttach(&(PVOID&) denoiser_dx12_hooked.DestroyContext, ffxDestroyContext_Dx12);

            if (denoiser_dx12_hooked.Dispatch != nullptr)
                DetourAttach(&(PVOID&) denoiser_dx12_hooked.Dispatch, ffxDispatch_Dx12);

            if (denoiser_dx12_hooked.Query != nullptr)
                DetourAttach(&(PVOID&) denoiser_dx12_hooked.Query, ffxQuery_Dx12);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                denoiser_dx12.Configure = nullptr;
                denoiser_dx12.CreateContext = nullptr;
                denoiser_dx12.DestroyContext = nullptr;
                denoiser_dx12.Dispatch = nullptr;
                denoiser_dx12.Query = nullptr;
            }
            else
            {
                if (denoiser_dx12.dll == denoiser_dx12_hooked.dll)
                    denoiser_dx12 = denoiser_dx12_hooked;

                denoiser_dx12_hooked.hooked = true;
                State::Instance().fsrHooks = true;
            }
        }

        bool loadResult = denoiser_dx12.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (!loadResult)
            denoiser_dx12.dll = nullptr;

        return loadResult;
    }

    static bool InitFfxDx12_Radiance(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (radiance_dx12.dll != nullptr && radiance_dx12.CreateContext != nullptr)
            return true;

        spdlog::info("");

        // This is module loaded by game, need to hook it to use inputs
        if (module != nullptr && radiance_dx12_hooked.dll == nullptr)
        {
            radiance_dx12.dll = module;
        }

        if (radiance_dx12.dll == nullptr)
        {
            // Try new api first
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_radiancecache_dx12.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->FfxDx12RCPath.value_or(L"");

                if (radiance_dx12_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &radiance_dx12_hooked.dll,
                                           &radiance_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &radiance_dx12.dll);
                }

                if (radiance_dx12.dll != nullptr)
                {
                    break;
                }
            }
        }

        // Can't find Opti dlls, use just module loaded by game
        if (radiance_dx12.dll == nullptr && radiance_dx12_hooked.dll != nullptr)
        {
            radiance_dx12 = radiance_dx12_hooked;
        }

        if (radiance_dx12.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(denoiser_dx12.dll, modulePath, MAX_PATH);
            radiance_dx12.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(radiance_dx12.filePath));
        }

        if (radiance_dx12.dll != nullptr && radiance_dx12.Configure == nullptr)
        {
            radiance_dx12.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(radiance_dx12.dll, "ffxConfigure");
            radiance_dx12.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(radiance_dx12.dll, "ffxCreateContext");
            radiance_dx12.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(radiance_dx12.dll, "ffxDestroyContext");
            radiance_dx12.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(radiance_dx12.dll, "ffxDispatch");
            radiance_dx12.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(radiance_dx12.dll, "ffxQuery");
        }

        if (radiance_dx12_hooked.dll != nullptr && radiance_dx12_hooked.Configure == nullptr)
        {
            radiance_dx12_hooked.Configure =
                (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(radiance_dx12_hooked.dll, "ffxConfigure");
            radiance_dx12_hooked.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(radiance_dx12_hooked.dll, "ffxCreateContext");
            radiance_dx12_hooked.DestroyContext = (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(
                radiance_dx12_hooked.dll, "ffxDestroyContext");
            radiance_dx12_hooked.Dispatch =
                (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(radiance_dx12_hooked.dll, "ffxDispatch");
            radiance_dx12_hooked.Query =
                (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(radiance_dx12_hooked.dll, "ffxQuery");
        }
        else if (radiance_dx12_hooked.Configure == nullptr)
        {
            radiance_dx12_hooked = radiance_dx12;
        }

        if (Config::Instance()->EnableFfxInputs.value_or_default() && radiance_dx12_hooked.dll != nullptr &&
            radiance_dx12_hooked.CreateContext != nullptr && !radiance_dx12_hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (radiance_dx12_hooked.Configure != nullptr)
                DetourAttach(&(PVOID&) radiance_dx12_hooked.Configure, ffxConfigure_Dx12);

            if (radiance_dx12_hooked.CreateContext != nullptr)
                DetourAttach(&(PVOID&) radiance_dx12_hooked.CreateContext, ffxCreateContext_Dx12);

            if (radiance_dx12_hooked.DestroyContext != nullptr)
                DetourAttach(&(PVOID&) radiance_dx12_hooked.DestroyContext, ffxDestroyContext_Dx12);

            if (radiance_dx12_hooked.Dispatch != nullptr)
                DetourAttach(&(PVOID&) radiance_dx12_hooked.Dispatch, ffxDispatch_Dx12);

            if (radiance_dx12_hooked.Query != nullptr)
                DetourAttach(&(PVOID&) radiance_dx12_hooked.Query, ffxQuery_Dx12);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                radiance_dx12.Configure = nullptr;
                radiance_dx12.CreateContext = nullptr;
                radiance_dx12.DestroyContext = nullptr;
                radiance_dx12.Dispatch = nullptr;
                radiance_dx12.Query = nullptr;
            }
            else
            {
                if (radiance_dx12.dll == radiance_dx12_hooked.dll)
                    radiance_dx12 = radiance_dx12_hooked;

                radiance_dx12_hooked.hooked = true;
                State::Instance().fsrHooks = true;
            }
        }

        bool loadResult = radiance_dx12.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (!loadResult)
            radiance_dx12.dll = nullptr;

        return loadResult;
    }

    static feature_version VersionDx12()
    {
        if (main_dx12.version.major == 0 && main_dx12.Query != nullptr /* && device != nullptr*/)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = main_dx12.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = main_dx12.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    main_dx12.version.parse_version(versionNames[0]);
                    LOG_INFO("FfxApi Dx12 version: {}.{}.{}", main_dx12.version.major, main_dx12.version.minor,
                             main_dx12.version.patch);
                }
                else
                {
                    LOG_WARN("main_dx12.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_dx12.Query result: {}", (UINT) queryResult);
            }
        }

        if (main_dx12.version.major == 0 && upscaling_dx12.Query != nullptr)
            main_dx12.version = VersionDx12_SR();

        if (main_dx12.version.major == 0 && fg_dx12.Query != nullptr)
            main_dx12.version = VersionDx12_FG();

        if (denoiser_dx12.version.major == 0 && denoiser_dx12.Query != nullptr)
            denoiser_dx12.version = VersionDx12_RR();

        if (radiance_dx12.version.major == 0 && radiance_dx12.Query != nullptr)
            radiance_dx12.version = VersionDx12_RC();

        return main_dx12.version;
    }

    static feature_version VersionDx12_SR()
    {
        if (upscaling_dx12.Query == nullptr)
            return VersionDx12();

        if (upscaling_dx12.version.major == 0)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = upscaling_dx12.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = upscaling_dx12.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    upscaling_dx12.version.parse_version(versionNames[0]);
                    LOG_INFO("FfxApi Dx12 SR version: {}.{}.{}", upscaling_dx12.version.major,
                             upscaling_dx12.version.minor, upscaling_dx12.version.patch);
                }
                else
                {
                    LOG_WARN("main_dx12.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_dx12.Query result: {}", (UINT) queryResult);
            }
        }

        return upscaling_dx12.version;
    }

    static feature_version VersionDx12_FG()
    {
        if (fg_dx12.Query == nullptr)
            return VersionDx12();

        if (fg_dx12.version.major == 0)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = fg_dx12.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = fg_dx12.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    fg_dx12.version.parse_version(versionNames[0]);
                    LOG_INFO("FfxApi Dx12 FG version: {}.{}.{}", fg_dx12.version.major, fg_dx12.version.minor,
                             fg_dx12.version.patch);
                }
                else
                {
                    LOG_WARN("main_dx12.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_dx12.Query result: {}", (UINT) queryResult);
            }
        }

        return fg_dx12.version;
    }

    static feature_version VersionDx12_RR()
    {
        if (denoiser_dx12.Query == nullptr)
            return VersionDx12();

        if (denoiser_dx12.version.major == 0)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = denoiser_dx12.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = denoiser_dx12.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    denoiser_dx12.version.parse_version(versionNames[0]);
                    LOG_INFO("FfxApi Dx12 SR version: {}.{}.{}", denoiser_dx12.version.major,
                             denoiser_dx12.version.minor, denoiser_dx12.version.patch);
                }
                else
                {
                    LOG_WARN("main_dx12.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_dx12.Query result: {}", (UINT) queryResult);
            }
        }

        return denoiser_dx12.version;
    }

    static feature_version VersionDx12_RC()
    {
        if (radiance_dx12.Query == nullptr)
            return VersionDx12();

        if (radiance_dx12.version.major == 0)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = radiance_dx12.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = radiance_dx12.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    radiance_dx12.version.parse_version(versionNames[0]);
                    LOG_INFO("FfxApi Dx12 SR version: {}.{}.{}", radiance_dx12.version.major,
                             radiance_dx12.version.minor, radiance_dx12.version.patch);
                }
                else
                {
                    LOG_WARN("main_dx12.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_dx12.Query result: {}", (UINT) queryResult);
            }
        }

        return radiance_dx12.version;
    }

    static ffxReturnCode_t D3D12_CreateContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                               const ffxAllocationCallbacks* memCb)
    {
        auto type = GetType(desc->type);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainDX12;

        ffxReturnCode_t result = FFX_API_RETURN_ERROR;

        if (isFg && fg_dx12.dll != nullptr)
        {
            LOG_DEBUG("Creating with fg_dx12");
            result = fg_dx12.CreateContext(context, desc, memCb);
            contextToType[*context] = type;
            LOG_DEBUG("Created with fg_dx12: {:X}", (size_t) *context);
            return result;
        }
        else if (type == FFXStructType::Upscaling && upscaling_dx12.dll != nullptr)
        {
            LOG_DEBUG("Creating with upscaling_dx12");
            result = upscaling_dx12.CreateContext(context, desc, memCb);
            contextToType[*context] = type;

            if (result == FFX_API_RETURN_OK)
                RegisterSrContext(context);

            SrDiagOnCreate(result);

            if (result == FFX_API_RETURN_OK)
                _srRevertTarget.reset();
            else
                RevertSrVariantAfterFailure();

            LOG_DEBUG("Created with upscaling_dx12: {:X}", (size_t) *context);
            return result;
        }
        else if (type == FFXStructType::Denoiser && denoiser_dx12.dll != nullptr)
        {
            LOG_DEBUG("Creating with denoiser_dx12");
            result = denoiser_dx12.CreateContext(context, desc, memCb);
            contextToType[*context] = type;
            LOG_DEBUG("Created with denoiser_dx12: {:X}", (size_t) *context);
            return result;
        }
        else if (type == FFXStructType::RadianceCache && radiance_dx12.dll != nullptr)
        {
            LOG_DEBUG("Creating with radiance_dx12");
            result = radiance_dx12.CreateContext(context, desc, memCb);
            contextToType[*context] = type;
            LOG_DEBUG("Created with radiance_dx12: {:X}", (size_t) *context);
            return result;
        }

        const auto skipFG = isFg && fg_dx12.skipQueryCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_dx12.skipQueryCalls;

        if (main_dx12.dll != nullptr && !skipFG && !skipUpscaling)
        {
            LOG_DEBUG("Creating with main_dx12");

            if (isFg)
                fg_dx12.skipCreateCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipCreateCalls = true;

            result = main_dx12.CreateContext(context, desc, memCb);

            if (isFg)
                fg_dx12.skipCreateCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipCreateCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t D3D12_DestroyContext(ffxContext* context, const ffxAllocationCallbacks* memCb)
    {
        ffxReturnCode_t result = FFX_API_RETURN_ERROR;
        auto type = FFXStructType::Unknown;

        // Created by an upscaler dll, might not be the active one anymore
        if (auto srContext = FindSrContext(context, true); srContext.has_value())
        {
            LOG_DEBUG("Destroying with the upscaler dll that created the context");
            contextToType.erase(*context);
            return srContext->DestroyContext(context, memCb);
        }

        if (contextToType.contains(*context))
        {
            type = contextToType[*context];
            LOG_DEBUG("Found context type mapping: {}", magic_enum::enum_name(type));
            contextToType.erase(*context);
        }
        else
        {
            LOG_DEBUG("No context type mapping found, defaulting to Unknown");
        }

        switch (type)
        {
        case FFXStructType::General:
            LOG_DEBUG("Destroying with main_dx12");
            if (main_dx12.dll != nullptr)
                result = main_dx12.DestroyContext(context, memCb);
            break;

        case FFXStructType::Upscaling:
            LOG_DEBUG("Destroying with upscaling_dx12");
            if (upscaling_dx12.dll != nullptr)
                result = upscaling_dx12.DestroyContext(context, memCb);
            break;

        case FFXStructType::FG:
        case FFXStructType::SwapchainDX12:
            LOG_DEBUG("Destroying with fg_dx12");
            if (fg_dx12.dll != nullptr)
                result = fg_dx12.DestroyContext(context, memCb);
            break;

        case FFXStructType::Denoiser:
            LOG_DEBUG("Destroying with denoiser_dx12");
            if (denoiser_dx12.dll != nullptr)
                result = denoiser_dx12.DestroyContext(context, memCb);
            break;

        case FFXStructType::RadianceCache:
            LOG_DEBUG("Destroying with radiance_dx12");
            if (radiance_dx12.dll != nullptr)
                result = radiance_dx12.DestroyContext(context, memCb);
            break;

        default:
            break;
        }

        if (result == FFX_API_RETURN_OK)
        {
            LOG_DEBUG("Destroyed with mapped module");
            return result;
        }

        if (main_dx12.dll != nullptr && !_skipDestroyCalls)
        {
            LOG_DEBUG("Destroying with main_dx12");
            _skipDestroyCalls = true;
            result = main_dx12.DestroyContext(context, memCb);
            _skipDestroyCalls = false;
        }

        if (result == FFX_API_RETURN_OK)
        {
            LOG_DEBUG("Destroyed with main_dx12");
            return result;
        }

        if (upscaling_dx12.dll != nullptr)
        {
            LOG_DEBUG("Destroying with upscaling_dx12");
            result = upscaling_dx12.DestroyContext(context, memCb);
        }

        if (result == FFX_API_RETURN_OK)
        {
            LOG_DEBUG("Destroyed with upscaling_dx12");
            return result;
        }

        if (fg_dx12.dll != nullptr)
        {
            LOG_DEBUG("Destroying with fg_dx12");
            result = fg_dx12.DestroyContext(context, memCb);
        }

        if (result == FFX_API_RETURN_OK)
        {
            LOG_DEBUG("Destroyed with fg_dx12");
            return result;
        }

        LOG_ERROR("Failed to destroy context in any module");
        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t D3D12_Configure(ffxContext* context, const ffxConfigureDescHeader* desc)
    {
        auto type = GetType(desc->type);

        if (type == FFXStructType::General && contextToType.contains(*context))
            type = contextToType[*context];

        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainDX12;

        if (isFg && fg_dx12.dll != nullptr)
            return fg_dx12.Configure(context, desc);
        else if (auto srContext = FindSrContext(context); type == FFXStructType::Upscaling && srContext.has_value())
            return srContext->Configure(context, desc);
        else if (type == FFXStructType::Upscaling && upscaling_dx12.dll != nullptr)
            return upscaling_dx12.Configure(context, desc);
        else if (type == FFXStructType::Denoiser && denoiser_dx12.dll != nullptr)
            return denoiser_dx12.Configure(context, desc);
        else if (type == FFXStructType::RadianceCache && radiance_dx12.dll != nullptr)
            return radiance_dx12.Configure(context, desc);

        const auto skipFG = isFg && fg_dx12.skipQueryCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_dx12.skipQueryCalls;

        if (main_dx12.dll != nullptr && !skipFG && !skipUpscaling)
        {
            if (isFg)
                fg_dx12.skipConfigureCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipConfigureCalls = true;

            auto result = main_dx12.Configure(context, desc);

            if (isFg)
                fg_dx12.skipConfigureCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipConfigureCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t D3D12_Query(ffxContext* context, ffxQueryDescHeader* desc)
    {
        auto type = GetIndirectType(desc);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainDX12;

        if (isFg && fg_dx12.dll != nullptr)
            return fg_dx12.Query(context, desc);
        else if (auto srContext = FindSrContext(context); type == FFXStructType::Upscaling && srContext.has_value())
            return srContext->Query(context, desc);
        else if (type == FFXStructType::Upscaling && upscaling_dx12.dll != nullptr)
            return upscaling_dx12.Query(context, desc);
        else if (type == FFXStructType::Denoiser && denoiser_dx12.dll != nullptr)
            return denoiser_dx12.Query(context, desc);
        else if (type == FFXStructType::RadianceCache && radiance_dx12.dll != nullptr)
            return radiance_dx12.Query(context, desc);

        const auto skipFG = isFg && fg_dx12.skipQueryCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_dx12.skipQueryCalls;

        if (main_dx12.dll != nullptr && !skipFG && !skipUpscaling)
        {
            if (isFg)
                fg_dx12.skipQueryCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipQueryCalls = true;

            auto result = main_dx12.Query(context, desc);

            if (isFg)
                fg_dx12.skipQueryCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipQueryCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t D3D12_Dispatch(ffxContext* context, const ffxDispatchDescHeader* desc)
    {
        auto type = GetType(desc->type);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainDX12;

        if (isFg && fg_dx12.dll != nullptr)
            return fg_dx12.Dispatch(context, desc);
        else if (auto srContext = FindSrContext(context); type == FFXStructType::Upscaling && srContext.has_value())
        {
            return DispatchSrObserved(srContext->Dispatch, srContext->owner, context, desc);
        }
        else if (type == FFXStructType::Upscaling && upscaling_dx12.dll != nullptr)
        {
            return DispatchSrObserved(upscaling_dx12.Dispatch, nullptr, context, desc);
        }
        else if (type == FFXStructType::Denoiser && denoiser_dx12.dll != nullptr)
            return denoiser_dx12.Dispatch(context, desc);
        else if (type == FFXStructType::RadianceCache && radiance_dx12.dll != nullptr)
            return radiance_dx12.Dispatch(context, desc);

        const auto skipFG = isFg && fg_dx12.skipQueryCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_dx12.skipQueryCalls;

        if (main_dx12.dll != nullptr && !skipFG && !skipUpscaling)
        {
            if (isFg)
                fg_dx12.skipDispatchCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipDispatchCalls = true;

            auto result = main_dx12.Dispatch(context, desc);

            if (isFg)
                fg_dx12.skipDispatchCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_dx12.skipDispatchCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static HMODULE VkModule() { return main_vk.dll; }
    static std::wstring VkModule_Path() { return main_vk.filePath; }

    static bool InitFfxVk(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (main_vk.dll != nullptr && main_vk.CreateContext != nullptr)
            return true;

        spdlog::info("");

        if (module != nullptr)
            main_vk.dll = module;

        if (main_vk.dll == nullptr)
        {
            // Try new api first
            std::vector<std::wstring> dllNames = { L"amd_fidelityfx_vk.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->FfxVkPath.value_or(L"");

                if (main_vk_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &main_vk_hooked.dll, &main_vk.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &main_vk.dll);
                }

                if (main_vk.dll != nullptr)
                {
                    break;
                }
            }
        }

        if (main_vk.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(main_vk.dll, modulePath, MAX_PATH);
            main_vk.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(main_vk.filePath));
        }

        if (main_vk.dll != nullptr && main_vk.CreateContext == nullptr)
        {
            main_vk.Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxConfigure");
            main_vk.CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxCreateContext");
            main_vk.DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxDestroyContext");
            main_vk.Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxDispatch");
            main_vk.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(main_vk.dll, "ffxQuery");

            if (Config::Instance()->EnableFfxInputs.value_or_default() && main_vk.CreateContext != nullptr)
            {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (main_vk.Configure != nullptr)
                    DetourAttach(&(PVOID&) main_vk.Configure, ffxConfigure_Vk);

                if (main_vk.CreateContext != nullptr)
                    DetourAttach(&(PVOID&) main_vk.CreateContext, ffxCreateContext_Vk);

                if (main_vk.DestroyContext != nullptr)
                    DetourAttach(&(PVOID&) main_vk.DestroyContext, ffxDestroyContext_Vk);

                if (main_vk.Dispatch != nullptr)
                    DetourAttach(&(PVOID&) main_vk.Dispatch, ffxDispatch_Vk);

                if (main_vk.Query != nullptr)
                    DetourAttach(&(PVOID&) main_vk.Query, ffxQuery_Vk);

                auto detourResult = DetourTransactionCommit();
                if (detourResult != NO_ERROR)
                {
                    LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                    main_vk.Configure = nullptr;
                    main_vk.CreateContext = nullptr;
                    main_vk.DestroyContext = nullptr;
                    main_vk.Dispatch = nullptr;
                    main_vk.Query = nullptr;
                }
                else
                {
                    State::Instance().fsrHooks = true;
                }
            }
        }

        bool loadResult = main_vk.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (loadResult)
            VersionVk();
        else
            main_vk.dll = nullptr;

        return loadResult;
    }

    static feature_version VersionVk()
    {
        if (main_vk.version.major == 0 && main_vk.Query != nullptr)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = main_vk.Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                queryResult = main_vk.Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    main_vk.version.parse_version(versionNames[0]);
                    LOG_INFO("FfxApi Vulkan version: {}.{}.{}", main_vk.version.major, main_vk.version.minor,
                             main_vk.version.patch);
                }
                else
                {
                    LOG_WARN("main_vk.Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("main_vk.Query result: {}", (UINT) queryResult);
            }
        }

        return main_vk.version;
    }

    static PfnFfxCreateContext VULKAN_CreateContext() { return main_vk.CreateContext; }
    static PfnFfxDestroyContext VULKAN_DestroyContext() { return main_vk.DestroyContext; }
    static PfnFfxConfigure VULKAN_Configure() { return main_vk.Configure; }
    static PfnFfxQuery VULKAN_Query() { return main_vk.Query; }
    static PfnFfxDispatch VULKAN_Dispatch() { return main_vk.Dispatch; }

    static std::string ReturnCodeToString(ffxReturnCode_t result)
    {
        switch (result)
        {
        case FFX_API_RETURN_OK:
            return "The operation was successful.";
        case FFX_API_RETURN_ERROR:
            return "An error occurred that is not further specified.";
        case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE:
            return "The structure type given was not recognized for the function or context with which it was used. "
                   "This is likely a programming error.";
        case FFX_API_RETURN_ERROR_RUNTIME_ERROR:
            return "The underlying runtime (e.g. D3D12, Vulkan) or effect returned an error code.";
        case FFX_API_RETURN_NO_PROVIDER:
            return "No provider was found for the given structure type. This is likely a programming error.";
        case FFX_API_RETURN_ERROR_MEMORY:
            return "A memory allocation failed.";
        case FFX_API_RETURN_ERROR_PARAMETER:
            return "A parameter was invalid, e.g. a null pointer, empty resource or out-of-bounds enum value.";
        default:
            return "Unknown";
        }
    }

    static std::string GetTypeName(ffxStructType_t type)
    {
        switch ((uint64_t) type)
        {
        // General
        case 1u:
            return std::format("CONFIGURE_DESC_TYPE_GLOBALDEBUG1 ({:X})", type);

        case 2u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12 ({:X})", type);

        case 3u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_BACKEND_VK ({:X})", type);

        case 4u:
            return std::format("QUERY_DESC_TYPE_GET_VERSIONS ({:X})", type);

        case 5u:
            return std::format("DESC_TYPE_OVERRIDE_VERSION ({:X})", type);

        case 6u:
            return std::format("QUERY_DESC_TYPE_GET_PROVIDER_VERSION ({:X})", type);

        // Upscaling
        case 0x00010000u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_UPSCALE ({:X})", type);

        case 0x00010001u:
            return std::format("DISPATCH_DESC_TYPE_UPSCALE ({:X})", type);

        case 0x00010002u:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GETUPSCALERATIOFROMQUALITYMODE ({:X})", type);

        case 0x00010003u:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE ({:X})", type);

        case 0x00010004u:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT ({:X})", type);

        case 0x00010005u:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET ({:X})", type);

        case 0x00010006u:
            return std::format("DISPATCH_DESC_TYPE_UPSCALE_GENERATEREACTIVEMASK ({:X})", type);

        case 0x00010007u:
            return std::format("CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE ({:X})", type);

        case 0x00010008u:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GPU_MEMORY_USAGE ({:X})", type);

        case 0x00010009u:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GPU_MEMORY_USAGE_V2 ({:X})", type);

        case 0x0001000au:
            return std::format("QUERY_DESC_TYPE_UPSCALE_GET_RESOURCE_REQUIREMENTS ({:X})", type);

        // FG
        case 0x00020001u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION ({:X})", type);

        case 0x00020005u:
            return std::format("CALLBACK_DESC_TYPE_FRAMEGENERATION_PRESENT ({:X})", type);

        case 0x00020003u:
            return std::format("DISPATCH_DESC_TYPE_FRAMEGENERATION ({:X})", type);

        case 0x00020002u:
            return std::format("CONFIGURE_DESC_TYPE_FRAMEGENERATION ({:X})", type);

        case 0x00020004u:
            return std::format("DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE ({:X})", type);

        case 0x00020006u:
            return std::format("CONFIGURE_DESC_TYPE_FRAMEGENERATION_KEYVALUE ({:X})", type);

        case 0x00020007u:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATION_GPU_MEMORY_USAGE ({:X})", type);

        case 0x00020008u:
            return std::format("CONFIGURE_DESC_TYPE_FRAMEGENERATION_REGISTERDISTORTIONRESOURCE ({:X})", type);

        case 0x00020009u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS ({:X})", type);

        case 0x0002000au:
            return std::format("DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO ({:X})", type);

        case 0x0002000bu:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATION_GPU_MEMORY_USAGE_V2 ({:X})", type);

        case 0x0002000cu:
            return std::format("DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2 ({:X})", type);

        // DX12 Swapchain
        case 0x00030001u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WRAP_DX12 ({:X})", type);

        case 0x00030005u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_NEW_DX12 ({:X})", type);

        case 0x00030006u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12 ({:X})", type);

        case 0x00030002u:
            return std::format("CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_REGISTERUIRESOURCE_DX12 ({:X})", type);

        case 0x00030003u:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_INTERPOLATIONCOMMANDLIST_DX12 ({:X})", type);

        case 0x00030004u:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_INTERPOLATIONTEXTURE_DX12 ({:X})", type);

        case 0x00030007u:
            return std::format("DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12 ({:X})", type);

        case 0x00030008u:
            return std::format("CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_KEYVALUE_DX12 ({:X})", type);

        case 0x00030009u:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_GPU_MEMORY_USAGE_DX12 ({:X})", type);

        case 0x0003000au:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_GPU_MEMORY_USAGE_DX12_V2 ({:X})", type);

        // Vulkan Swapchain
        case 0x00040001u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK ({:X})", type);

        case 0x00040002u:
            return std::format("CONFIGURE_DESC_TYPE_FGSWAPCHAIN_REGISTERUIRESOURCE_VK ({:X})", type);

        case 0x00040003u:
            return std::format("QUERY_DESC_TYPE_FGSWAPCHAIN_INTERPOLATIONCOMMANDLIST_VK ({:X})", type);

        case 0x00040004u:
            return std::format("QUERY_DESC_TYPE_FGSWAPCHAIN_INTERPOLATIONTEXTURE_VK ({:X})", type);

        case 0x00040007u:
            return std::format("DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK ({:X})", type);

        case 0x00040008u:
            return std::format("CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_KEYVALUE_VK ({:X})", type);

        case 0x00040009u:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_GPU_MEMORY_USAGE_VK ({:X})", type);

        case 0x00040005u:
            return std::format("QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK ({:X})", type);

        case 0x00040010u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_MODE_VK ({:X})", type);

        // Denoiser
        case 0x00050001u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_DENOISER ({:X})", type);

        case 0x00050002u:
            return std::format("DISPATCH_DESC_TYPE_DENOISER ({:X})", type);

        case 0x00050003u:
            return std::format("DISPATCH_DESC_INPUT_4_SIGNALS_TYPE_DENOISER ({:X})", type);

        case 0x00050004u:
            return std::format("DISPATCH_DESC_INPUT_2_SIGNALS_TYPE_DENOISER ({:X})", type);

        case 0x00050005u:
            return std::format("DISPATCH_DESC_INPUT_1_SIGNAL_TYPE_DENOISER ({:X})", type);

        case 0x00050007u:
            return std::format("DISPATCH_DESC_INPUT_DOMINANT_LIGHT_TYPE_DENOISER ({:X})", type);

        case 0x00050008u:
            return std::format("CONFIGURE_DESC_TYPE_DENOISER_SETTINGS ({:X})", type);

        case 0x00050009u:
            return std::format("QUERY_DESC_TYPE_DENOISER_GPU_MEMORY_USAGE ({:X})", type);

        case 0x0005000au:
            return std::format("QUERY_DESC_TYPE_DENOISER_GET_VERSION ({:X})", type);

        case 0x0005000bu:
            return std::format("QUERY_DESC_TYPE_DENOISER_GET_DEFAULT_SETTINGS ({:X})", type);

        // Radiance Cache
        case 0x00060002u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_RADIANCECACHE ({:X})", type);

        case 0x00060004u:
            return std::format("DISPATCH_DESC_TYPE_RADIANCECACHE ({:X})", type);

        case 0x00060005u:
            return std::format("DEBUG_DISPATCH_DESC_TYPE_RADIANCECACHE ({:X})", type);
        }

        return std::format("??? ({:X})", type);
    }
};
