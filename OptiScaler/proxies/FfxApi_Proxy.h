#pragma once

#include "SysUtils.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <inputs/FfxApi_Dx12.h>
#include <inputs/FfxApi_Vk.h>

#include <fsr4/FSR4DriverProviderDx12.h>
#include <fsr4/FSR4DriverProviderVk.h>
#include <fsr4/FSR4Models.h>
#include <fsr4/FSR4Presets.h>
#include <fsr4/FSR4VisualVerify.h>
#include <fsr4/FSR4Versions.h>
#include <misc/CustomProviders.h>
#include <misc/IlionUpscalers.h>
#include <misc/IdentifyGpu.h>
#include <menu/ilion_lang.h>

#include "ffx_api.h"
#include "FfxModule.h"
#include <detours/detours.h>
#include <ffx_framegeneration.h>
#include <ffx_upscale.h>

#include <magic_enum.hpp>
#include <mutex>
#include <set>
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

// Upscaler or FG from custom/<name>/<kind> or a plugin, only used by Opti and never hooked for inputs
struct CustomFfxModule
{
    std::string name;
    FfxModule module;
    State::FfxVersionList versions;
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
    inline static FfxModule upscaling_vk;
    inline static FfxModule fg_vk;

    // amd_fidelityfx_vk.dll next to the loader, for the effects whose SDK 2.x dll is missing
    inline static FfxModule legacy_vk;

    inline static FfxModule main_dx12_hooked;
    inline static FfxModule upscaling_dx12_hooked;
    inline static FfxModule fg_dx12_hooked;
    inline static FfxModule denoiser_dx12_hooked;
    inline static FfxModule radiance_dx12_hooked;
    inline static FfxModule main_vk_hooked;
    inline static FfxModule upscaling_vk_hooked;
    inline static FfxModule fg_vk_hooked;

    inline static ankerl::unordered_dense::map<ffxContext, FFXStructType> contextToType;

    // Vulkan contexts made by the driver, later calls with them go to the same module
    inline static std::mutex vkDriverMutex;
    inline static ankerl::unordered_dense::map<ffxContext, FfxModule*> vkDriverContexts;

    // std::map so the pointers handed out stay valid
    inline static std::map<std::string, CustomFfxModule> custom_dx12;
    inline static std::map<std::string, CustomFfxModule> custom_vk;
    inline static std::map<std::string, CustomFfxModule> custom_fg_dx12;
    inline static std::map<std::string, CustomFfxModule> custom_fg_vk;
    inline static std::recursive_mutex customMutex;
    inline static ankerl::unordered_dense::map<ffxContext, FfxModule*> customContexts;

    static FfxModule* CustomContextModule(ffxContext* context)
    {
        if (context == nullptr)
            return nullptr;

        std::scoped_lock lock(customMutex);
        auto it = customContexts.find(*context);
        return it != customContexts.end() ? it->second : nullptr;
    }

    // Custom upscaler dlls can have the names of the SDK dlls, so looking one up by name can find them instead
    static bool IsCustomModule(HMODULE module)
    {
        if (module == nullptr)
            return false;

        std::scoped_lock lock(customMutex);
        auto matches = [module](const auto& custom) { return custom.second.module.dll == module; };
        return std::ranges::any_of(custom_dx12, matches) || std::ranges::any_of(custom_vk, matches) ||
               std::ranges::any_of(custom_fg_dx12, matches) || std::ranges::any_of(custom_fg_vk, matches);
    }

    // Util::LoadProxyLibrary that never returns a custom upscaler dll, those can't be hooked for inputs
    static void LoadSdkLibrary(const std::wstring& name, const std::wstring& optiPath, const std::wstring& overridePath,
                               HMODULE* memoryModule, HMODULE* loadedModule)
    {
        Util::LoadProxyLibrary(name, optiPath, overridePath, memoryModule, loadedModule);

        if (IsCustomModule(*memoryModule))
        {
            LOG_DEBUG("Ignoring {} in memory, it's a custom upscaler", wstring_to_string(name));
            *memoryModule = nullptr;
        }

        if (IsCustomModule(*loadedModule))
        {
            LOG_DEBUG("Ignoring loaded {}, it's a custom upscaler", wstring_to_string(name));
            *loadedModule = nullptr;
        }
    }

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

    // ILION: FSR 4 network chosen before the last model change (FSR4Models), kept until a context is made with the
    // new one; nullopt inside means "automatic"
    inline static std::optional<std::optional<FSR4Model>> _srModelRevertTarget;

    // ILION: the FFX provider index was picked by hand (Advanced) since the last implementation choice; until then
    // the index is resolved for the dll in use (SrAutomaticIndex)
    inline static bool _srIndexManual = false;

    // ILION: id of the upscaler provider the AMD driver handed to an FFX dll through IAmdExtFfxApi (FSR4Upgrade)
    inline static std::atomic<uint64_t> _driverProviderId { 0 };

    // ILION: a stable copy of a provider name for the FFX version list (State::ffxUpscalerVersionsDx12 keeps the
    // pointer until the next query). Each name is kept once for the whole session: a std::set node never moves, so the
    // pointer stays valid when the module it came from is replaced by a dll switch
    static const char* StableProviderName(const std::string& name)
    {
        static std::mutex namesMutex;
        static std::set<std::string> names;

        std::scoped_lock lock(namesMutex);
        return names.insert(name).first->c_str();
    }

    // ILION: a query without a context for an AMD driver provider (see FfxModule::driverProvider). The versions query
    // lists the provider itself, so the version list, its index and the names shown all belong to what runs
    static ffxReturnCode_t DriverProviderQueryWithoutContext(const FfxModule& module, ffxQueryDescHeader* desc)
    {
        if (desc != nullptr && desc->type == FFX_API_QUERY_DESC_TYPE_GET_VERSIONS)
        {
            auto versions = reinterpret_cast<ffxQueryDescGetVersions*>(desc);

            if (versions->outputCount == nullptr)
                return FFX_API_RETURN_ERROR_PARAMETER;

            if (versions->versionIds != nullptr && *versions->outputCount >= 1)
                versions->versionIds[0] = module.providerVersionId;
            if (versions->versionNames != nullptr && *versions->outputCount >= 1)
                versions->versionNames[0] = StableProviderName(module.providerName);

            *versions->outputCount = 1;
            return FFX_API_RETURN_OK;
        }

        return module.fallbackQuery != nullptr ? module.fallbackQuery(nullptr, desc) : FFX_API_RETURN_NO_PROVIDER;
    }

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

        if (const auto display = IlionUpscalers::DisplayForPath(dllPath); !display.empty())
            return display;

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
    static void SetSrIndexManual(bool manual) { _srIndexManual = manual; }

    // ILION: the provider an upscaling context was made with, asked to the dll that made it with FFX's own
    // GET_PROVIDER_VERSION query. Never through D3D12_Query: GetIndirectType reads the field after the header of this
    // query (versionId, 0) as its type, takes it as a general query and hands the context to main_dx12, a dll that
    // doesn't own it (crash). Only asked to AMD based FFX dlls: not to an AMD driver provider (its query only knows the
    // upscaler's descriptions) nor to third-party dlls such as HelixSR.
    static bool SrContextProviderVersion(ffxContext* context, uint64_t* versionId, const char** versionName)
    {
        const auto fns = FindSrContext(context);
        if (!fns.has_value() || fns->Query == nullptr || fns->owner == nullptr)
            return false;

        bool amdFfxDll = false;
        {
            std::scoped_lock lock(_srMutex);

            if (upscaling_dx12.dll == fns->owner)
                amdFfxDll = !upscaling_dx12.driverProvider && !upscaling_dx12.thirdParty;
            else
            {
                for (const auto& loaded : _srLoaded)
                {
                    if (loaded.dll == fns->owner)
                        amdFfxDll = !loaded.driverProvider && !loaded.thirdParty;
                }
            }
        }

        if (!amdFfxDll)
            return false;

        ffxQueryGetProviderVersion query {};
        query.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;

        if (fns->Query(context, &query.header) != FFX_API_RETURN_OK || query.versionName == nullptr)
            return false;

        *versionId = query.versionId;
        *versionName = query.versionName;
        return true;
    }

    // ILION (SDK 2.4): the provider a frame generation context (FG or its swapchain) was made with, asked to the dll
    // that made it, as D3D12_CreateContext routes it: the framegeneration dll when loaded, else the combined one. Never
    // through D3D12_Query, for the reason above
    static bool FgContextProviderVersion(ffxContext* context, uint64_t* versionId, const char** versionName)
    {
        if (context == nullptr || *context == nullptr)
            return false;

        const PfnFfxQuery query = fg_dx12.dll != nullptr ? fg_dx12.Query : main_dx12.Query;
        if (query == nullptr)
            return false;

        ffxQueryGetProviderVersion providerQuery {};
        providerQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;

        if (query(context, &providerQuery.header) != FFX_API_RETURN_OK || providerQuery.versionName == nullptr)
            return false;

        *versionId = providerQuery.versionId;
        *versionName = providerQuery.versionName;
        return true;
    }

    static void SrNoteDriverProviderId(uint64_t id)
    {
        if (id != 0)
            _driverProviderId = id;
    }
    static bool SrIndexManual() { return _srIndexManual; }

    // ILION BLACKLIGHT: the upscaler in use is an AMD driver provider (amdxcffx64), called directly without AMD's SDK dll
    static bool SrDriverProviderActive() { return upscaling_dx12.driverProvider; }

    // ILION: the provider of its FFX version list the dll in use runs by itself. An FFX dll also lists the AMD driver's
    // provider (an external one, bit 31 of its id): ILION's IAmdExtFfxApi (FSR4Upgrade) hands it to every FFX dll that
    // asks, and it comes first as the newest. Index 0 would make a community build pass the upscale to the driver.
    //   default dll (not one of ILION's folders): 0, AMD's driver through the SDK, as it always was
    //   FSR 4 dlls of ILION's folders (community builds, Lite, AMD's SDK dll): their own first provider
    //   HelixSR, other FFX dlls, AMD driver providers: 0
    // ILION 1.5: whether the saved FSR variant is an AMD driver dll. Upstream's FSR4DriverProviderDx12 runs it: the
    // default dll stays loaded and the driver's entry of its version list is chosen (SrAutomaticIndex)
    static bool SrVariantIsDriver(const std::wstring& name)
    {
        static std::mutex mutex;
        static std::wstring lastName;
        static bool lastResult = false;

        if (name.empty())
            return false;

        std::scoped_lock lock(mutex);
        if (name != lastName)
        {
            bool driver = false;
            const auto path = SrVariantDllPath(name);
            std::string problem;
            lastResult = !path.empty() && FSR4Versions::Validate(path, &problem, &driver) && driver;
            lastName = name;
        }

        return lastResult;
    }

    // ILION 1.5: the DX12 FFX index points at the AMD driver's provider (the catalog's driver entry runs)
    static bool DriverEntryChosenDx12()
    {
        const auto& ids = State::Instance().ffxUpscalerVersionsDx12.ids;
        const auto index = Config::Instance()->FfxUpscalerIndex.value_or_default();
        const auto driverId = FSR4DriverProviderDx12::VersionId();
        return driverId != 0 && index >= 0 && index < (int) ids.size() && ids[index] == driverId;
    }

    static int SrAutomaticIndex(const std::vector<uint64_t>& ids)
    {
        // ILION 1.5: the catalog's AMD driver entry: the driver provider of the version list
        if (!ids.empty() && SrVariantIsDriver(Config::Instance()->Fsr4SrVariant.value_or(L"")))
        {
            const auto driverId = FSR4DriverProviderDx12::VersionId();
            for (size_t i = 0; driverId != 0 && i < ids.size(); i++)
            {
                if (ids[i] == driverId)
                    return (int) i;
            }

            LOG_WARN("The AMD driver's FSR 4 isn't in the version list, the default one is used");
        }

        if (ids.empty() || upscaling_dx12.driverProvider)
            return 0;

        const auto entry = IlionUpscalers::FindByPath(upscaling_dx12.filePath);
        if (!entry.has_value() || entry->family != IlionUpscalers::Family::Fsr4)
            return 0;

        // Not the driver's: neither an external id nor the id the driver handed out (see SrNoteDriverProviderId)
        const uint64_t driverId = _driverProviderId;
        for (size_t i = 0; i < ids.size(); i++)
        {
            if ((ids[i] & 0x80000000ull) == 0 && ids[i] != driverId)
                return (int) i;
        }

        return 0;
    }

    // ILION: called by the menu when the FSR 4 network changes, before the backend is made again
    static void ArmSrModelRevert(std::optional<FSR4Model> previous) { _srModelRevertTarget = previous; }

    // ILION: result of a context made outside the DX12 proxy (the Vulkan driver provider) after a model change
    static void SrModelContextResult(bool ok)
    {
        if (ok)
            _srModelRevertTarget.reset();
        else
            RevertSrModelAfterFailure();
    }

    static HMODULE Dx12Module() { return main_dx12.dll; }
    static HMODULE Dx12Module_SR() { return upscaling_dx12.dll; }
    static HMODULE Dx12Module_FG() { return fg_dx12.dll; }
    static HMODULE Dx12Module_Denoiser() { return denoiser_dx12.dll; }
    static HMODULE Dx12Module_Radiance() { return radiance_dx12.dll; }

    // FFX modules that Opti creates its upscalers with, custom ones included
    static bool IsUpscalerModule(HMODULE module)
    {
        return module != nullptr &&
               (module == main_dx12.dll || module == upscaling_dx12.dll || IsCustomUpscalerModule(module));
    }

    static bool IsVkModule(HMODULE module)
    {
        if (module == nullptr)
            return false;

        for (const auto* vk :
             { &main_vk, &upscaling_vk, &fg_vk, &legacy_vk, &main_vk_hooked, &upscaling_vk_hooked, &fg_vk_hooked })
        {
            if (vk->dll == module)
                return true;
        }

        std::scoped_lock lock(customMutex);
        auto matches = [module](const auto& custom) { return custom.second.module.dll == module; };
        return std::ranges::any_of(custom_vk, matches) || std::ranges::any_of(custom_fg_vk, matches);
    }

    static bool IsCustomUpscalerModule(HMODULE module)
    {
        if (module == nullptr)
            return false;

        std::scoped_lock lock(customMutex);
        return std::ranges::any_of(custom_dx12,
                                   [module](const auto& custom) { return custom.second.module.dll == module; });
    }

    // Module that runs the DX12 upscaler with this version id
    static HMODULE UpscalerModule(uint64_t versionId)
    {
        if (versionId != 0 && versionId == FSR4DriverProviderDx12::VersionId())
            return FSR4DriverProviderDx12::LoadedModule();

        return upscaling_dx12.dll != nullptr ? upscaling_dx12.dll : main_dx12.dll;
    }

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

    // Custom FFX FG picked for FSR FG output (State::activeFsrFgProvider), nullptr for the regular one
    static CustomFfxModule* ActiveCustomFgDx12()
    {
        auto& name = State::Instance().activeFsrFgProvider;
        return CustomProviders::Has(name, CustomProviders::Kind::FfxFg) ? LoadCustomFgDx12(name) : nullptr;
    }

    static CustomFfxModule* ActiveCustomFgVk()
    {
        auto& name = State::Instance().activeFsrFgProvider;
        return CustomProviders::Has(name, CustomProviders::Kind::FfxFgVk) ? LoadCustomFgVk(name) : nullptr;
    }

    // FSR FG output can run on DX12, with the active custom FG or the regular FFX dlls
    static bool IsFsrFgReady(bool sendNotification = true)
    {
        return ActiveCustomFgDx12() != nullptr || IsFGReady(sendNotification);
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

    // Every upscaler ILION knows of, FFX and others, read from disk again (IlionUpscalers)
    using SrVariant = IlionUpscalers::Entry;

    static std::vector<SrVariant> SrVariants() { return IlionUpscalers::Rescan(); }

    // The upscaler dll in use reports providers that aren't AMD's (a third-party FFX upscaler such as HelixSR)
    static bool ActiveSrThirdParty()
    {
        std::scoped_lock lock(_srMutex);
        return upscaling_dx12.thirdParty;
    }

    // Provider the upscaler dll in use reports, empty when none is loaded or it didn't answer
    static std::string ActiveSrProvider()
    {
        std::scoped_lock lock(_srMutex);

        // Asked once per dll, when the game has a device; without one AMD's dlls would only name FSR 3.1
        auto device = State::Instance().currentD3D12Device;
        if (!upscaling_dx12.providerFromDevice && device != nullptr && upscaling_dx12.Query != nullptr)
        {
            std::string name;
            bool allAmd = true;

            if (QueryProviders(upscaling_dx12, device, &name, &allAmd))
                upscaling_dx12.providerName = name;

            upscaling_dx12.providerFromDevice = true;
        }

        return upscaling_dx12.providerFromDevice ? upscaling_dx12.providerName : std::string();
    }

    // Empty name is the dll in OptiDllPath, returns empty when the dll doesn't exist
    static std::wstring SrVariantDllPath(const std::wstring& name)
    {
        if (!name.empty())
            return IlionUpscalers::FfxDllPathFor(name);

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

        // ILION 1.5: an AMD driver variant runs on the default dll, its version list gives the driver (SrAutomaticIndex)
        if (SrVariantIsDriver(*pending))
        {
            LOG_INFO("{}: AMD driver FSR 4 through the default dll's version list", variantName);
            targetPath = SrVariantDllPath(L"");
        }

        if (targetPath.empty())
        {
            LOG_ERROR("Can't find amd_fidelityfx_upscaler_dx12.dll for variant: {}", variantName);
            ImGui::InsertNotification(
                { ImGuiToastType::Error, 10000, Ilion::Tr("Can't find the dll of the selected upscaler") });
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
            bool driverProvider = false;
            if (std::string problem; !FSR4Versions::Validate(targetPath, &problem, &driverProvider))
            {
                LOG_ERROR("{} can't be used: {}", wstring_to_string(targetPath), problem);
                ImGui::InsertNotification(
                    { ImGuiToastType::Error, 10000, Ilion::Tr("The selected upscaler isn't a compatible dll") });
                SrDiagSetSwitchResult(diagFrom, diagTo, "FAILED, " + problem);
                return;
            }

            // ILION: an AMD driver dll: its upscaler provider gives the five functions; its queries without a context
            // are answered by DriverProviderQueryWithoutContext. Loaded once, it stays loaded; switching back to it
            // finds it in _srLoaded with its own provider data
            if (driverProvider)
            {
                // ILION: the AMD driver's code only runs on an AMD GPU (a saved choice from an AMD PC, or the driver of
                // an AMD iGPU next to another GPU, is never loaded); the dll in use stays
                if (const auto vendor = IdentifyGpu::getPrimaryGpu().vendorId;
                    vendor != VendorId::AMD && vendor != VendorId::Invalid)
                {
                    LOG_ERROR("{} not loaded: FSR 4 through the AMD driver needs an AMD GPU", wstring_to_string(targetPath));
                    ImGui::InsertNotification(
                        { ImGuiToastType::Error, 10000, Ilion::Tr("FSR 4 through the AMD driver needs an AMD GPU") });
                    SrDiagSetSwitchResult(diagFrom, diagTo, "FAILED, not an AMD GPU");

                    if (Config::Instance()->Fsr4SrVariant.value_or(L"") == *pending)
                        Config::Instance()->Fsr4SrVariant.reset();

                    return;
                }

                // ILION 1.5: the AMD driver's FSR 4 now comes from upstream's FSR4DriverProviderDx12, an entry of the
                // FFX version list ("FSR 4 (driver)"); a driver dll is no longer loaded as an SR variant
                LOG_WARN("{} is an AMD driver dll: choose the driver entry of the FSR version list instead",
                         wstring_to_string(targetPath));
                ImGui::InsertNotification(
                    { ImGuiToastType::Info, 10000, Ilion::Tr("FSR 4 from the AMD driver is chosen in the FSR version list") });
                SrDiagSetSwitchResult(diagFrom, diagTo, "SKIPPED, driver provider is in the version list");
                return;
            }
            else
                target.dll = NtdllProxy::LoadLibraryExW_Ldr(targetPath.c_str(), NULL, 0);

            if (target.dll == nullptr)
            {
                LOG_ERROR("Can't load {}", wstring_to_string(targetPath));
                ImGui::InsertNotification(
                    { ImGuiToastType::Error, 10000, Ilion::Tr("Can't load the dll of the selected upscaler") });
                SrDiagSetSwitchResult(diagFrom, diagTo, "FAILED, dll can't be loaded");
                return;
            }

            wchar_t modulePath[MAX_PATH];
            GetModuleFileNameW(target.dll, modulePath, MAX_PATH);
            target.filePath = std::wstring(modulePath);

            // A driver provider already has its functions, from its upscaler provider
            if (!target.driverProvider)
            {
                target.Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(target.dll, "ffxConfigure");
                target.CreateContext =
                    (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(target.dll, "ffxCreateContext");
                target.DestroyContext =
                    (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(target.dll, "ffxDestroyContext");
                target.Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(target.dll, "ffxDispatch");
                target.Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(target.dll, "ffxQuery");
            }

            if (target.CreateContext == nullptr || target.DestroyContext == nullptr || target.Configure == nullptr ||
                target.Dispatch == nullptr || target.Query == nullptr)
            {
                LOG_ERROR("Missing ffx exports in {}", wstring_to_string(target.filePath));
                ImGui::InsertNotification(
                    { ImGuiToastType::Error, 10000, Ilion::Tr("The selected upscaler is not a valid dll") });
                SrDiagSetSwitchResult(diagFrom, diagTo, "FAILED, dll is missing ffx exports");
                return;
            }

            LOG_INFO("Variant {} loaded from {}, module: {:X}", variantName, wstring_to_string(target.filePath),
                     (size_t) target.dll);
        }

        // Model selection hooks are per source, move them to the module that's going to be used. A driver provider
        // has its networks hooked by FSR4Models when loaded; its other queries without a context go to the FFX dll in
        // use now (or the one it already had)
        if (target.driverProvider)
        {
            if (!upscaling_dx12.driverProvider && upscaling_dx12.Query != nullptr)
                target.fallbackQuery = upscaling_dx12.Query;
            else if (target.fallbackQuery == nullptr && main_dx12.Query != nullptr)
                target.fallbackQuery = main_dx12.Query;
        }
        else
            HookFsr4ModelSelection(target, true);

        // Provider indexes belong to the dll they came from. The new dll starts with its own first provider, which
        // is the dll itself: HelixSR lists AMD's FSR after itself, an index kept from before could pick that one.
        Config::Instance()->FfxUpscalerIndex.set_volatile_value(0);

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

        FfxModule previousModule = previous;
        HookFsr4ModelSelection(previousModule, true);

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
        const auto previousEntry = IlionUpscalers::FindByPath(previous.filePath);
        const auto previousVersion = previousEntry.has_value() ? previousEntry->key : std::wstring();

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
                                    Ilion::Tr("The selected upscaler failed to start, the previous one was restored.") });

        SrDiagSetSwitchResult(failedName, previousName, "REVERTED, the new dll couldn't create a context");
    }

    // ILION: the context made with a newly chosen FSR 4 network failed: the previous choice comes back and the backend
    // is made again with it, as a dll switch is reverted
    static void RevertSrModelAfterFailure()
    {
        if (!_srModelRevertTarget.has_value())
            return;

        const auto previous = *_srModelRevertTarget;
        _srModelRevertTarget.reset();

        const std::string failed = Config::Instance()->Fsr4Model.has_value() ? FSR4Models::Name(Config::Instance()->Fsr4Model.value())
                                                                             : "auto";
        if (previous.has_value())
            Config::Instance()->Fsr4Model = previous.value();
        else
            Config::Instance()->Fsr4Model.reset();

        for (auto& changeBackend : State::Instance().changeBackend)
            changeBackend.second = true;

        const std::string restored = previous.has_value() ? FSR4Models::Name(previous.value()) : "auto";
        LOG_ERROR("FSR 4 model {} couldn't create a context, {} is selected again", failed, restored);
        ImGui::InsertNotification({ ImGuiToastType::Error, 10000,
                                    Ilion::Tr("The selected FSR 4 model failed to start, the previous one was restored.") });
        SrDiagSetSwitchResult("FSR 4 model " + failed, "FSR 4 model " + restored, "REVERTED, the model couldn't create a context");
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
        if (module != nullptr && main_dx12_hooked.dll == nullptr && !IsCustomModule(module))
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
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &main_dx12_hooked.dll, &main_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &memModule, &main_dx12.dll);
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
                FSR4Presets::Hook(main_dx12.dll);
                FSR4Models::Hook(main_dx12.dll);
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
        if (module != nullptr && upscaling_dx12_hooked.dll == nullptr && !IsCustomModule(module))
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
                    bool driverProvider = false;
                    if (std::string problem; !overridePath.empty() && !FSR4Versions::Validate(overridePath, &problem, &driverProvider))
                    {
                        LOG_ERROR("Saved FSR version can't be used ({}): {}", problem, wstring_to_string(overridePath));
                        overridePath.clear();
                    }
                    else if (driverProvider)
                    {
                        // ILION: an AMD driver provider: the default dll loads first (the driver checks that the SDK
                        // dll is there), the provider is switched to when the first context is made
                        LOG_INFO("Saved FSR version is an AMD driver provider: the default dll, its driver entry is chosen");
                        std::scoped_lock pendingLock(_srMutex);
                        _srPending = Config::Instance()->Fsr4SrVariant.value();
                        overridePath.clear();
                    }
                }

                if (upscaling_dx12_hooked.dll == nullptr)
                {
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &upscaling_dx12_hooked.dll,
                                   &upscaling_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &memModule, &upscaling_dx12.dll);
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
            // ILION 1.5: FSR4Presets / FSR4Models are hooked once, by HookFsr4ModelSelection below (it skips HelixSR
            // and third party providers, which have no AMD preset selection)

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

        HookFsr4ModelSelection(upscaling_dx12, false);

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
        if (module != nullptr && fg_dx12_hooked.dll == nullptr && !IsCustomModule(module))
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
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &fg_dx12_hooked.dll, &fg_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &memModule, &fg_dx12.dll);
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
        if (module != nullptr && denoiser_dx12_hooked.dll == nullptr && !IsCustomModule(module))
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
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &denoiser_dx12_hooked.dll, &denoiser_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &memModule, &denoiser_dx12.dll);
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
        if (module != nullptr && radiance_dx12_hooked.dll == nullptr && !IsCustomModule(module))
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
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &radiance_dx12_hooked.dll, &radiance_dx12.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    LoadSdkLibrary(dllNames[i], optiPath, overridePath, &memModule, &radiance_dx12.dll);
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
            DWORD len = GetModuleFileNameW(radiance_dx12.dll, modulePath, MAX_PATH);
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

    // Loading is only tried once per name, nullptr if it failed
    static CustomFfxModule* LoadCustomUpscalerDx12(const std::string& name)
    {
        return LoadCustom(custom_dx12, name, CustomProviders::Kind::Ffx);
    }

    static CustomFfxModule* LoadCustomUpscalerVk(const std::string& name)
    {
        return LoadCustom(custom_vk, name, CustomProviders::Kind::FfxVk);
    }

    static CustomFfxModule* LoadCustomFgDx12(const std::string& name)
    {
        return LoadCustom(custom_fg_dx12, name, CustomProviders::Kind::FfxFg);
    }

    static CustomFfxModule* LoadCustomFgVk(const std::string& name)
    {
        return LoadCustom(custom_fg_vk, name, CustomProviders::Kind::FfxFgVk);
    }

    // Already loaded custom upscaler, doesn't try loading it
    static CustomFfxModule* FindCustomUpscalerDx12(const std::string& name) { return FindCustom(custom_dx12, name); }

    static CustomFfxModule* FindCustomUpscalerVk(const std::string& name) { return FindCustom(custom_vk, name); }

    // Context made by the given module, later calls with it go to the same module
    static ffxReturnCode_t D3D12_CreateContext(FfxModule* module, ffxContext* context, ffxCreateContextDescHeader* desc,
                                               const ffxAllocationCallbacks* memCb)
    {
        auto result = module->CreateContext(context, desc, memCb);

        if (result == FFX_API_RETURN_OK)
        {
            std::scoped_lock lock(customMutex);
            customContexts[*context] = module;
        }

        return result;
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

    // Every provider id AMD's FFX dlls report starts with this value (FSR 4, FSR 3.1 and FSR 2 alike)
    static constexpr uint64_t AmdProviderIdPrefix = 0xF5A5CA1E00000000ull;

    // Asks an upscaler dll which providers it has: the first one's name, and whether all of them are AMD's
    static bool QueryProviders(FfxModule& module, void* device, std::string* firstName, bool* allAmd)
    {
        if (module.Query == nullptr)
            return false;

        // ILION: an AMD driver provider is its single provider; its own query isn't asked without a context
        if (module.driverProvider)
        {
            if (firstName != nullptr)
                *firstName = module.providerName;
            if (allAmd != nullptr)
                *allAmd = true;
            return !module.providerName.empty();
        }

        ffxQueryDescGetVersions versionQuery {};
        versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        versionQuery.device = device;
        uint64_t versionCount = 0;
        versionQuery.outputCount = &versionCount;

        auto queryResult = module.Query(nullptr, &versionQuery.header);

        if (versionCount == 0 || queryResult != FFX_API_RETURN_OK)
        {
            LOG_WARN("Upscaler version query of {} failed: {}", wstring_to_string(module.filePath), (UINT) queryResult);
            return false;
        }

        std::vector<uint64_t> versionIds(versionCount);
        std::vector<const char*> versionNames(versionCount);
        versionQuery.versionIds = versionIds.data();
        versionQuery.versionNames = versionNames.data();

        queryResult = module.Query(nullptr, &versionQuery.header);

        if (queryResult != FFX_API_RETURN_OK || versionNames[0] == nullptr)
        {
            LOG_WARN("Upscaler version query 2 of {} failed: {}", wstring_to_string(module.filePath), (UINT) queryResult);
            return false;
        }

        *firstName = versionNames[0];
        *allAmd = true;

        for (uint64_t i = 0; i < versionCount; i++)
        {
            LOG_INFO("Upscaler dll {} provider {}: '{}' id {:X}{}", wstring_to_string(module.filePath), i,
                     versionNames[i] != nullptr ? versionNames[i] : "", versionIds[i], device != nullptr ? " (device)" : "");

            if ((versionIds[i] & 0xFFFFFFFF00000000ull) != AmdProviderIdPrefix)
                *allAmd = false;
        }

        return true;
    }

    // Version kept in the module, from the query without a device as OptiScaler always did, and whether the dll
    // is a third-party upscaler
    static void QuerySrProvider(FfxModule& module)
    {
        std::string name;
        bool allAmd = true;

        if (!QueryProviders(module, nullptr, &name, &allAmd))
            return;

        module.version.parse_version(name.c_str());
        module.thirdParty = !allAmd;

        if (module.providerName.empty())
            module.providerName = name;
    }

    // FSR 4's model selection hooks are found by a byte pattern inside the dll. AMD's dlls (and dlls that don't
    // answer the query) get them as before; a dll whose providers aren't AMD's is used as it is.
    static void HookFsr4ModelSelection(FfxModule& module, bool rehook)
    {
        if (module.dll == nullptr || module.driverProvider)
            return;

        if (module.version.major == 0)
            QuerySrProvider(module);

        // Known from the file: HelixSR (its network marker) or an FFX dll outside the FSR families
        if (const auto entry = IlionUpscalers::FindByPath(module.filePath);
            entry.has_value() && entry->family != IlionUpscalers::Family::Fsr4)
        {
            LOG_INFO("FSR 4 model selection not hooked in {}: it is {}", wstring_to_string(module.filePath),
                     entry->display);
            return;
        }

        if (module.thirdParty)
        {
            LOG_INFO("FSR 4 model selection not hooked in {}: its providers aren't AMD's ('{}')",
                     wstring_to_string(module.filePath), module.providerName);
            return;
        }

        FSR4Presets::Hook(module.dll); // ILION 1.5: upstream's preset override replaces FSR4ModelSelection
        FSR4Models::Hook(module.dll); // ILION: network choice when the dll has several models
    }

    static feature_version VersionDx12_SR()
    {
        if (upscaling_dx12.Query == nullptr)
            return VersionDx12();

        if (upscaling_dx12.version.major == 0)
            QuerySrProvider(upscaling_dx12);

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

        if (auto overrideVersion = FindOverrideVersion(desc); type == FFXStructType::Upscaling && overrideVersion &&
                                                              *overrideVersion == FSR4DriverProviderDx12::VersionId())
        {
            return FSR4DriverProviderDx12::CreateContext(context, desc, memCb);
        }

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
            {
                _srRevertTarget.reset();
                _srModelRevertTarget.reset();
            }
            else
            {
                RevertSrVariantAfterFailure();
                RevertSrModelAfterFailure();
            }

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
        if (FSR4DriverProviderDx12::OwnsContext(context))
            return FSR4DriverProviderDx12::DestroyContext(context, memCb);

        if (auto custom = CustomContextModule(context))
        {
            std::scoped_lock lock(customMutex);
            customContexts.erase(*context);
            return custom->DestroyContext(context, memCb);
        }

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
        if (FSR4DriverProviderDx12::OwnsContext(context))
            return FSR4DriverProviderDx12::Configure(context, desc);

        if (auto custom = CustomContextModule(context))
            return custom->Configure(context, desc);

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
        if (FSR4DriverProviderDx12::OwnsContext(context))
            return FSR4DriverProviderDx12::Query(context, desc);

        if (auto custom = CustomContextModule(context))
            return custom->Query(context, desc);

        if (desc->type == FFX_API_QUERY_DESC_TYPE_GET_VERSIONS && GetIndirectType(desc) == FFXStructType::Upscaling)
        {
            return D3D12_QueryUpscalerVersions(reinterpret_cast<ffxQueryDescGetVersions*>(desc),
                                               FSR4DriverProviderDx12::Listing::WhenNewestUpscaler);
        }

        return D3D12_QuerySdk(context, desc);
    }

    // Versions of the FFX SDK frame generation, the driver's ML FG is given to the SDK through AmdExtFfxApi.
    // A custom module is asked for its own instead.
    static ffxReturnCode_t D3D12_QueryFgVersions(void* device, std::vector<uint64_t>& ids,
                                                 std::vector<const char*>& names, FfxModule* custom = nullptr)
    {
        auto query = [custom](ffxQueryDescHeader* header)
        { return custom != nullptr ? custom->Query(nullptr, header) : D3D12_Query(nullptr, header); };

        ffxQueryDescGetVersions versionQuery {};
        versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        versionQuery.device = device;
        uint64_t versionCount = 0;
        versionQuery.outputCount = &versionCount;

        ids.clear();
        names.clear();

        auto result = query(&versionQuery.header);

        if (result == FFX_API_RETURN_OK && versionCount > 0)
        {
            ids.resize(versionCount);
            names.resize(versionCount);
            versionQuery.versionIds = ids.data();
            versionQuery.versionNames = names.data();

            result = query(&versionQuery.header);

            ids.resize(result == FFX_API_RETURN_OK ? versionCount : 0);
            names.resize(ids.size());
        }

        return result;
    }

    // Versions of the FFX SDK upscalers and the upscaler of the driver
    static ffxReturnCode_t D3D12_QueryUpscalerVersions(ffxQueryDescGetVersions* desc,
                                                       FSR4DriverProviderDx12::Listing driverListing)
    {
        ffxQueryDescGetVersions sdkQuery = *desc;
        uint64_t sdkCount = 0;
        sdkQuery.outputCount = &sdkCount;
        sdkQuery.versionIds = nullptr;
        sdkQuery.versionNames = nullptr;

        if (auto result = D3D12_QuerySdk(nullptr, &sdkQuery.header); result != FFX_API_RETURN_OK)
            return result;

        std::vector<uint64_t> ids(sdkCount);
        std::vector<const char*> names(sdkCount);
        sdkQuery.versionIds = ids.data();
        sdkQuery.versionNames = names.data();

        if (auto result = D3D12_QuerySdk(nullptr, &sdkQuery.header); result != FFX_API_RETURN_OK)
            return result;

        ids.resize(sdkCount);
        names.resize(sdkCount);

        FSR4DriverProviderDx12::AddToVersionList(ids, names, driverListing);

        if (desc->outputCount == nullptr)
            return FFX_API_RETURN_ERROR_PARAMETER;

        // Same contract as FFX, a non-zero count is the capacity of the arrays
        auto count = ids.size();
        if (*desc->outputCount != 0)

        {
            count = std::min<size_t>(count, *desc->outputCount);

            for (size_t i = 0; i < count; i++)
            {
                if (desc->versionIds != nullptr)
                    desc->versionIds[i] = ids[i];

                if (desc->versionNames != nullptr)
                    desc->versionNames[i] = names[i];
            }
        }

        *desc->outputCount = count;
        return FFX_API_RETURN_OK;
    }

    template <typename QueryFn>
    static ffxReturnCode_t QueryUpscalerVersionsWithoutFsr4(ffxQueryDescGetVersions* desc, QueryFn query)
    {
        if (desc->outputCount == nullptr)
            return FFX_API_RETURN_ERROR_PARAMETER;

        ffxQueryDescGetVersions fullQuery = *desc;
        uint64_t fullCount = 0;
        fullQuery.outputCount = &fullCount;
        fullQuery.versionIds = nullptr;
        fullQuery.versionNames = nullptr;

        if (auto result = query(&fullQuery.header); result != FFX_API_RETURN_OK)
            return result;

        std::vector<uint64_t> ids(fullCount);
        std::vector<const char*> names(fullCount);
        fullQuery.versionIds = ids.data();
        fullQuery.versionNames = names.data();

        if (auto result = query(&fullQuery.header); result != FFX_API_RETURN_OK)
            return result;

        // Same contract as FFX, a non-zero count is the capacity of the arrays
        auto capacity = *desc->outputCount;
        uint64_t count = 0;

        for (size_t i = 0; i < fullCount && i < ids.size(); i++)
        {
            if (names[i] != nullptr && std::atoi(names[i]) >= 4)
            {
                LOG_DEBUG("Hiding upscaler version {} from the game", names[i]);
                continue;
            }

            if (capacity != 0)
            {
                if (count >= capacity)
                    break;

                if (desc->versionIds != nullptr)
                    desc->versionIds[count] = ids[i];

                if (desc->versionNames != nullptr)
                    desc->versionNames[count] = names[i];
            }

            count++;
        }

        *desc->outputCount = count;
        return FFX_API_RETURN_OK;
    }

  private:
    static std::optional<uint64_t> FindOverrideVersion(const ffxApiHeader* desc)
    {
        for (auto header = desc; header != nullptr; header = header->pNext)
        {
            if (header->type == FFX_API_DESC_TYPE_OVERRIDE_VERSION)
                return reinterpret_cast<const ffxOverrideVersion*>(header)->versionId;
        }

        return std::nullopt;
    }

    static ffxReturnCode_t D3D12_QuerySdk(ffxContext* context, ffxQueryDescHeader* desc)
    {
        auto type = GetIndirectType(desc);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainDX12;

        if (isFg && fg_dx12.dll != nullptr)
            return fg_dx12.Query(context, desc);
        else if (auto srContext = FindSrContext(context); type == FFXStructType::Upscaling && srContext.has_value())
            return srContext->Query(context, desc);
        else if (type == FFXStructType::Upscaling && upscaling_dx12.driverProvider && (context == nullptr || *context == nullptr))
            return DriverProviderQueryWithoutContext(upscaling_dx12, desc);
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

  public:
    static ffxReturnCode_t D3D12_Dispatch(ffxContext* context, const ffxDispatchDescHeader* desc)
    {
        if (FSR4DriverProviderDx12::OwnsContext(context))
            return FSR4DriverProviderDx12::Dispatch(context, desc);

        if (auto custom = CustomContextModule(context))
            return custom->Dispatch(context, desc);

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
    static HMODULE VkModule_SR() { return upscaling_vk.dll; }
    static HMODULE VkModule_FG() { return fg_vk.dll; }

    static std::wstring VkModule_Path() { return main_vk.filePath; }
    static std::wstring VkModule_SR_Path() { return upscaling_vk.filePath; }
    static std::wstring VkModule_FG_Path() { return fg_vk.filePath; }

    static bool VkModule_BackendV2() { return main_vk.vkBackendV2; }

    // Module that Opti runs its Vulkan upscaler with, the upscaler dll of SDK 2.x when it's loaded
    static FfxModule* VkFfxModule_SR()
    {
        auto module = VkEffectModule(upscaling_vk);
        return module != nullptr ? module : &main_vk;
    }

    // Module that Opti runs its Vulkan frame generation and swapchain with
    static FfxModule* VkFfxModule_FG()
    {
        auto module = VkEffectModule(fg_vk);
        return module != nullptr ? module : &main_vk;
    }

    // Module that runs the Vulkan upscaler with this version id
    static HMODULE UpscalerModuleVk(uint64_t versionId)
    {
        if (versionId != 0 && FSR4DriverProviderVk::IsAvailable() && versionId == FSR4DriverProviderVk::VersionId())
            return FSR4DriverProviderVk::Module()->dll;

        return VkFfxModule_SR()->dll;
    }

    static bool IsSRReadyVk()
    {
        return (main_vk.dll != nullptr && !main_vk.isLoader) || VkEffectModule(upscaling_vk) != nullptr;
    }

    static bool IsFGReadyVk()
    {
        return (main_vk.dll != nullptr && !main_vk.isLoader) || VkEffectModule(fg_vk) != nullptr;
    }

    static bool InitFfxVk(HMODULE module = nullptr)
    {
        InitFfxVkModule(main_vk, main_vk_hooked, module, { L"amd_fidelityfx_loader_vk.dll", L"amd_fidelityfx_vk.dll" },
                        Config::Instance()->FfxVkPath.value_or(L""));

        InitFfxVk_SR();
        InitFfxVk_FG();

        if (main_vk.isLoader && (upscaling_vk.dll == nullptr || fg_vk.dll == nullptr))
            InitFfxVkLegacy();

        bool loadResult = main_vk.CreateContext != nullptr || upscaling_vk.CreateContext != nullptr ||
                          fg_vk.CreateContext != nullptr || legacy_vk.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        return loadResult;
    }

    static bool InitFfxVk_SR(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (upscaling_vk.dll != nullptr && upscaling_vk.CreateContext != nullptr)
            return true;

        if (!InitFfxVkModule(upscaling_vk, upscaling_vk_hooked, module, { L"amd_fidelityfx_upscaler_vk.dll" },
                             Config::Instance()->FfxVkSRPath.value_or(L"")))
        {
            return false;
        }

        FSR4Presets::Hook(upscaling_vk.dll);
        FSR4Models::Hook(upscaling_vk.dll);

        return true;
    }

    static bool InitFfxVk_FG(HMODULE module = nullptr)
    {
        return InitFfxVkModule(fg_vk, fg_vk_hooked, module, { L"amd_fidelityfx_framegeneration_vk.dll" },
                               Config::Instance()->FfxVkFGPath.value_or(L""));
    }

    static feature_version VersionVk()
    {
        if (main_vk.version.major == 0)
            QueryVersionVk(main_vk, FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE, "");

        if (main_vk.version.major == 0 && VkEffectModule(upscaling_vk) != nullptr)
            main_vk.version = VersionVk_SR();

        if (main_vk.version.major == 0 && VkEffectModule(fg_vk) != nullptr)
            main_vk.version = VersionVk_FG();

        return main_vk.version;
    }

    static feature_version VersionVk_SR()
    {
        auto module = VkEffectModule(upscaling_vk);
        if (module == nullptr)
            return VersionVk();

        return QueryVersionVk(*module, FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE, "SR ");
    }

    static feature_version VersionVk_FG()
    {
        auto module = VkEffectModule(fg_vk);
        if (module == nullptr)
            return VersionVk();

        return QueryVersionVk(*module, FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION, "FG ");
    }

    static ffxReturnCode_t VULKAN_CreateContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                                const ffxAllocationCallbacks* memCb)
    {
        auto type = GetType(desc->type);
        auto overrideVersion = FindOverrideVersion(desc);

        FfxModule* driver = nullptr;
        if (overrideVersion && type == FFXStructType::Upscaling && FSR4DriverProviderVk::IsAvailable() &&
            *overrideVersion == FSR4DriverProviderVk::VersionId())
            driver = FSR4DriverProviderVk::Module();
        else if (overrideVersion && type == FFXStructType::FG && FSR4DriverProviderVk::IsFgAvailable() &&
                 *overrideVersion == FSR4DriverProviderVk::FgVersionId())
            driver = FSR4DriverProviderVk::FgModule();

        if (driver != nullptr)
        {
            // The driver FG logs an error without the SDK 2.x version
            ffxCreateContextDescFrameGenerationVersionV2 fgVersion {};
            fgVersion.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION_V2;
            fgVersion.version = FFX_FRAMEGENERATION_VERSION_V2;

            ffxApiHeader* last = desc;
            while (last->pNext != nullptr)
                last = last->pNext;

            if (type == FFXStructType::FG)
                last->pNext = &fgVersion.header;

            LOG_DEBUG("Creating with the driver provided {}", type == FFXStructType::FG ? "FG" : "upscaler");
            auto result = CreateContextVk(driver, context, desc, memCb);
            last->pNext = nullptr;

            if (result == FFX_API_RETURN_OK)
            {
                std::scoped_lock lock(vkDriverMutex);
                vkDriverContexts[*context] = driver;
            }

            return result;
        }

        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainVulkan;

        ffxReturnCode_t result = FFX_API_RETURN_ERROR;

        if (auto fg = VkEffectModule(fg_vk); isFg && fg != nullptr)
        {
            LOG_DEBUG("Creating with {}", VkModuleName(fg));
            result = CreateContextVk(fg, context, desc, memCb);
            contextToType[*context] = type;
            LOG_DEBUG("Created with {}: {:X}", VkModuleName(fg), (size_t) *context);
            return result;
        }
        else if (auto upscaling = VkEffectModule(upscaling_vk);
                 type == FFXStructType::Upscaling && upscaling != nullptr)
        {
            LOG_DEBUG("Creating with {}", VkModuleName(upscaling));
            result = CreateContextVk(upscaling, context, desc, memCb);
            contextToType[*context] = type;
            LOG_DEBUG("Created with {}: {:X}", VkModuleName(upscaling), (size_t) *context);
            return result;
        }

        const auto skipFG = isFg && fg_vk.skipCreateCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_vk.skipCreateCalls;

        if (main_vk.dll != nullptr && !skipFG && !skipUpscaling)
        {
            LOG_DEBUG("Creating with main_vk");

            if (isFg)
                fg_vk.skipCreateCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipCreateCalls = true;

            result = CreateContextVk(&main_vk, context, desc, memCb);

            if (isFg)
                fg_vk.skipCreateCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipCreateCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t VULKAN_DestroyContext(ffxContext* context, const ffxAllocationCallbacks* memCb)
    {
        if (auto driver = VkDriverContextModule(context))
        {
            {
                std::scoped_lock lock(vkDriverMutex);
                vkDriverContexts.erase(*context);
            }

            return driver->DestroyContext(context, memCb);
        }

        ffxReturnCode_t result = FFX_API_RETURN_ERROR;
        auto type = FFXStructType::Unknown;

        if (contextToType.contains(*context))
        {
            type = contextToType[*context];
            LOG_DEBUG("Found context type mapping: {}", magic_enum::enum_name(type));
            contextToType.erase(*context);
        }

        FfxModule* mapped = nullptr;

        switch (type)
        {
        case FFXStructType::Upscaling:
            mapped = VkEffectModule(upscaling_vk);
            break;

        case FFXStructType::FG:
        case FFXStructType::SwapchainVulkan:
            mapped = VkEffectModule(fg_vk);
            break;

        default:
            break;
        }

        if (mapped != nullptr)
        {
            LOG_DEBUG("Destroying with {}", VkModuleName(mapped));
            result = mapped->DestroyContext(context, memCb);
        }

        if (result == FFX_API_RETURN_OK)
            return result;

        if (main_vk.dll != nullptr && !_skipDestroyCalls)
        {
            LOG_DEBUG("Destroying with main_vk");
            _skipDestroyCalls = true;
            result = main_vk.DestroyContext(context, memCb);
            _skipDestroyCalls = false;
        }

        if (result == FFX_API_RETURN_OK)
            return result;

        if (upscaling_vk.dll != nullptr)
        {
            LOG_DEBUG("Destroying with upscaling_vk");
            result = upscaling_vk.DestroyContext(context, memCb);
        }

        if (result == FFX_API_RETURN_OK)
            return result;

        if (fg_vk.dll != nullptr)
        {
            LOG_DEBUG("Destroying with fg_vk");
            result = fg_vk.DestroyContext(context, memCb);
        }

        if (result == FFX_API_RETURN_OK)
            return result;

        if (legacy_vk.dll != nullptr)
        {
            LOG_DEBUG("Destroying with legacy_vk");
            result = legacy_vk.DestroyContext(context, memCb);
        }

        if (result == FFX_API_RETURN_OK)
            return result;

        LOG_ERROR("Failed to destroy context in any module");
        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t VULKAN_Configure(ffxContext* context, const ffxConfigureDescHeader* desc)
    {
        if (auto driver = VkDriverContextModule(context))
            return driver->Configure(context, desc);

        auto type = GetType(desc->type);

        if (type == FFXStructType::General && context != nullptr && contextToType.contains(*context))
            type = contextToType[*context];

        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainVulkan;

        if (auto fg = VkEffectModule(fg_vk); isFg && fg != nullptr)
            return fg->Configure(context, desc);
        else if (auto upscaling = VkEffectModule(upscaling_vk);
                 type == FFXStructType::Upscaling && upscaling != nullptr)
            return upscaling->Configure(context, desc);

        const auto skipFG = isFg && fg_vk.skipConfigureCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_vk.skipConfigureCalls;

        if (main_vk.dll != nullptr && !skipFG && !skipUpscaling)
        {
            if (isFg)
                fg_vk.skipConfigureCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipConfigureCalls = true;

            auto result = main_vk.Configure(context, desc);

            if (isFg)
                fg_vk.skipConfigureCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipConfigureCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    static ffxReturnCode_t VULKAN_Query(ffxContext* context, ffxQueryDescHeader* desc)
    {
        if (auto driver = VkDriverContextModule(context))
            return driver->Query(context, desc);

        if (desc->type == FFX_API_QUERY_DESC_TYPE_GET_VERSIONS && GetIndirectType(desc) == FFXStructType::Upscaling)
        {
            return VULKAN_QueryUpscalerVersions(reinterpret_cast<ffxQueryDescGetVersions*>(desc),
                                                DriverListing::WhenNewestUpscaler);
        }

        return VULKAN_QuerySdk(context, desc);
    }

    // Versions of the FFX SDK upscalers and the upscaler of the driver
    static ffxReturnCode_t VULKAN_QueryUpscalerVersions(ffxQueryDescGetVersions* desc, DriverListing driverListing)
    {
        if (desc->outputCount == nullptr)
            return FFX_API_RETURN_ERROR_PARAMETER;

        ffxQueryDescGetVersions sdkQuery = *desc;
        uint64_t sdkCount = 0;
        sdkQuery.outputCount = &sdkCount;
        sdkQuery.versionIds = nullptr;
        sdkQuery.versionNames = nullptr;

        std::vector<uint64_t> ids;
        std::vector<const char*> names;

        // Driver provided FSR 4 works without the FFX SDK
        auto sdkResult = VULKAN_QuerySdk(nullptr, &sdkQuery.header);

        if (sdkResult == FFX_API_RETURN_OK && sdkCount > 0)
        {
            ids.resize(sdkCount);
            names.resize(sdkCount);
            sdkQuery.versionIds = ids.data();
            sdkQuery.versionNames = names.data();

            sdkResult = VULKAN_QuerySdk(nullptr, &sdkQuery.header);

            ids.resize(sdkResult == FFX_API_RETURN_OK ? sdkCount : 0);
            names.resize(ids.size());
        }

        FSR4DriverProviderVk::AddToVersionList(ids, names, driverListing);

        if (ids.empty() && sdkResult != FFX_API_RETURN_OK)
            return sdkResult;

        // Same contract as FFX, a non-zero count is the capacity of the arrays
        auto count = ids.size();
        if (*desc->outputCount != 0)
        {
            count = std::min<size_t>(count, *desc->outputCount);

            for (size_t i = 0; i < count; i++)
            {
                if (desc->versionIds != nullptr)
                    desc->versionIds[i] = ids[i];

                if (desc->versionNames != nullptr)
                    desc->versionNames[i] = names[i];
            }
        }

        *desc->outputCount = count;
        return FFX_API_RETURN_OK;
    }

    // Versions of the FFX SDK frame generation followed by the frame generation of the driver.
    // A custom module is asked for its own instead.
    static ffxReturnCode_t VULKAN_QueryFgVersions(void* device, std::vector<uint64_t>& ids,
                                                  std::vector<const char*>& names, FfxModule* custom = nullptr)
    {
        auto query = [custom](ffxQueryDescHeader* header)
        { return custom != nullptr ? custom->Query(nullptr, header) : VULKAN_QuerySdk(nullptr, header); };

        ffxQueryDescGetVersions versionQuery {};
        versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        versionQuery.device = device;
        uint64_t versionCount = 0;
        versionQuery.outputCount = &versionCount;

        ids.clear();
        names.clear();

        // Driver provided ML FG works without the FFX SDK
        auto result = query(&versionQuery.header);

        if (result == FFX_API_RETURN_OK && versionCount > 0)
        {
            ids.resize(versionCount);
            names.resize(versionCount);
            versionQuery.versionIds = ids.data();
            versionQuery.versionNames = names.data();

            result = query(&versionQuery.header);

            ids.resize(result == FFX_API_RETURN_OK ? versionCount : 0);
            names.resize(ids.size());
        }

        if (custom == nullptr)
            FSR4DriverProviderVk::AddFgToVersionList(ids, names);

        return ids.empty() ? result : FFX_API_RETURN_OK;
    }

    static ffxReturnCode_t VULKAN_Dispatch(ffxContext* context, const ffxDispatchDescHeader* desc)
    {
        if (auto driver = VkDriverContextModule(context))
            return driver->Dispatch(context, desc);

        auto type = GetType(desc->type);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainVulkan;

        if (auto fg = VkEffectModule(fg_vk); isFg && fg != nullptr)
            return fg->Dispatch(context, desc);
        else if (auto upscaling = VkEffectModule(upscaling_vk);
                 type == FFXStructType::Upscaling && upscaling != nullptr)
            return upscaling->Dispatch(context, desc);

        const auto skipFG = isFg && fg_vk.skipDispatchCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_vk.skipDispatchCalls;

        if (main_vk.dll != nullptr && !skipFG && !skipUpscaling)
        {
            if (isFg)
                fg_vk.skipDispatchCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipDispatchCalls = true;

            auto result = main_vk.Dispatch(context, desc);

            if (isFg)
                fg_vk.skipDispatchCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipDispatchCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

    // Backend description type that the module takes, FFX SDK 2.x doesn't create a backend with the older type
    static uint64_t VkBackendType(const FfxModule* module)
    {
        return module->vkBackendV2 ? FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_V2
                                   : FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    }

  private:
    // Loads Opti's copy of a Vulkan FFX dll, falls back to the one of the game and hooks the game's one for inputs
    static bool InitFfxVkModule(FfxModule& module, FfxModule& hooked, HMODULE gameModule,
                                const std::vector<std::wstring>& dllNames, const std::wstring& overridePath)
    {
        // if dll already loaded
        if (module.dll != nullptr && module.CreateContext != nullptr)
            return true;

        spdlog::info("");

        // This is module loaded by game, need to hook it to use inputs
        if (gameModule != nullptr && hooked.dll == nullptr && !IsCustomModule(gameModule))
            hooked.dll = gameModule;

        // Try loading Opti's dlls first
        if (module.dll == nullptr)
        {
            auto optiPath = Config::Instance()->MainDllPath.value();

            for (const auto& dllName : dllNames)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllName));

                if (hooked.dll == nullptr)
                {
                    LoadSdkLibrary(dllName, optiPath, overridePath, &hooked.dll, &module.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    LoadSdkLibrary(dllName, optiPath, overridePath, &memModule, &module.dll);
                }

                if (module.dll != nullptr)
                    break;
            }
        }

        // Can't find Opti dlls, use just module loaded by game
        if (module.dll == nullptr && hooked.dll != nullptr)
            module = hooked;

        if (module.dll != nullptr)
        {
            auto modulePath = Util::ModulePath(module.dll);
            module.filePath = modulePath.wstring();
            module.isLoader = IsLoader(module.filePath);

            // Only the legacy FFX SDK 1.x dll keeps the old backend type
            module.vkBackendV2 = _wcsicmp(modulePath.filename().c_str(), L"amd_fidelityfx_vk.dll") != 0;

            LOG_INFO("Loaded from {}", wstring_to_string(module.filePath));
        }

        if (module.dll != nullptr && module.CreateContext == nullptr)
            GetMethods(module);

        // Game calls Opti's dll when it has none of its own, LoadLibrary hooks hand it Opti's
        if (hooked.dll != nullptr && hooked.CreateContext == nullptr)
            GetMethods(hooked);
        else if (hooked.CreateContext == nullptr)
            hooked = module;

        if (Config::Instance()->EnableFfxInputs.value_or_default() && hooked.dll != nullptr &&
            hooked.CreateContext != nullptr && !hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (hooked.Configure != nullptr)
                DetourAttach(&(PVOID&) hooked.Configure, ffxConfigure_Vk);

            if (hooked.CreateContext != nullptr)
                DetourAttach(&(PVOID&) hooked.CreateContext, ffxCreateContext_Vk);

            if (hooked.DestroyContext != nullptr)
                DetourAttach(&(PVOID&) hooked.DestroyContext, ffxDestroyContext_Vk);

            if (hooked.Dispatch != nullptr)
                DetourAttach(&(PVOID&) hooked.Dispatch, ffxDispatch_Vk);

            if (hooked.Query != nullptr)
                DetourAttach(&(PVOID&) hooked.Query, ffxQuery_Vk);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("DetourTransactionCommit result: {:X}", detourResult);
                hooked.Configure = nullptr;
                hooked.CreateContext = nullptr;
                hooked.DestroyContext = nullptr;
                hooked.Dispatch = nullptr;
                hooked.Query = nullptr;
            }
            else
            {
                // If we are hooking Opti's dll then we need to use original method ptrs
                if (module.dll == hooked.dll)
                {
                    module.Configure = hooked.Configure;
                    module.CreateContext = hooked.CreateContext;
                    module.DestroyContext = hooked.DestroyContext;
                    module.Dispatch = hooked.Dispatch;
                    module.Query = hooked.Query;
                }

                hooked.hooked = true;
                State::Instance().fsrHooks = true;

                // Pin the dll so the hooks stay valid if the game unloads it
                HMODULE pinned = nullptr;
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                   (LPCWSTR) hooked.dll, &pinned);
            }
        }

        bool loadResult = module.CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (loadResult)
        {
            // Pin the dll so the method pointers stay valid if the game unloads it
            HMODULE pinned = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               (LPCWSTR) module.dll, &pinned);
        }
        else
        {
            module.dll = nullptr;
        }

        return loadResult;
    }

    // Module of an effect, amd_fidelityfx_vk.dll stands in for its SDK 2.x dll when the main module is the loader,
    // which can only forward to the SDK 2.x dlls
    static FfxModule* VkEffectModule(FfxModule& module)
    {
        if (module.dll != nullptr)
            return &module;

        if (main_vk.isLoader && legacy_vk.dll != nullptr)
            return &legacy_vk;

        return nullptr;
    }

    static std::string VkModuleName(const FfxModule* module)
    {
        if (module == &upscaling_vk)
            return "upscaling_vk";

        if (module == &fg_vk)
            return "fg_vk";

        if (module == &legacy_vk)
            return "legacy_vk";

        return "main_vk";
    }

    // Only used by Opti, the game's amd_fidelityfx_vk.dll gets the loader through the LoadLibrary hooks
    static void InitFfxVkLegacy()
    {
        if (legacy_vk.dll != nullptr)
            return;

        LOG_DEBUG("Trying to load amd_fidelityfx_vk.dll for the missing FFX SDK 2.x dlls");

        HMODULE memModule = nullptr;
        LoadSdkLibrary(L"amd_fidelityfx_vk.dll", Config::Instance()->MainDllPath.value(),
                       Config::Instance()->FfxVkPath.value_or(L""), &memModule, &legacy_vk.dll);

        if (legacy_vk.dll == nullptr)
            return;

        auto modulePath = Util::ModulePath(legacy_vk.dll);
        legacy_vk.filePath = modulePath.wstring();
        legacy_vk.isLoader = IsLoader(legacy_vk.filePath);
        legacy_vk.vkBackendV2 = _wcsicmp(modulePath.filename().c_str(), L"amd_fidelityfx_vk.dll") != 0;
        GetMethods(legacy_vk);

        if (legacy_vk.isLoader || legacy_vk.CreateContext == nullptr)
        {
            LOG_WARN("{} can't stand in for the FFX SDK 2.x dlls", wstring_to_string(legacy_vk.filePath));
            legacy_vk = {};
            return;
        }

        LOG_INFO("Loaded from {}", wstring_to_string(legacy_vk.filePath));

        // Pin the dll so the method pointers stay valid if the game unloads it
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCWSTR) legacy_vk.dll, &pinned);
    }

    static CustomFfxModule* LoadCustom(std::map<std::string, CustomFfxModule>& customs, const std::string& name,
                                       CustomProviders::Kind kind)
    {
        std::scoped_lock lock(customMutex);

        auto [it, inserted] = customs.try_emplace(name);
        auto& module = it->second.module;

        if (inserted)
        {
            it->second.name = name;
            auto exports = CustomProviders::Load(name, kind);

            if (!exports)
                return nullptr;

            module.dll = exports.dll;
            module.filePath = exports.path.wstring();
            module.vkBackendV2 = !exports.IsLegacyFfxVk();

            // FSR4 presets and models are patched in the dll, a plugin has neither
            if (module.dll != nullptr)
            {
                FSR4Presets::Hook(module.dll);
                FSR4Models::Hook(module.dll);
            }

            GetMethods(module, exports);
        }

        return module.CreateContext != nullptr && module.Query != nullptr ? &it->second : nullptr;
    }

    static CustomFfxModule* FindCustom(std::map<std::string, CustomFfxModule>& customs, const std::string& name)
    {
        std::scoped_lock lock(customMutex);

        auto it = customs.find(name);
        return it != customs.end() && it->second.module.CreateContext != nullptr ? &it->second : nullptr;
    }

    static void GetMethods(FfxModule& module) { GetMethods(module, { .dll = module.dll }); }

    static void GetMethods(FfxModule& module, const CustomProviders::Exports& exports)
    {
        module.Configure = (PfnFfxConfigure) exports("ffxConfigure");
        module.CreateContext = (PfnFfxCreateContext) exports("ffxCreateContext");
        module.DestroyContext = (PfnFfxDestroyContext) exports("ffxDestroyContext");
        module.Dispatch = (PfnFfxDispatch) exports("ffxDispatch");
        module.Query = (PfnFfxQuery) exports("ffxQuery");
    }

    static feature_version QueryVersionVk(FfxModule& module, uint64_t createDescType, const char* label)
    {
        if (module.version.major != 0 || module.Query == nullptr)
            return module.version;

        ffxQueryDescGetVersions versionQuery {};
        versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versionQuery.createDescType = createDescType;
        uint64_t versionCount = 0;
        versionQuery.outputCount = &versionCount;

        auto queryResult = module.Query(nullptr, &versionQuery.header);

        // get number of versions for allocation
        if (versionCount == 0 || queryResult != FFX_API_RETURN_OK)
        {
            LOG_WARN("Vulkan {}Query result: {}", label, (UINT) queryResult);
            return module.version;
        }

        std::vector<uint64_t> versionIds(versionCount);
        std::vector<const char*> versionNames(versionCount);
        versionQuery.versionIds = versionIds.data();
        versionQuery.versionNames = versionNames.data();

        queryResult = module.Query(nullptr, &versionQuery.header);

        if (queryResult == FFX_API_RETURN_OK)
        {
            module.version.parse_version(versionNames[0]);
            LOG_INFO("FfxApi Vulkan {}version: {}.{}.{}", label, module.version.major, module.version.minor,
                     module.version.patch);
        }
        else
        {
            LOG_WARN("Vulkan {}Query 2 result: {}", label, (UINT) queryResult);
        }

        return module.version;
    }

    static FfxModule* VkDriverContextModule(ffxContext* context)
    {
        if (context == nullptr)
            return nullptr;

        std::scoped_lock lock(vkDriverMutex);
        auto it = vkDriverContexts.find(*context);
        return it != vkDriverContexts.end() ? it->second : nullptr;
    }

    // Create descs of the game carry the backend type of the SDK it was built with, the layouts of both are the same
    static ffxReturnCode_t CreateContextVk(FfxModule* module, ffxContext* context, ffxCreateContextDescHeader* desc,
                                           const ffxAllocationCallbacks* memCb)
    {
        ffxApiHeader* backend = nullptr;
        ffxStructType_t originalType = 0;

        for (ffxApiHeader* header = desc; header != nullptr; header = header->pNext)
        {
            if (header->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK ||
                header->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_V2)
            {
                backend = header;
                originalType = header->type;
                header->type = VkBackendType(module);
                break;
            }
        }

        auto result = module->CreateContext(context, desc, memCb);

        if (backend != nullptr)
            backend->type = originalType;

        return result;
    }

    static ffxReturnCode_t VULKAN_QuerySdk(ffxContext* context, ffxQueryDescHeader* desc)
    {
        auto type = GetIndirectType(desc);
        auto isFg = type == FFXStructType::FG || type == FFXStructType::SwapchainVulkan;

        if (auto fg = VkEffectModule(fg_vk); isFg && fg != nullptr)
            return fg->Query(context, desc);
        else if (auto upscaling = VkEffectModule(upscaling_vk);
                 type == FFXStructType::Upscaling && upscaling != nullptr)
            return upscaling->Query(context, desc);

        const auto skipFG = isFg && fg_vk.skipQueryCalls;
        const auto skipUpscaling = type == FFXStructType::Upscaling && upscaling_vk.skipQueryCalls;

        if (main_vk.dll != nullptr && !skipFG && !skipUpscaling)
        {
            if (isFg)
                fg_vk.skipQueryCalls = true;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipQueryCalls = true;

            auto result = main_vk.Query(context, desc);

            if (isFg)
                fg_vk.skipQueryCalls = false;
            else if (type == FFXStructType::Upscaling)
                upscaling_vk.skipQueryCalls = false;

            return result;
        }

        return FFX_API_RETURN_NO_PROVIDER;
    }

  public:
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

        case 0x01000003u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_V2 ({:X})", type);

        case 0x01000004u:
            return std::format("CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_ALLOCATION_CALLBACKS ({:X})", type);

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

        case 0x0001000bu:
            return std::format("CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION ({:X})", type);

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

        case 0x0002000eu:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION ({:X})", type);

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

        case 0x0004000au:
            return std::format("CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_VK ({:X})", type);

        case 0x0004000bu:
            return std::format("QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_GPU_MEMORY_USAGE_VK_V2 ({:X})", type);

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
