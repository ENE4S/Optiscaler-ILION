// ILION adaptation: AMD NR only; optional GI and presentation paths excluded.
// Copyright (c) 2026 3zwr1 (AMDNR)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "AmdBridge.h"
#include "AmdPreSr.h"
#include "AmdLayout.h"   // (0.3.4, danielblnc support) KnobsUnmapped / QualityMapped: the danielblnc capability answers
#include "ComIdentity.h" // SameD3D12Device (the device gate, 0.3.3.2)
#include "BridgeRules.h" // (0.3.4) the settle rule (P10) and the self-TerminateProcess stamp rule (P22), pure
#include "DynamicNr.h"
#include "GpuSupportRules.h" // (0.3.4) the GPU table and the APU defaults (pure, tests\034)
#include <dlssnr/lmxxf/LmxxfBackend.h>
#include <dlssnr/lmxxf/LmxxfTierPolicy.h> // (0.3.4) LmxxfTierSnapDefault (pure)
#include "SystemCompiler.h"
#include <misc/IdentifyGpu.h>
#include <State.h>
#include <upscalers/IFeature.h>
#include <Util.h>
#include <misc/SkipSpoof.h>
#include <misc/ProcessNameMatch.h> // (0.3.4, FB-L9) danielblnc's installer by name (W1-F's rule)
#include <misc/LogRotationNames.h> // (0.3.4, FB-L11B) SettleLogGate: the status log-write dedupe
#include <algorithm>
#include <detours/detours.h>
#include <dxgi1_4.h>
#include <Psapi.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>
#include "resource.h" // "ILION " ILION_VERSION_STR " NR experimental" (ProductName)

// Logger.cpp: this process's session header line (0.3.3.2); empty before BeginLogSession.
static std::string SessionHeaderText() { return "ILION experimental AMD NR"; }
// Logger.cpp (0.3.4, FB-L7): stamps this session's running marker from the Exit hook; kernel32 only, no allocation.
static void NoteSessionExitHook(long) noexcept {}
// Logger.cpp (0.3.4, FB-L7, G1 review): the second stamp, once the hook's own exit work below has returned.
static void NoteSessionExitDone(const char*, unsigned long long) noexcept {}

namespace DlssNr::AmdBridge
{
namespace
{
// amd_bridge.log, opened for one append. The first open in this process writes the session header
// first (0.3.3.2): which build, exe and pid wrote the lines below.
std::ofstream OpenBridgeLog()
{
    std::ofstream log(Util::DllPath().parent_path() / L"amd_bridge.log", std::ios::app);
    static std::atomic<bool> headerWritten { false };
    if (log && !headerWritten.exchange(true))
        log << GetTickCount64() << " " << SessionHeader() << '\n';
    return log;
}
std::atomic<AmdPreSr::NeuralBackend*> backend { nullptr };
std::atomic<int> activeRuntime { 0 };
// A rendered NR output counts as active only after the actual submit queue completes it.
std::atomic<ID3D12CommandList*> activityCandidate { nullptr };
std::mutex activityMutex;
Microsoft::WRL::ComPtr<ID3D12Fence> activityFence;
UINT64 activitySerial = 0;
std::vector<std::pair<UINT64, ULONGLONG>> activityPending;
ULONGLONG activityCompletedAt = 0;
void PollActivityLocked()
{
    if (!activityFence) return;
    const UINT64 done = activityFence->GetCompletedValue();
    if (done == UINT64_MAX) { activityPending.clear(); activityCompletedAt = 0; return; }
    auto it = activityPending.begin();
    while (it != activityPending.end() && it->first <= done) {
        activityCompletedAt = it->second;
        ++it;
    }
    activityPending.erase(activityPending.begin(), it);
}
void ActivitySubmitted(ID3D12CommandQueue* queue)
{
    std::lock_guard lock(activityMutex);
    if (!activityFence) {
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) ||
            FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&activityFence)))) return;
    }
    PollActivityLocked();
    const UINT64 serial = ++activitySerial;
    if (SUCCEEDED(queue->Signal(activityFence.Get(), serial))) {
        if (activityPending.size() == 128) activityPending.erase(activityPending.begin());
        activityPending.emplace_back(serial, GetTickCount64());
    }
}
 // NeuralRuntime of the backend that was built: 0 none, 1 daniel, 2 lmxxf
// Set by the RtlExitUserProcess hook (Exit) as its first instruction and never reset (C1-A, 0.3.3.2 rebuild): from then
// on Run records nothing and the upscaler gets the title's colour. State::isShuttingDown cannot serve: DLL_PROCESS_DETACH,
// which sets it, runs after this hook.
std::atomic<bool> exiting { false };
using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using ExitFn = void(NTAPI*)(LONG);
ExecuteFn executeOriginal = nullptr;
ExitFn exitOriginal = nullptr;
// (0.3.3.2, [DlssNr] AmdDeviceGate) The device the backend was built on (one reference, never released:
// the backend owns it for the process's lifetime too). Null until a backend exists.
std::atomic<ID3D12Device*> backendDevice { nullptr };
// (0.3.4, P1, [DlssNr] AmdStreamlineDeviceFix) danielblnc's backend runs on the device behind the game's proxy device
// (ChooseNrDevice): set once, before the backend is published. Read by its Submitting (NrOnDeviceBehindProxy).
std::atomic<bool> nrOnDeviceBehindProxy { false };
// (0.3.4, P1) The refusal text when danielblnc cannot be given a consistent device and queue (NrDeviceRefusal).
std::mutex refusalMutex;
std::string nrDeviceRefusal;
// (0.3.3.2, [DlssNr] AmdNeuralListRecovery) DISCARD EVIDENCE. Both runtimes record into the game's list
// and act when it is executed; a list the game resets without executing it never comes (see
// AmdPreSr.cpp, RecoverDiscarded). ID3D12GraphicsCommandList::Reset is hooked once, when the backend is
// built, for the DIRECT and COMPUTE list implementations of its device (one detour when they share it).
// The hook forwards only the list NR last recorded into (`watchedList`, armed after each Record, disarmed
// by its first successful Reset): every other Reset in the game costs one relaxed load and a compare.
// It never takes a lock: the runtimes reset their own lists while their backend's lock is held.
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
ResetFn resetOriginal[2] = { nullptr, nullptr };
std::atomic<ID3D12CommandList*> watchedList { nullptr };
std::atomic<bool> listRecovery { false };
void NoteListReset(ID3D12CommandList* list)
{
    CancelActivity(list);
    ID3D12CommandList* expected = list;
    if (!watchedList.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel))
        return; // another thread took it, or the next Record armed another list
    if (auto b = backend.load())
        b->ListReset(list, false);
}
HRESULT STDMETHODCALLTYPE ResetDirect(ID3D12GraphicsCommandList* l, ID3D12CommandAllocator* a, ID3D12PipelineState* s)
{
    const HRESULT hr = resetOriginal[0](l, a, s);
    if (SUCCEEDED(hr) && l && static_cast<ID3D12CommandList*>(l) == watchedList.load(std::memory_order_relaxed))
        NoteListReset(l);
    return hr;
}
HRESULT STDMETHODCALLTYPE ResetCompute(ID3D12GraphicsCommandList* l, ID3D12CommandAllocator* a, ID3D12PipelineState* s)
{
    const HRESULT hr = resetOriginal[1](l, a, s);
    if (SUCCEEDED(hr) && l && static_cast<ID3D12CommandList*>(l) == watchedList.load(std::memory_order_relaxed))
        NoteListReset(l);
    return hr;
}
// D3D11 and Vulkan titles reach the pass through the D3D12 bridge too, so no API is named here.
std::string message = "AMD pre-SR: waiting for the first upscaler frame";
std::mutex messageMutex;
std::mutex initMutex;
std::mutex observedMutex;
std::unordered_set<ID3D12CommandList*> observedLists;
// (0.3.4, FB-L11B) Which status texts reach amd_bridge.log. The menu status (`message`) takes every text as in
// 0.3.3.2; only the log write is deduped: Run() clears the status every frame before posting that frame's, so the old
// "differs from the current status" test wrote a settle's "waiting for resolution settings to settle" about 45 times
// per NR change. Now a text is written once per settle (LogRotationNames.h, SettleLogGate). Under messageMutex.
LogRotationNames::SettleLogGate messageLogGate;
void Message(const char* s)
{
    std::lock_guard l(messageMutex);
    if (messageLogGate.ShouldLog(s))
    {
        auto log = OpenBridgeLog();
        log << GetTickCount64() << " thread=" << GetCurrentThreadId() << " " << s << '\n';
    }
    message = s;
}
// Run()'s status clear (0.3.3.2's Message(""), which never wrote a line): the menu status is emptied on every call as
// before, but the log gate sees one "" per presented frame, as its contract assumes (0.3.4, FB-L11B, G1 review). With
// [DlssNr] AmdOneStreamPerFrame engaged a present calls Run() once per NR entry, the chosen one and each one passed
// through, so a "" per call put two in a row between a settle's texts and the settle was written every present again.
// A present is a new `epoch` (AmdBridge.h). Epoch 0 (unknown), and an epoch that does not advance (more calls in it
// than a present has entries), count every call as a frame: the 0.3.3.2 cadence.
void ClearStatus(unsigned long long epoch)
{
    constexpr unsigned kStuckEpochCalls = 8;
    static unsigned long long clearedEpoch = 0; // under messageMutex
    static unsigned sameEpochCalls = 0;         // calls after the first in clearedEpoch, saturating
    std::lock_guard l(messageMutex);
    message.clear();
    if (epoch == 0 || epoch != clearedEpoch)
    {
        clearedEpoch = epoch;
        sameEpochCalls = 0;
        messageLogGate.ShouldLog("");
    }
    else if (sameEpochCalls >= kStuckEpochCalls || ++sameEpochCalls >= kStuckEpochCalls)
    {
        messageLogGate.ShouldLog("");
    }
}
// A line in amd_bridge.log that is not the status: Message() also puts its text in the menu
// until the next frame clears it, which a one-off fact does not need.
void LogLine(const std::string& s)
{
    std::lock_guard l(messageMutex);
    auto log = OpenBridgeLog();
    log << GetTickCount64() << " thread=" << GetCurrentThreadId() << " " << s << '\n';
}
// SetCompositionNote / CompositionNote: written by a backend on its recording thread, read by
// the menu on the present thread.
std::mutex compositionNoteMutex;
std::string compositionNote;
// DANIELBLNC'S STANDALONE BESIDE AMDNR (0.3.3.2; GTA V Enhanced: his v0.3.1 as version.dll, God of War
// Ragnarok: his 0.4 builds as winhttp.dll, version.dll and dxgi.dll). It hooks the game on its own, shares
// dlssnr_on_amd.ini/.log with the copy AMDNR hosts, and its own overlay only switches itself, so
// testers toggled the idle copy and saw "no change". Warn only: the other module is never touched.
std::mutex foreignMutex;
std::string foreignStandalone;
void CheckForeignStandalone()
{
    try
    {
        // AMDNR's own module is skipped by identity, not by name: OptiScaler.dll carries his overlay text
        // and the ini name too, and it may itself be version.dll, winhttp.dll or dxgi.dll.
        wchar_t ownW[MAX_PATH] {};
        HMODULE self = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&CheckForeignStandalone), &self))
            GetModuleFileNameW(self, ownW, MAX_PATH);
        const std::filesystem::path own(ownW);
        std::vector<std::filesystem::path> dirs { Util::ExePath().parent_path() };
        {
            std::error_code ec;
            const auto dllDir = Util::DllPath().parent_path();
            if (!std::filesystem::equivalent(dirs[0], dllDir, ec))
                dirs.push_back(dllDir);
        }
        static const wchar_t* const kProxyNames[] = { L"version", L"winhttp", L"dxgi",    L"d3d12", L"dinput8",
                                                      L"winmm",   L"dbghelp", L"wininet", L"d3d11" };
        std::string found, files;
        // (0.3.4, FB-L9) His installer under any name it ships with (dlssnr_on_amd_setup*.exe, for example
        // dlssnr_on_amd_setup_v0.3.1-re-engine-test.exe): the rule is ProcessNameMatch::IsAmdSetupExe, the one copy
        // the passthrough list uses too. 0.3.3.2's exact name is still asked for on its own, so the check never finds
        // less than 0.3.3.2 did.
        std::vector<std::string> setups;
        const auto noteSetup = [&setups](const std::string& fileName) {
            if (std::find(setups.begin(), setups.end(), fileName) == setups.end())
                setups.push_back(fileName);
        };
        for (const auto& dir : dirs)
        {
            std::error_code ec;
            bool setupHere = false;
            std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec);
            for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
            {
                const std::wstring fileName = it->path().filename().wstring();
                std::error_code typeEc;
                if (ProcessNameMatch::IsAmdSetupExe(fileName) && it->is_regular_file(typeEc))
                {
                    setupHere = true;
                    noteSetup(wstring_to_string(fileName));
                }
            }
            if (!setupHere && std::filesystem::exists(dir / L"dlssnr_on_amd_setup.exe", ec))
                noteSetup("dlssnr_on_amd_setup.exe");
            for (const auto* name : kProxyNames)
            {
                const auto file = dir / (std::wstring(name) + L".dll");
                if (!std::filesystem::is_regular_file(file, ec))
                    continue;
                if (!own.empty() && std::filesystem::equivalent(file, own, ec))
                    continue;
                const std::string version = AmdPreSr::DanielblncBuildOf(file);
                if (version.empty())
                    continue;
                const std::string fileName = wstring_to_string(file.filename().wstring());
                found += (found.empty() ? "" : ", ") + fileName + " (" + version + ")";
                files += (files.empty() ? "" : ", ") + fileName;
            }
        }
        std::string setupList;
        for (const auto& fileName : setups)
            setupList += (setupList.empty() ? "" : ", ") + fileName;
        std::string text;
        if (!found.empty())
            text = "danielblnc's standalone DLSS-NR on AMD is also installed here: " + found +
                   ". AMDNR runs his runtime itself (dlssnr_amd_pass1..3.dll) and does not use this copy: it hooks the game "
                   "on its own, shares dlssnr_on_amd.ini/.log, and its own overlay only switches itself. Remove " +
                   files + (!setups.empty() ? " (and " + setupList + ")." : ".");
        else if (setups.size() == 1)
            text = "danielblnc's installer " + setupList + " is in the game folder. Do not run it here: it installs "
                   "his standalone DLSS-NR on AMD next to AMDNR, which already runs his runtime as dlssnr_amd_pass1..3.dll. "
                   "Remove it.";
        else if (!setups.empty())
            text = "danielblnc's installers " + setupList + " are in the game folder. Do not run them here: they install "
                   "his standalone DLSS-NR on AMD next to AMDNR, which already runs his runtime as dlssnr_amd_pass1..3.dll. "
                   "Remove them.";
        if (text.empty())
            return;
        LogLine("AMD neural: " + text);
        LOG_WARN("AMD neural: {}", text);
        std::lock_guard l(foreignMutex);
        foreignStandalone = text;
    }
    catch (...)
    {
        // A check that cannot read the folder says nothing; it must never end the process.
    }
}
// Whether a colour format can hold linear HDR. The NVIDIA path's list (DlssNr_Dx12.cpp
// FormatCanHoldLinearHdr: only a float format can; an 8- or 10-bit UNORM frame is finished,
// display-referred output, and HDR10 is PQ-encoded) plus the shared-exponent format.
bool ColourHoldsLinearHdr(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
        return true;
    default:
        return false;
    }
}
thread_local NVSDK_NGX_Parameter* replacedParams = nullptr;
thread_local ID3D12Resource* originalColour = nullptr;
// The replacement itself (C3-A, 0.3.3.2 rebuild): what Run set as the colour, for the upscaler wrappers' identity test
// (Replacement). Set and cleared with the two above.
thread_local ID3D12Resource* replacementColour = nullptr;
struct FrameIdentity
{
    ID3D12Resource* colour = nullptr;
    ID3D12Resource* motion = nullptr;
    ID3D12Resource* depth = nullptr;
    UINT width = 0, height = 0;
};
std::mutex frameMutex;
FrameIdentity lastFrame {};
UINT stableFrames = 0;
void ExecuteBatch(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* c)
{
    auto b = backend.load();
    if (b)
        b->Submitting(q, n, c);
    // Execute every game list exactly once. Private runtime Notify callbacks
    // publish HIP jobs afterwards and have their internal ECL call neutralized.
    executeOriginal(q, n, c);
    {
        std::lock_guard guard(observedMutex);
        if (observedLists.size() > 256)
            observedLists.clear();
        for (UINT i = 0; i < n; ++i)
            observedLists.insert(c[i]);
    }
    if (b)
        b->Submitted(q, n, c);
    for (UINT i = 0; i < n; ++i) {
        ID3D12CommandList* expected = c[i];
        if (activityCandidate.compare_exchange_strong(expected, nullptr)) ActivitySubmitted(q);
    }
}
void STDMETHODCALLTYPE Execute(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* c)
{
    auto b = backend.load();
    int index = b ? b->PendingListIndex(n, c) : -1;
    if (n > 1 && index >= 0)
    {
        // Separate Execute calls establish an execution boundary around the
        // interop list. Preserve list order and execute each list exactly once.
        //
        // Noted ONCE. This used to be the per-frame status message, and Run() clears the
        // message every frame, so the bridge status flipped between this line and the
        // backend's counters line on alternate frames. Two things followed: Message()
        // logs every change, so amd_bridge.log grew a line per frame (191,919 lines, 14 MB,
        // in one Silent Hill 2 session - an open/append/close on the render thread each
        // frame), and the menu's status text changed length every frame, which is the
        // menu flicker testers saw. The fact is worth one line, not sixty a second.
        static bool saidIsolated = false;
        if (!saidIsolated)
        {
            saidIsolated = true;
            auto log = OpenBridgeLog();
            log << GetTickCount64() << " thread=" << GetCurrentThreadId()
                << " AMD isolated neural command list from a render batch (noted once)\n";
        }
        if (index) ExecuteBatch(q, static_cast<UINT>(index), c);
        ExecuteBatch(q, 1, c + index);
        auto remaining = n - static_cast<UINT>(index) - 1;
        if (remaining) ExecuteBatch(q, remaining, c + index + 1);
        return;
    }
    ExecuteBatch(q, n, c);
}
// EXIT (C1-A, 0.3.3.2 rebuild). NR stops at the first instruction: `exiting` turns Run into a pass-through (ExecuteBatch is
// NOT gated - a list recorded before exit still gets Submitting/Submitted, and danielblnc's Notify publishes the job its
// recorded GPU wait expects). The backend's Shutdown gets about 2 s for its lock and lmxxf's queue drain is bounded the
// same way; a busy runtime is left to the process, and neither runtime session is destroyed here. Nothing may throw out
// of this hook, and its one amd_bridge.log line never waits for the log's lock.
void NTAPI Exit(LONG code)
{
    exiting.store(true, std::memory_order_release);
    // (0.3.4, FB-L7) The game quit through its own exit call: the running marker says so before anything below can
    // stall, and says again once the work below has returned (NoteSessionExitDone). Only with both stamps is a
    // session that does not reach DLL_PROCESS_DETACH noted at the next start as quitting rather than warned about: a
    // crash or hang in AMDNR's own shutdown still warns. A few kernel32 file calls on stack buffers each, no
    // allocation, no lock (Logger.cpp).
    NoteSessionExitHook(code);
    const ULONGLONG exitStart = GetTickCount64();
    const char* exitWork = "no NR runtime was running";
    if (auto b = backend.load())
    {
        const ULONGLONG t0 = GetTickCount64();
        const char* result = "left to the process (busy, never started, or its lock not free within 2 s)";
        exitWork = "NR shutdown: left to the process";
        try
        {
            if (b->Shutdown())
            {
                result = "stopped";
                exitWork = "NR shutdown: stopped";
            }
        }
        catch (...)
        {
            result = "threw (ignored)";
            exitWork = "NR shutdown: threw (ignored)";
        }
        try
        {
            std::unique_lock<std::mutex> l(messageMutex, std::try_to_lock);
            if (l.owns_lock())
            {
                const int rt = activeRuntime.load();
                auto log = OpenBridgeLog();
                log << GetTickCount64() << " thread=" << GetCurrentThreadId() << " AMD neural: exit - "
                    << (rt == 2 ? "lmxxf" : rt == 1 ? "danielblnc" : "no runtime") << " " << result << " in "
                    << (GetTickCount64() - t0) << " ms; no NR from here on\n";
            }
        }
        catch (...)
        {
        }
    }
    NoteSessionExitDone(exitWork, GetTickCount64() - exitStart);
    exitOriginal(code);
}
// (0.3.4, P22) THE GAME ENDING ITSELF. Forza Horizon 6 quits without reaching the RtlExitUserProcess hook above, so its
// running marker kept no stamp and a normal quit was warned about at the next start ("No clean exit recorded"). The
// game terminating its own process with exit code 0 (kernelbase!TerminateProcess) now stamps the marker as a quit:
// both FB-L7 stamps, the second naming the call and that AMDNR's NR shutdown was skipped by the game. Another
// process's handle and a non-zero code (a crash handler's exit) pass through unstamped (BridgeRules.h). Nothing else
// is done here: the process ends with this call.
using TerminateFn = BOOL(WINAPI*)(HANDLE, UINT);
TerminateFn terminateOriginal = nullptr;
BOOL WINAPI TerminateSelf(HANDLE process, UINT code)
{
    if (DlssNr::BridgeRules::SelfTerminateIsQuit(process, code))
    {
        NoteSessionExitHook(static_cast<long>(code));
        NoteSessionExitDone("the game ended itself with TerminateProcess (exit code 0); AMDNR's NR shutdown was skipped "
                            "by the game",
                            0);
    }
    return terminateOriginal(process, code);
}
std::filesystem::path Directory() { return Util::DllPath().parent_path(); }
// Written by the lmxxf backend on a Vulkan title while its first answer is outstanding
// (LmxxfBackend.cpp, kVkLaunchMarker - the same name).
constexpr wchar_t kVkLaunchMarker[] = L"lmxxf_vk_launch.pending";
ID3D12Resource* Resource(NVSDK_NGX_Parameter* p, const char* name)
{
    ID3D12Resource* r = nullptr;
    if (p->Get(name, &r) != NVSDK_NGX_Result_Success)
        p->Get(name, reinterpret_cast<void**>(&r));
    return r;
}
bool IsAmd(ID3D12Device* d)
{
    struct PhysicalAdapterScope
    {
        uint64_t id = SkipSpoof::AddEntry(SkipSpoofType::Thread);
        ~PhysicalAdapterScope() { SkipSpoof::RemoveEntry(id); }
    } physicalAdapterScope;
    IDXGIFactory4* f = nullptr;
    IDXGIAdapter1* a = nullptr;
    bool amd = false;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f))))
    {
        if (SUCCEEDED(f->EnumAdapterByLuid(d->GetAdapterLuid(), IID_PPV_ARGS(&a))))
        {
            DXGI_ADAPTER_DESC1 desc {};
            amd = SUCCEEDED(a->GetDesc1(&desc)) && desc.VendorId == 0x1002;
            LOG_INFO("AMD pre-SR physical adapter vendor: {:04X}, AMD: {}", desc.VendorId, amd);
            a->Release();
        }
        f->Release();
    }
    return amd;
}
// (0.3.3.2) The Reset hook of the discard evidence (see ResetDirect), installed once with the backend.
// Its own transaction after the submission hooks: if it cannot be installed, NR runs as before, without
// the evidence (the backends then only learn of a drop from OptiScaler's own bridges and a released list).
void InstallResetHook(ID3D12Device* device)
{
    const D3D12_COMMAND_LIST_TYPE types[2] = { D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE };
    for (int i = 0; i < 2; ++i)
    {
        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12GraphicsCommandList* probe = nullptr;
        if (SUCCEEDED(device->CreateCommandAllocator(types[i], IID_PPV_ARGS(&allocator))) &&
            SUCCEEDED(device->CreateCommandList(0, types[i], allocator, nullptr, IID_PPV_ARGS(&probe))))
            resetOriginal[i] = reinterpret_cast<ResetFn>((*reinterpret_cast<void***>(probe))[10]);
        if (probe)
            probe->Release();
        if (allocator)
            allocator->Release();
    }
    if (resetOriginal[1] == resetOriginal[0])
        resetOriginal[1] = nullptr; // one implementation for both types: one detour
    LONG err = resetOriginal[0] || resetOriginal[1] ? DetourTransactionBegin() : ERROR_NOT_FOUND;
    if (err == NO_ERROR)
        err = DetourUpdateThread(GetCurrentThread());
    if (err == NO_ERROR && resetOriginal[0])
        err = DetourAttach(reinterpret_cast<PVOID*>(&resetOriginal[0]), ResetDirect);
    if (err == NO_ERROR && resetOriginal[1])
        err = DetourAttach(reinterpret_cast<PVOID*>(&resetOriginal[1]), ResetCompute);
    if (err == NO_ERROR)
        err = DetourTransactionCommit();
    else if (err != ERROR_NOT_FOUND)
        DetourTransactionAbort();
    listRecovery.store(true, std::memory_order_release); // own bridges and released lists still count
    if (err != NO_ERROR)
    {
        resetOriginal[0] = resetOriginal[1] = nullptr;
        LogLine("AMD neural: the command-list Reset hook could not be installed (error " + std::to_string(err) +
                "); a discarded neural list is still recovered when OptiScaler's bridge drops it or the game releases it");
        return;
    }
    LogLine(std::string("AMD neural: discarded neural lists are recovered ([DlssNr] AmdNeuralListRecovery): Reset watched on ") +
            (resetOriginal[1] ? "the DIRECT and COMPUTE list implementations" : "the list implementation"));
}
// (0.3.4, P22) The TerminateProcess watch (TerminateSelf), installed once with the backend in its own transaction
// after the submission hooks: if it cannot be installed, NR and the Exit hook work as before.
void InstallTerminateHook()
{
    terminateOriginal =
        reinterpret_cast<TerminateFn>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "TerminateProcess"));
    LONG err = terminateOriginal ? DetourTransactionBegin() : ERROR_NOT_FOUND;
    if (err == NO_ERROR)
        err = DetourUpdateThread(GetCurrentThread());
    if (err == NO_ERROR)
        err = DetourAttach(reinterpret_cast<PVOID*>(&terminateOriginal), TerminateSelf);
    if (err == NO_ERROR)
        err = DetourTransactionCommit();
    else if (err != ERROR_NOT_FOUND)
        DetourTransactionAbort();
    if (err != NO_ERROR)
    {
        terminateOriginal = nullptr;
        LogLine("AMD neural: TerminateProcess could not be watched (error " + std::to_string(err) +
                "); a game that ends itself that way is still noted as having no clean exit at the next start");
    }
}
} // namespace
bool DeviceIsAmd(ID3D12Device* device)
{
    return device && IsAmd(device);
}
void CancelActivity(ID3D12CommandList* list)
{
    if (list) {
        auto expected = list;
        activityCandidate.compare_exchange_strong(expected, nullptr);
        return;
    }
    activityCandidate.store(nullptr);
    std::lock_guard lock(activityMutex);
    activityPending.clear();
    activityCompletedAt = 0;
}
bool RecentlyActive()
{
    if (!Config::Instance()->DlssNrEnabled.value_or_default() || RuntimeStopped()) return false;
    const auto chosen = ChosenRuntime();
    if (chosen != NeuralRuntime::Unchosen && chosen != ActiveRuntime()) return false;
    std::lock_guard lock(activityMutex);
    PollActivityLocked();
    return activityCompletedAt && GetTickCount64() - activityCompletedAt <= 1500;
}
bool HasFiles()
{
    // Proxy names such as winmm.dll can load before Util::DllPath is finalized.
    // A negative result cached at that point disabled the AMD backend for the
    // rest of the process and left the menu at "waiting for the first upscaler
    // frame". Recheck until the package path becomes available.
    std::error_code ec;
    return std::filesystem::exists(Directory() / L"dlssnr_amd_pass1.dll", ec);
}
NeuralRuntime ChosenRuntime()
{
    const auto& v = Config::Instance()->DlssNrBackend.value_or_default();
    if (v == "lmxxf")
        return NeuralRuntime::Lmxxf;
    if (v == "daniel")
        return NeuralRuntime::Daniel;
    return NeuralRuntime::Unchosen;
}
bool LmxxfAssetsPresent()
{
    // lmxxf's package layout: DLSS5-AMD\native-game-tiled-assets beside the game holds the
    // weight files, the HLSL and HIP\ with the modules (in an architecture folder such as
    // HIP\gfx1201 in the current package). The check lives with the backend; cached once found.
    return Lmxxf::AssetsPresent(Directory());
}
bool LmxxfRuntimePresent()
{
    // LmxxfNrRuntime.dll, built from dlssnr\lmxxf\runtime and shipped beside OptiScaler.dll
    // (DLSS5-AMD\ is accepted too).
    return Lmxxf::RuntimePresent(Directory());
}
bool LmxxfReady()
{
    return LmxxfAssetsPresent() && LmxxfRuntimePresent();
}
bool AnyRuntimePresent()
{
    return HasFiles() || LmxxfReady();
}
// The HIP runtime's version, read the way both runtimes read it (hipInit, then
// hipRuntimeGetVersion on the driver's amdhip64_7.dll), and hipDriverGetVersion when exported.
// Logged once, for information; it no longer decides the runtime. The "HIP runtime 0" in
// danielblnc's "a store from the game's queue was NOT seen by the GPU wait ... (HIP runtime 0 ...)"
// is a field of its DLL that only its standalone host's environment dump fills in, so under
// OptiScaler it reads 0 on every machine. The Indiana Jones freeze blamed on it was the
// Vulkan-on-D3D12 bridge's ordering: the lmxxf runtime's first job uploaded its weights with a
// hipStreamSynchronize inside Evaluate, behind the bridge's queue Wait on the game's next
// vkQueueSubmit (now warmed up front, LMXXF_NR_FRAME_FLAG_VULKAN_BRIDGE), and danielblnc's check
// failed because its store sat behind the same Wait. -1: HIP is not usable here at all
// (amdhip64_7.dll not loadable, hipInit failing, or no version answer - the lmxxf runtime's own
// Create needs all three); only that sends the lmxxf choice elsewhere. 0 only when the runtime
// really answers 0.
namespace
{
struct HipProbe
{
    int runtime = -1, driver = -1;
    std::string failure; // why `runtime` is -1
    // (0.3.3.2) HIP's devices in its own order (index = HIP device): name, LUID (R0600: name at 0, LUID at 272)
    std::vector<std::pair<std::string, LUID>> devices;
};
const HipProbe& ProbeHip()
{
    static const HipProbe probe = [] {
        HipProbe r;
        HMODULE hip = LoadLibraryExW(L"amdhip64_7.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!hip)
        {
            r.failure = "amdhip64_7.dll could not be loaded (Win32 error " + std::to_string(GetLastError()) + ")";
            return r;
        }
        using InitFn = int (*)(unsigned);
        using VerFn = int (*)(int*);
        auto init = reinterpret_cast<InitFn>(GetProcAddress(hip, "hipInit"));
        auto ver = reinterpret_cast<VerFn>(GetProcAddress(hip, "hipRuntimeGetVersion"));
        auto drv = reinterpret_cast<VerFn>(GetProcAddress(hip, "hipDriverGetVersion"));
        const int initRc = init ? init(0) : -1;
        if (initRc != 0)
        {
            r.failure = init ? "hipInit failed (error " + std::to_string(initRc) + ")" : std::string("hipInit is not exported");
            return r;
        }
        int v = 0;
        const int verRc = ver ? ver(&v) : -1;
        if (verRc != 0)
        {
            r.failure = ver ? "hipRuntimeGetVersion failed (error " + std::to_string(verRc) + ")"
                            : std::string("hipRuntimeGetVersion is not exported");
            return r;
        }
        r.runtime = v;
        int d = 0;
        if (drv && drv(&d) == 0)
            r.driver = d;
        auto count = reinterpret_cast<int (*)(int*)>(GetProcAddress(hip, "hipGetDeviceCount"));
        auto props = reinterpret_cast<int (*)(void*, int)>(GetProcAddress(hip, "hipGetDevicePropertiesR0600"));
        int n = 0;
        if (count && props && count(&n) == 0)
            for (int i = 0; i < n; ++i)
            {
                alignas(16) std::array<unsigned char, 8192> p {};
                LUID l {};
                std::string name = "?";
                if (props(p.data(), i) == 0)
                {
                    name.assign(reinterpret_cast<const char*>(p.data()), strnlen(reinterpret_cast<const char*>(p.data()), 256));
                    std::memcpy(&l, p.data() + 272, sizeof l);
                }
                r.devices.emplace_back(name, l);
            }
        return r;
    }();
    return probe;
}
} // namespace
int HipRuntimeVersion()
{
    return ProbeHip().runtime;
}
int HipDriverVersion()
{
    return ProbeHip().driver;
}
bool LmxxfVkLaunchPending()
{
    // The menu asks every frame it is open; one file check per second is plenty. 0 = never checked.
    static std::atomic<ULONGLONG> checkedAt { 0 };
    static std::atomic<bool> pending { false };
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG last = checkedAt.load(std::memory_order_relaxed);
    if (last == 0 || now - last >= 1000)
    {
        std::error_code ec;
        pending.store(std::filesystem::exists(Directory() / kVkLaunchMarker, ec), std::memory_order_relaxed);
        checkedAt.store(now ? now : 1, std::memory_order_relaxed);
    }
    return pending.load(std::memory_order_relaxed);
}
// The runtime to build is lmxxf when it was chosen, or when it is the only one installed. The
// main archive ships LmxxfNrRuntime.dll + .pak and none of danielblnc's files, so "unchosen"
// (or "daniel" with its DLL missing) plus a complete lmxxf must mean lmxxf, not a fall-through
// to the NVIDIA path (the NTE report: RX 9070 XT, lmxxf fully installed, and the log ended in
// "nvngx.dll_dlssnr.dll is missing").
bool LmxxfWanted()
{
    const auto chosen = ChosenRuntime();
    if (!LmxxfReady())
    {
        // Chosen but not installed completely: said once, instead of running danielblnc's
        // runtime as if nothing had been asked (the report: switched to lmxxf in the menu,
        // restarted, nothing changed).
        static bool saidMissing = false;
        if (chosen == NeuralRuntime::Lmxxf && !saidMissing)
        {
            saidMissing = true;
            Message(LmxxfRuntimePresent()
                        ? "AMD neural: NrBackend=lmxxf chosen but its assets are missing (LmxxfNrRuntime.pak beside "
                          "LmxxfNrRuntime.dll, or DLSS5-AMD\\native-game-tiled-assets next to the game); running "
                          "danielblnc's runtime instead"
                        : "AMD neural: NrBackend=lmxxf chosen but LmxxfNrRuntime.dll is not beside OptiScaler.dll; "
                          "running danielblnc's runtime instead");
        }
        return false;
    }
    if (chosen == NeuralRuntime::Lmxxf)
        return true;
    if (HasFiles())
        return false; // danielblnc chosen, or unchosen with both installed (the menu asks)
    static bool said = false;
    if (!said)
    {
        said = true;
        Message(chosen == NeuralRuntime::Daniel
                    ? "AMD neural: NrBackend=daniel chosen but dlssnr_amd_pass1.dll is missing; running the lmxxf runtime instead"
                    : "AMD neural: lmxxf is the only runtime installed, running it ([DlssNr] NrBackend=lmxxf makes it explicit)");
    }
    return true;
}
NeuralRuntime ActiveRuntime()
{
    switch (activeRuntime.load())
    {
    case 1: return NeuralRuntime::Daniel;
    case 2: return NeuralRuntime::Lmxxf;
    default: return NeuralRuntime::Unchosen;
    }
}
// What one adapter can run (GpuSupportInfo): the table. Which adapter it describes is decided below
// (0.3.3.2: the one NR runs on).
namespace
{
struct GpuIds
{
    std::string name;
    VendorId::Value vendorId = VendorId::Invalid;
    uint32_t deviceId = 0, revisionId = 0;
};
GpuSupport DescribeGpu(const GpuIds& gpu)
{
    return [&gpu] {
        GpuSupport g;
        g.name = gpu.name;
        g.amd = gpu.vendorId == VendorId::AMD;
        if (!g.amd)
        {
            g.note = "not an AMD GPU: the AMD neural runtimes cannot run here";
            return g;
        }
        device_info::AdapterId id { gpu.vendorId, gpu.deviceId, gpu.revisionId };
        const auto card = device_info::GetCardInfo(id);
        if (!card.has_value() || !card->gfx_target)
        {
            g.note = "GPU not in the device table; the runtime itself will say whether it loads";
            return g;
        }
        g.known = true;
        g.note = "runtime initialization determines support";
        g.target = card->gfx_target;
        g.apu = card->is_apu;
        if (g.name.empty() && card->marketing_name) g.name = card->marketing_name;
        // (0.3.4) The compute units split gfx1103 (780M / Z1 Extreme 12, 760M 8, 740M / Z1 4) by the card's revision.
        if (const auto info = device_info::GetDeviceInfo(*card))
            g.computeUnits = info->num_cus;
        // The table (GpuSupportRules.h): RDNA 4 and RX 7000 both runtimes, Strix Halo lmxxf, the 12+ CU APUs lmxxf
        // experimental, the smaller APUs, RDNA 1 / 2 and older none.
        return g;
    }();
}
// (0.3.3.2, C7-B) The adapter the backend was built on, described once when it is built (NoteNrAdapter)
// and never freed, so a reference handed out stays valid; null until then.
std::atomic<const GpuSupport*> nrAdapterSupport { nullptr };
} // namespace
const GpuSupport& GpuSupportInfo()
{
    if (const GpuSupport* nr = nrAdapterSupport.load(std::memory_order_acquire))
        return *nr;
    static const GpuSupport primary = [] {
        const auto gpu = IdentifyGpu::getPrimaryGpu();
        return DescribeGpu({ gpu.name, gpu.vendorId, gpu.deviceId, gpu.revisionId });
    }();
    return primary;
}
// (0.3.4, RDNA 3 speed) The tier snap by default on gfx11 only (LmxxfTierPolicy.h); an explicit ini value wins. Asked
// per frame by BuildSettings: GpuSupportInfo is the NR adapter once the backend is built, the primary GPU before.
bool LmxxfTierSnapOn()
{
    const auto& key = Config::Instance()->AmdLmxxfTierSnap;
    return key.has_value() ? *key : Lmxxf::LmxxfTierSnapDefault(GpuSupportInfo().target);
}
// The GPU line names the adapter NR runs on, not the primary GPU (0.3.3.2): read from DXGI by the
// device's LUID, with adapter spoofing skipped as in IsAmd. Said when the two differ (a hybrid PC).
static void NoteNrAdapter(ID3D12Device* device)
{
    const LUID luid = device->GetAdapterLuid();
    const auto primary = IdentifyGpu::getPrimaryGpu();
    struct PhysicalAdapterScope
    {
        uint64_t id = SkipSpoof::AddEntry(SkipSpoofType::Thread);
        ~PhysicalAdapterScope() { SkipSpoof::RemoveEntry(id); }
    } physicalAdapterScope;
    IDXGIFactory4* f = nullptr;
    IDXGIAdapter1* a = nullptr;
    DXGI_ADAPTER_DESC1 desc {};
    bool found = false;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f))))
    {
        if (SUCCEEDED(f->EnumAdapterByLuid(luid, IID_PPV_ARGS(&a))))
        {
            found = SUCCEEDED(a->GetDesc1(&desc));
            a->Release();
        }
        f->Release();
    }
    if (!found)
        return; // the primary GPU's line stays, as before
    const bool isPrimary = IsEqualLUID(luid, primary.luid);
    // The primary GPU's name when it is that adapter (IdentifyGpu's may be the marketing name), else DXGI's.
    const std::string name = isPrimary && !primary.name.empty() ? primary.name : wstring_to_string(desc.Description);
    nrAdapterSupport.store(new GpuSupport(DescribeGpu({ name, static_cast<VendorId::Value>(desc.VendorId), desc.DeviceId, desc.Revision })),
                           std::memory_order_release);
    if (!isPrimary)
        Message(("AMD neural: NR runs on " + name + ", not on the primary GPU " + primary.name +
                 " - the GPU line and the Neural tab describe the adapter NR runs on")
                    .c_str());
}
bool DanielWeightsPresent()
{
    std::error_code ec;
    return std::filesystem::exists(Directory() / L"dlssnr_on_amd_weights.bin", ec);
}
bool RuntimeChoiceNeeded()
{
    // Only when there is a choice to make. With one runtime installed it simply runs.
    return ChosenRuntime() == NeuralRuntime::Unchosen && HasFiles() && LmxxfAssetsPresent();
}
// The runtime choice, applied. lmxxf chosen and its two pieces installed: Run() builds the
// lmxxf backend. Chosen with a piece missing: danielblnc's runtime runs when it is installed
// and the status says which piece is missing, once; with nothing else installed the pass
// stands down rather than pretending.
static bool RuntimeGate()
{
    if (ChosenRuntime() != NeuralRuntime::Lmxxf || LmxxfReady())
        return true;
    static bool said = false;
    const char* missing = LmxxfAssetsPresent() ? "LmxxfNrRuntime.dll (beside OptiScaler.dll)"
                                               : "LmxxfNrRuntime.pak beside LmxxfNrRuntime.dll (or DLSS5-AMD\\native-game-tiled-assets)";
    if (HasFiles())
    {
        if (!said)
        {
            said = true;
            Message((std::string("AMD neural: NrBackend=lmxxf chosen but ") + missing +
                     " is missing; running danielblnc's runtime instead").c_str());
        }
        return true;
    }
    if (!said)
    {
        said = true;
        Message((std::string("AMD neural: NrBackend=lmxxf chosen but ") + missing +
                 " is missing, and no danielblnc runtime is installed").c_str());
    }
    return false;
}
// Pass 1 of the folder, identified once per process (name and layout together, one read), or null while it is missing.
// (0.3.4, danielblnc support: the layout joins the name, for the capability answers below.)
struct RuntimeProbe
{
    const char* name = nullptr;
    const AmdPreSr::AmdLayout* layout = nullptr;
};
static const RuntimeProbe* ProbeRuntimeOnce()
{
    static std::once_flag once;
    static RuntimeProbe probe;
    std::error_code ec;
    const auto path = Directory() / L"dlssnr_amd_pass1.dll";
    if (!std::filesystem::exists(path, ec))
        return nullptr;
    std::call_once(once, [&] { probe.name = AmdPreSr::IdentifyRuntimeName(path, &probe.layout); });
    return &probe;
}
const char* RuntimeName()
{
    // The loaded name first, and in the normal case that is the whole function: the backend
    // identified the pass DLL when it loaded it, so asking the filesystem again answers a
    // question already answered.
    //
    // It used to re-stat the file and, on any metadata mismatch, re-read and re-hash all
    // 7.3 MB of it - from the menu, every frame the Neural section was open, on the game's
    // render thread. That is the crash report from Neverness to Everness: NR running fine,
    // the game dying the moment that section was expanded, with an exception raised inside
    // the game's own crash SDK. A render thread that disappears for seconds is not a hang
    // this code can be forgiven for; it is one it caused.
    if (const char* loaded = AmdPreSr::LoadedRuntimeName())
        return loaded;

    // Before the backend has loaded anything there is nothing to report but the file, so
    // identify it ONCE and keep whatever came back - including "unknown". A retry loop is
    // what turned a cheap lookup into a per-frame 7.3 MB read in the first place.
    const RuntimeProbe* probe = ProbeRuntimeOnce();
    return probe ? probe->name : nullptr;
}
// (0.3.4, danielblnc support) The layout behind RuntimeName: -1 no pass 1 here, 0 an unknown build, 1 known (layout set).
static int RuntimeLayoutOf(const AmdPreSr::AmdLayout*& layout)
{
    layout = AmdPreSr::LoadedRuntimeLayout();
    if (layout)
        return 1;
    const RuntimeProbe* probe = ProbeRuntimeOnce();
    if (!probe)
        return -1;
    layout = probe->layout;
    return layout ? 1 : 0;
}
int DanielKnobsMapped()
{
    const AmdPreSr::AmdLayout* layout = nullptr;
    const int known = RuntimeLayoutOf(layout);
    return known <= 0 ? known : (AmdPreSr::KnobsUnmapped(*layout) ? 0 : 1);
}
int DanielFastModeSupported()
{
    const AmdPreSr::AmdLayout* layout = nullptr;
    const int known = RuntimeLayoutOf(layout);
    return known <= 0 ? known : (AmdPreSr::QualityMapped(*layout) ? 1 : 0);
}
int DanielFastModeRuntimeOwn() { return AmdPreSr::RuntimeOwnQuality(); }
// Every backend setting, read from the config. Shared by the pre-SR bridge (Before) and
// final image mode, so the two never disagree about what a slider means.
// ILION dev.51: the dynamic NR controller's last state, for the menu (AmdBridge.h, DynamicNrState)
static std::mutex dynamicReadoutLock;
static DynamicNrReadout dynamicReadout;
static unsigned long long dynamicReadoutTick = 0;

DynamicNrReadout DynamicNrState()
{
    std::scoped_lock lock(dynamicReadoutLock);
    DynamicNrReadout r = dynamicReadout;
    r.running = dynamicReadoutTick != 0 && GetTickCount64() - dynamicReadoutTick < 1000;
    return r;
}

static AmdPreSr::Settings BuildSettings(float sessionScale)
{
    const auto& cfg = *Config::Instance();
    AmdPreSr::Settings s {};
    s.look.enabled = false; s.rtgi.enabled = false;

    s.modelScale = sessionScale;
    { const int st = cfg.AmdNrSizeStep.value_or_default(); s.sizeStep = st <= 0 ? 0u : UINT(std::clamp(st, 16, 256)); }
    s.stability = std::clamp(cfg.AmdTemporalStability.value_or_default(), 0.f, 1.f);
    s.stabilityMode = static_cast<UINT>(std::clamp(cfg.AmdStabilityMode.value_or_default(), 0, 1));
    s.stabilityThreshold = std::clamp(cfg.AmdStabilityThreshold.value_or_default(), 0.005f, 0.5f);
    { const float r = cfg.AmdStabilityStaticRelax.value_or_default(); s.stabilityStaticRelax = std::isfinite(r) ? std::clamp(r, 0.f, 1.f) : 0.f; }
    s.stabilityStaticDebug = cfg.AmdStabilityStaticDebug.value_or_default();
    s.detail = std::clamp(cfg.AmdDetailStrength.value_or_default(), 0.f, 1.f);
    s.colour = std::clamp(cfg.AmdColourStrength.value_or_default(), 0.f, 1.f);
    {
        // Colour composition (NrCompose.h). Classic (0, and any value but 1) reads none of the
        // fields below, so its picture cannot move. RenoDX (1) has Detail / Colour keys of its
        // own, so a 2 set there never lands in Classic's 0..1 range, and reuses the NVIDIA path's
        // guard and skin keys with that path's sanitising (DlssNr_Dx12.cpp: a non-finite value
        // is the neutral one, skin colour is 0 while skin tone changes are not allowed).
        const auto bounded = [](float v, float lo, float hi, float fallback) {
            return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
        };
        s.composition = cfg.AmdComposition.value_or_default() == 1 ? 1u : 0u;
        s.composeDetail = bounded(cfg.AmdComposeDetail.value_or_default(), 0.f, 2.f, 1.f);
        s.composeColour = bounded(cfg.AmdComposeColour.value_or_default(), 0.f, 4.f, 1.f);
        s.maxRatio = bounded(cfg.DlssNrMaxRatio.value_or_default(), 1.f, 8.f, 2.f);
        s.skinProtection = cfg.DlssNrSkinProtection.value_or_default();
        s.skinDetail = bounded(cfg.DlssNrSkinDetail.value_or_default(), 0.f, 1.f, 1.f);
        s.skinColour = cfg.DlssNrSkinToneEnabled.value_or_default() ? bounded(cfg.DlssNrSkinColour.value_or_default(), 0.f, 1.f, 1.f)
                                                                    : 0.f;
        s.envDetail = bounded(cfg.DlssNrEnvironmentDetail.value_or_default(), 0.f, 1.f, 1.f);
        s.envColour = bounded(cfg.DlssNrEnvironmentColour.value_or_default(), 0.f, 1.f, 1.f);
    }
    s.sharpness = 0;
    {
        // >1 enables interleave at that average cadence (fractional allowed,
        // e.g. 1.4); at or below 1 there is nothing to skip, so treat as off.
        const float il = cfg.AmdInterleave.value_or_default();
        // WHOLE NUMBERS ONLY. A fractional cadence was supported for a while and 1.5
        // was even the menu's "gentler" option, but it is the one value that reliably
        // looked wrong. The accumulator in AmdPreSr turns 1.5 into M F M M F M M F - a
        // period of THREE with unevenly spaced skips, which at 60 fps beats at 20 Hz,
        // close to where the eye is most sensitive to flicker, while 2.0 gives an even
        // M F M F at 30 Hz. No fractional value can produce an evenly spaced pattern,
        // which is the whole reason it was visible, so there is nothing here to tune.
        // A hand-edited fractional value in the ini is rounded to the nearest whole
        // cadence rather than being allowed to behave badly.
        s.interleave = il > 1.f ? std::round(std::clamp(il, 2.f, 4.f)) : 0.f;
        s.interleaveFill = static_cast<UINT>(std::clamp(cfg.AmdInterleaveFill.value_or_default(), 0, 3));
        s.interleaveSharp = cfg.AmdInterleaveSharp.value_or_default();
        s.interleaveDebug = static_cast<UINT>(std::clamp(cfg.AmdInterleaveDebug.value_or_default(), 0, 5));
        // 1..5: 4 is Residual temporal, 5 is Guided fill. This clamped to 3 for a while after
        // preset 5 was added, which silently turned the new default back into Held frame -
        // the first "Guided fill" test was Held frame with a different label.
        {
            // Edit carry (7) is gone; its fill is a candidate inside Self-tuning (8), so a
            // stored 7 runs as 8 rather than falling into a preset it never meant.
            // 10 is Edit accumulation, on both runtimes. 9 and 11 are the temporal pass's own
            // numbers for lmxxf's carry and Edit accumulation's lmxxf form - the lmxxf host picks
            // between them from this value - so a stored 9 or 11 runs as 10, never as a preset
            // that would be handed danielblnc's inputs with lmxxf's meaning.
            int preset = std::clamp(cfg.AmdInterleavePreset.value_or_default(), 1, 11);
            if (preset == 7) preset = 8;
            if (preset == 9 || preset == 11) preset = 10;
            s.interleavePreset = static_cast<UINT>(preset);
        }
        // "GUIDED FILL" IS HELD FRAME, BY THE USER'S DECISION. The build he kept as the good
        // one (`53d42bc0`) showed "Guided fill" and ran Held frame, because this very line
        // clamped to 3 then. Fixing the clamp put the real history-guided filter on screen and
        // he preferred what he had. So 5 maps to 3 here on purpose; the guided-filter branch
        // in TemporalStability.h stays compiled and unreachable until someone asks for it.
        if (s.interleavePreset == 5)
            s.interleavePreset = 3;
        s.interleaveFreshHistory = cfg.AmdInterleaveFreshHistory.value_or_default();
        s.interleaveModelHistory = cfg.AmdInterleaveModelHistory.value_or_default();
        s.lmxxfHistory = cfg.AmdLmxxfHistory.value_or_default();
        s.lmxxfFullNetwork = cfg.LmxxfFullNetwork.value_or_default();
        s.lmxxfEditDetail = std::clamp(cfg.AmdLmxxfEditDetail.value_or_default(), 0.f, 2.f);
        s.lmxxfEditSaturation = std::clamp(cfg.AmdLmxxfEditSaturation.value_or_default(), 0.f, 2.f);
        s.lmxxfAutoExposure = cfg.AmdLmxxfAutoExposure.value_or_default();
        s.lmxxfCpuWait = State::Instance().api == API::Vulkan; // running over the Vulkan-on-D3D12 bridge
        s.vulkanBridge = s.lmxxfCpuWait; // the same fact, read by both hosts (AmdPreSr.h)
        s.d3d11Bridge = State::Instance().api == API::DX11; // running over the D3D11-on-D3D12 bridge
        s.lmxxfEdgeGuard = std::clamp(cfg.AmdLmxxfEdgeGuard.value_or_default(), 0.f, 1.f);
        s.lmxxfOutputSmooth = std::clamp(cfg.AmdLmxxfOutputSmooth.value_or_default(), 0.f, 1.f);
        // LOCKED OFF in this build by the user's decision (the code stays): in normal play it
        // kept the model on nearly every frame, so the interleave gain vanished. The ini key
        // is read and ignored until this line changes.
        s.interleaveAdaptive = false && cfg.AmdInterleaveAdaptive.value_or_default();
        s.jitterSign = std::clamp(cfg.AmdJitterSign.value_or_default(), -1, 1);
        s.interleaveAdaptiveSensitivity = static_cast<UINT>(std::clamp(cfg.AmdInterleaveAdaptiveSensitivity.value_or_default(), 0, 2));
        s.interleavePacing = std::clamp(cfg.AmdInterleavePacing.value_or_default(), -1.f, 1.f);
        s.proxy = cfg.AmdHighlightProxy.value_or_default() != 0;
        s.proxyKnee = std::clamp(cfg.AmdHighlightProxyKnee.value_or_default(), 0.25f, 8.f);
        s.proxyRange = std::clamp(cfg.AmdHighlightProxyRange.value_or_default(), 0.25f, 8.f);
        s.interleaveGhostBound = std::clamp(cfg.AmdInterleaveGhostBound.value_or_default(), 0.f, 1.f);
        s.networkOutput = cfg.AmdNetworkOutput.value_or_default();
        s.residualIntensity = std::clamp(cfg.AmdResidualIntensity.value_or_default(), 0.f, 2.f);
        s.residualLimit = std::clamp(cfg.AmdResidualLimit.value_or_default(), 0.f, 2.f);
        s.residualFade = std::clamp(cfg.AmdResidualFade.value_or_default(), 0.f, 0.25f);
        s.residualTemporal = cfg.AmdResidualTemporal.value_or_default();
        s.editShaper = cfg.AmdEditShaper.value_or_default();
        // Network output no longer forces interleave off, and removing that is a bug fix.
        //
        // The forcing existed so the comparison could not be run wrong while the option was
        // a checkbox. Then the checkbox was removed - the measurement had settled the
        // question - and the forcing was left behind, still reading a key that is now only
        // reachable from the ini. Anyone who had switched it on once while testing had the
        // value saved, and from then on Model interleave was disabled silently, by a
        // setting with no control left to turn it off.
        //
        // A rule that exists to protect an experiment has to be deleted with the experiment.
        // Left alone it stops being a safeguard and becomes a trap, and the fact that it is
        // invisible is what makes it one.
    }
    s.passes = cfg.DlssNrPasses.value_or_default();
    s.everyFrame = cfg.AmdEveryFrame.value_or_default();
    s.slots = std::clamp(cfg.AmdSlots.value_or_default(), 1, 5);
    s.spinDraw = cfg.AmdSpinDraw.value_or_default();
    s.graphicsWait = cfg.AmdGraphicsWaitExperimental.value_or_default();
    // The pinned AMD binary explicitly disables the broad lighting/colour
    // channels. Its embedded UI warns that nonzero tone mostly darkens frames.
    s.encoding=std::clamp(cfg.AmdEncoding.value_or_default(),0,3);
    s.toneChannels=cfg.AmdNeuralLightingStrength.value_or_default()>0;
    // Upper bound raised from 1 to 2, to match structure and skin.
    //
    // This is LocalToneStrength. The RenoDX add-on, which drives the same parameter
    // through the NVIDIA path, documents it as: "The supplied binaries default to 1 and
    // consume this value without range validation." So the network takes values above 1
    // and amplifies - and our clamp was throwing that range away before the runtime ever
    // saw it. The two neighbouring strengths were never clamped this way, which is the
    // giveaway that the 1 was a guess rather than a limit.
    s.tone=s.toneChannels ? std::clamp(cfg.AmdNeuralLightingStrength.value_or_default(),0.f,2.f) : 0.f;
    s.structure = cfg.DlssNrLocalStructure.value_or_default();
    s.skin = cfg.DlssNrSkinStructure.value_or_default();
    if (s.skin < 0)
        s.skin = s.structure;
    {
        // AMDNR 0.3.4 (plan section 3): carried into Settings only; each field's default is the 0.3.3.2 behaviour.
        // A value outside its range means the default, so a hand-edited typo never changes behaviour: shaper
        // limit -> 0 (literal, 0.3.3.2), scope -> 0 (both sides), a knob -> "auto" (-1: the host writes nothing,
        // so a typo never becomes a write into the runtime).
        const auto knob = [](int v, int hi) { return v >= 0 && v <= hi ? v : -1; };
        const int shaperLimit = cfg.AmdEditShaperLimit.value_or_default();
        s.editShaperLimit = shaperLimit >= 0 && shaperLimit <= 2 ? shaperLimit : 0;
        s.editShaperBelowOnly = cfg.AmdEditShaperScope.value_or_default() == 1;
        s.editShaperCarryCap = cfg.AmdEditShaperCarryCap.value_or_default();
        s.danielHighlightGuard = cfg.AmdDanielHighlightGuard.value_or_default();
        s.autoMask = cfg.DlssNrAutoMask.value_or_default();
        s.runtimeStyle = knob(cfg.AmdRuntimeStyle.value_or_default(), 2);
        s.toneCurve = knob(cfg.AmdToneCurve.value_or_default(), 1);
        s.useGameExposure = knob(cfg.AmdUseGameExposure.value_or_default(), 1);
        const float lift = cfg.AmdToneLift.value_or_default();
        s.toneLift = std::isfinite(lift) && lift >= 0.f ? std::min(lift, 0.25f) : -1.f;
        // (0.3.4, danielblnc support) danielblnc's quality mode. Unset (every ini before 0.3.4, and "auto") = -1: the host writes
        // nothing and the runtime keeps its own mode (Fast unless its dlssnr_on_amd.ini says otherwise), as 0.3.3.2.
        // true = Fast, an explicit false = Reference: the runtime's default is Fast, so a false that wrote nothing would
        // leave Fast running under a key that says it is off. Written only where the layout maps the byte.
        const auto& fastMode = cfg.AmdDanielFastMode;
        s.danielQuality = fastMode.has_value() ? (*fastMode ? 1 : 0) : -1;
        // (0.3.4, RDNA 3 speed) Unset = on for gfx11, off elsewhere; an explicit true / false in the ini wins.
        s.lmxxfTierSnap = LmxxfTierSnapOn();
    }
    return s;
}

// (0.3.4, APU defaults) Once the adapter NR runs on is known (the backend is being built): on an lmxxf APU
// (GpuSupport::lmxxfExperimental: gfx1103 / gfx1150 with 12+ compute units) the model runs every 4th frame while the
// ini leaves [DlssNr] AmdInterleave unset (auto, as the shipped ini has it). A volatile value: Save Settings never
// writes it, and an item the player picks in the menu replaces it and is saved (Off as 1 on such an APU:
// KeepApuInterleaveOff below); a value in the ini wins. Both runtimes (a GPU class is not a runtime setting). The 360p
// network size is AmdLmxxfTierCap's auto (lmxxf only, LmxxfTierPolicy.h); Neural passes keep their default of 1. Once
// per process (the latch: after set_volatile_value the key holds a value).
static void ApplyGpuClassDefaults()
{
    static bool applied = false;
    if (applied)
        return;
    applied = true;
    auto& interleave = Config::Instance()->AmdInterleave;
    if (!AmdGpuRules::UseApuInterleaveDefault(interleave.has_value(), GpuSupportInfo().lmxxfExperimental))
        return;
    interleave.set_volatile_value(AmdGpuRules::kApuInterleave);
    Message("AMD neural: APU default - the model runs every 4th frame ([DlssNr] AmdInterleave is unset: 4 for this "
            "session, not saved; a Model interleave item picked in the menu, or a value in the ini, replaces it)");
}
// (0.3.4, r1 review 1) On an lmxxf APU an explicit Model interleave Off (the menu writes 0, or a hand-typed 0) is kept
// as 1, which also means off: 0 equals the key's default, so Save Settings wrote it as "auto" and the APU default
// above came back at the next game start. Per frame, before BuildSettings, so a menu pick is caught on the next
// frame; the APU default's 4 (volatile) is never touched. Desktop GPUs: nothing (0.3.3.2's 0 / auto). Run() holds
// frameMutex.
static void KeepApuInterleaveOff()
{
    auto& interleave = Config::Instance()->AmdInterleave;
    if (!AmdGpuRules::KeepApuInterleaveOff(GpuSupportInfo().lmxxfExperimental, interleave.has_value(),
                                           interleave.value_or_default()))
        return;
    interleave = AmdGpuRules::kApuInterleaveOff;
    static bool said = false;
    if (!said)
    {
        said = true;
        LogLine("AMD neural: Model interleave Off on this APU is kept as [DlssNr] AmdInterleave=1 (also off): 0 is the "
                "default, which Save Settings writes as auto, and the APU default (every 4th frame) would come back");
    }
}

// (0.3.3.2, C7-A, [DlssNr] AmdDeviceGate) ONE DEVICE. The backend is built once, on the first frame's
// device, and everything it owns - pipelines, textures, the runtime's session - belongs to that device.
// A frame recorded on another one (a second adapter, or a game that re-created its device) used to reach
// it anyway: lmxxf's runtime then refused the list ("command list device mismatch") and stopped for the
// session, and danielblnc's recorded its own device's resources into the other device's list. Such a
// frame now goes to the upscaler untouched. The pointers first (every normal frame ends there); another
// pointer is asked once (SameD3D12Device: the adapter, then COM identity through Streamline's and
// ReShade's wrappers, so a wrapper handing out another pointer to the same device keeps NR), the answer
// kept for that pointer and its adapter. Run() holds frameMutex.
static bool OnBackendDevice(ID3D12Device* device, ID3D12Device* built)
{
    if (device == built)
        return true;
    static ID3D12Device* askedDevice = nullptr;
    static LUID askedLuid {};
    static bool askedSame = false;
    const LUID luid = device->GetAdapterLuid();
    if (device != askedDevice || !IsEqualLUID(luid, askedLuid))
    {
        askedDevice = device;
        askedLuid = luid;
        askedSame = DlssNr::SameD3D12Device(device, built);
    }
    return askedSame;
}
// Said once per adapter in the status, then every 600 such frames in amd_bridge.log. Run() holds frameMutex.
static void NoteForeignDevice(ID3D12Device* device, ID3D12Device* built)
{
    static LUID saidLuid {};
    static bool said = false;
    static UINT64 frames = 0;
    ++frames;
    const LUID luid = device->GetAdapterLuid();
    const bool newAdapter = !said || !IsEqualLUID(luid, saidLuid);
    if (!newAdapter && frames % 600 != 0)
        return;
    said = true;
    saidLuid = luid;
    const std::string line = "AMD neural: a frame was recorded on another D3D12 device than the one NR runs on (" +
                             std::string(IsEqualLUID(luid, built->GetAdapterLuid()) ? "the same adapter" : "another adapter") +
                             "); it goes to the upscaler untouched (" + std::to_string(frames) +
                             " such frames; [DlssNr] AmdDeviceGate)";
    if (newAdapter)
        Message(line.c_str());
    else
        LogLine(line);
}

// (0.3.4, P1 Marvel's Midnight Suns, [DlssNr] AmdStreamlineDeviceFix) THE DEVICE danielblnc RUNS ON, decided once, when
// the first backend is about to be built (ComIdentity.h, ChooseNrDevice, has the why). The frame's device stays the
// one the Execute probe, the Reset hook and the device gate use (the game's lists are made through it); only
// danielblnc's backend is built on the device behind a proven proxy, which is the device of the queue it is built
// with. lmxxf keeps the frame's device and binds to the queue its lists arrive on (a proxy device and a proxy queue,
// consistent), so it only gets the log line. Two devices with no proof: danielblnc is not started (it would fault on
// its first submission, as it did in Midnight Suns); the game runs untouched and the status says why. The key off =
// 0.3.3.2 (no probe, every runtime on the frame's device). Returns false for that refusal, on every frame of the
// session; `danielDevice` = the device to build danielblnc on, null = the frame's. Run() holds frameMutex.
static bool PairNrDevice(ID3D12Device* device, ID3D12CommandQueue* q, bool lmxxf, ID3D12Device*& danielDevice)
{
    static bool decided = false;
    static DlssNr::NrDevicePairing pairing = DlssNr::NrDevicePairing::SameDevice;
    static ID3D12Device* behind = nullptr; // ProxyBy*: one reference, kept for the process like backendDevice's
    danielDevice = nullptr;
    if (!Config::Instance()->AmdStreamlineDeviceFix.value_or_default())
        return true;
    using DlssNr::NrDevicePairing;
    if (!decided)
    {
        decided = true;
        const DlssNr::NrDeviceChoice c = DlssNr::ChooseNrDevice(device, q);
        pairing = c.pairing;
        behind = c.device;
        const bool guid = pairing == NrDevicePairing::ProxyByGuid;
        switch (pairing)
        {
        case NrDevicePairing::ProxyByGuid:
        case NrDevicePairing::ProxyByFence:
            LogLine(std::string("AMD neural: the game's D3D12 device is ") +
                    (guid ? "a Streamline proxy (GUID proof)"
                          : "a proxy (fence proof: its fences belong to the device of the game's queue; it does not "
                            "answer Streamline's GUID)") +
                    (lmxxf ? "; lmxxf runs on it and on the queue the game submits to, as before"
                           : "; danielblnc runs on the device behind it, the device of the game's queue") +
                    " ([DlssNr] AmdStreamlineDeviceFix)");
            break;
        case NrDevicePairing::NotProven:
            if (lmxxf)
                LogLine("AMD neural: the game's D3D12 device and the device of its command queue differ and neither was "
                        "shown to wrap the other; lmxxf runs as before (its runtime checks the devices itself)");
            break;
        case NrDevicePairing::QueueWrapsFrame:
            LogLine("AMD neural: the game's command queue belongs to a wrapper of the game's D3D12 device; NR runs on the "
                    "game's device as before");
            break;
        case NrDevicePairing::QueueDeviceUnknown:
            LogLine("AMD neural: the game's command queue did not name its D3D12 device; NR runs on the game's device as "
                    "before");
            break;
        case NrDevicePairing::OtherAdapter:
            LogLine("AMD neural: the game's command queue is on another adapter than its frames ([DlssNr] AmdDeviceGate "
                    "is off); NR runs on the frames' device as before");
            break;
        case NrDevicePairing::SameDevice:
            break;
        }
    }
    if (lmxxf)
        return true;
    if (pairing == NrDevicePairing::NotProven)
    {
        static const char* const kRefusal =
            "AMD neural: danielblnc's runtime is not started: the game's D3D12 device and the device of its command "
            "queue differ and neither was shown to wrap the other (an unknown wrapper), which would crash its first "
            "submission. No NR in this session; the game runs untouched ([DlssNr] AmdStreamlineDeviceFix)";
        {
            std::lock_guard l(refusalMutex);
            nrDeviceRefusal = kRefusal;
        }
        Message(kRefusal); // written to amd_bridge.log once (SettleLogGate); the status every frame
        return false;
    }
    danielDevice = behind;
    return true;
}

// (0.3.3.2, C6-B, [DlssNr] AmdOneStreamPerFrame) ONE NEURAL STREAM PER PRESENTED FRAME. The backend keeps
// one history and counts its calls as frames (the interleave cadence, the temporal pass, the runtime's
// own history). A title with more than one NR entry per presented frame - split view, a scope or
// picture-in-picture, a second upscaler context - fed it two streams: at two sizes the settle below
// restarted on every call and NR never ran; at one size the model ran twice a frame on mixed history.
// Measured, not guessed: NR entries against presented frames (`epoch`, see AmdBridge.h) over windows of
// 120 presents, per placement (pre-SR / post-RR). Only at 1.75 entries per present or more does it
// engage (and it lets go at 1.25): a game with one entry per frame, whose Evaluate and Present merely
// drift against each other on different threads, averages 1.0 and is never touched. Engaged, NR runs on
// one parameter block - the largest view, sticky, handed over only when that block stops calling for 30
// presents or a larger one arrives first in a frame - and every other entry passes through untouched
// before the settle, so it no longer restarts the NR stream. When the chosen block itself carries the
// several entries (one block, several views), it runs once per present. Epoch 0 or an epoch that does
// not advance never engages it. Run() holds frameMutex.
struct StreamWatch
{
    unsigned long long lastEpoch = 0;           // the epoch of the previous call (0 = none yet)
    unsigned long long presents = 0, calls = 0, stickyCalls = 0; // this window
    bool multi = false;                         // measured: more than one entry per presented frame
    bool shared = false;                        // measured: the chosen block itself carries them
    NVSDK_NGX_Parameter* sticky = nullptr;      // the block NR runs on while `multi`
    unsigned long long stickyArea = 0, stickySeen = 0, ranEpoch = ~0ull;
    NVSDK_NGX_Parameter* windowBest = nullptr;  // this window's largest entry: the first `sticky` on engaging
    unsigned long long windowBestArea = 0;
    unsigned handovers = 0;
};
static bool OneStreamPerFrame(StreamWatch& w, bool post, NVSDK_NGX_Parameter* params, unsigned long long area,
                              unsigned long long epoch)
{
    if (epoch != 0)
    {
        if (w.lastEpoch != 0 && epoch > w.lastEpoch)
            w.presents += (std::min)(epoch - w.lastEpoch, 1000ull); // a load screen is not a measurement
        w.lastEpoch = epoch;
    }
    ++w.calls;
    if (params == w.sticky)
        ++w.stickyCalls;
    if (area > w.windowBestArea)
    {
        w.windowBestArea = area;
        w.windowBest = params;
    }
    if (w.presents >= 120)
    {
        const double ratio = double(w.calls) / double(w.presents);
        const bool multi = w.multi ? ratio > 1.25 : ratio >= 1.75;
        w.shared = multi && double(w.stickyCalls) / double(w.presents) >= 1.75;
        if (multi != w.multi)
        {
            w.multi = multi;
            // Engaging: the largest entry of the window it was measured over, so the NR stream does not
            // pass through a smaller one first (each change of stream restarts the settle below).
            w.sticky = multi ? w.windowBest : nullptr;
            w.stickyArea = w.windowBestArea;
            w.stickySeen = w.lastEpoch;
            char measured[64];
            std::snprintf(measured, sizeof measured, "%.1f", ratio);
            LogLine(std::string("AMD neural: ") + measured + " NR entries per presented frame" +
                    (post ? " after Ray Regeneration" : "") +
                    (multi ? " (split view, a scope or a second upscaler context): NR runs on the largest, the others "
                             "pass through untouched ([DlssNr] AmdOneStreamPerFrame)"
                           : " again: every entry runs NR"));
        }
        w.presents = w.calls = w.stickyCalls = 0;
        w.windowBest = nullptr;
        w.windowBestArea = 0;
    }
    if (!w.multi)
        return true;
    if (params == w.sticky)
    {
        w.stickyArea = area;
        w.stickySeen = epoch;
        if (w.shared && w.ranEpoch == epoch)
            return false;
        w.ranEpoch = epoch;
        return true;
    }
    const bool gone = !w.sticky || epoch < w.stickySeen || epoch - w.stickySeen > 30;
    if (gone || (area > w.stickyArea && w.ranEpoch != epoch))
    {
        w.sticky = params;
        w.stickyArea = area;
        w.stickySeen = epoch;
        w.stickyCalls = 0;
        w.ranEpoch = epoch;
        if (++w.handovers <= 8)
            LogLine("AMD neural: NR now runs on the entry at " + std::to_string(area) + " pixels" +
                    (gone ? " (the previous one stopped)" : " (larger than the previous one)") +
                    (w.handovers == 8 ? " (further hand-overs are not logged)" : ""));
        return true;
    }
    return false;
}

// SJ-LOG (AMDNR 0.3.4, FEEDBACK-1 P4): the dials an owner's A/B report has to be matched to, logged when they change,
// as one amd_bridge.log line "AMD settings: <ini key> a -> b | <ini key> a -> b". Log only: nothing here feeds a
// backend, and none of it is part of a backend's settingsChanged (that list resets history). A new value is logged once
// it has held for 30 bridge frames, so one slider drag is one line, and a value dragged back to where it was is none.
// Both runtimes: the bridge builds one Settings for either backend, and the values are the sanitised ones they get.
// The first NR frame only takes the baseline. Run() holds frameMutex; a steady frame costs a compare, no allocation.
namespace
{
enum class WatchKind : unsigned char
{
    Number,      // a float dial
    OnOff,       // a bool
    Knob,        // a runtime knob: -1 = auto (the host writes nothing), else the value
    ShaperLimit, // Settings::editShaperLimit: 0 literal, 1 F1, 2 F2 (EditShapeRules.h)
    ShaperScope, // Settings::editShaperBelowOnly: both sides of 100% or below 100% only
    Composition, // Settings::composition: 0 Classic, 1 RenoDX
};
struct WatchedSetting
{
    const char* key; // its [DlssNr] key in OptiScaler.ini
    WatchKind kind;
    float (*get)(const AmdPreSr::Settings&);
};
using NrSettings = AmdPreSr::Settings;
const WatchedSetting kWatchedSettings[] = {
    { "AmdResidualIntensity", WatchKind::Number, [](const NrSettings& s) { return s.residualIntensity; } },
    { "AmdResidualLimit", WatchKind::Number, [](const NrSettings& s) { return s.residualLimit; } },
    { "AmdResidualFade", WatchKind::Number, [](const NrSettings& s) { return s.residualFade; } },
    { "AmdComposition", WatchKind::Composition, [](const NrSettings& s) { return float(s.composition); } },
    { "AmdDetailStrength", WatchKind::Number, [](const NrSettings& s) { return s.detail; } },
    { "AmdColourStrength", WatchKind::Number, [](const NrSettings& s) { return s.colour; } },
    { "AmdComposeDetail", WatchKind::Number, [](const NrSettings& s) { return s.composeDetail; } },
    { "AmdComposeColour", WatchKind::Number, [](const NrSettings& s) { return s.composeColour; } },
    { "AmdSharpness", WatchKind::Number, [](const NrSettings& s) { return s.sharpness; } },
    { "AmdInterleaveModelHistory", WatchKind::OnOff, [](const NrSettings& s) { return s.interleaveModelHistory ? 1.f : 0.f; } },
    { "AmdLmxxfHistory", WatchKind::OnOff, [](const NrSettings& s) { return s.lmxxfHistory ? 1.f : 0.f; } },
    { "AmdEditShaper", WatchKind::OnOff, [](const NrSettings& s) { return s.editShaper ? 1.f : 0.f; } },
    { "AmdEditShaperLimit", WatchKind::ShaperLimit, [](const NrSettings& s) { return float(s.editShaperLimit); } },
    { "AmdEditShaperScope", WatchKind::ShaperScope, [](const NrSettings& s) { return s.editShaperBelowOnly ? 1.f : 0.f; } },
    { "AmdEditShaperCarryCap", WatchKind::OnOff, [](const NrSettings& s) { return s.editShaperCarryCap ? 1.f : 0.f; } },
    { "AmdDanielHighlightGuard", WatchKind::OnOff, [](const NrSettings& s) { return s.danielHighlightGuard ? 1.f : 0.f; } },
    { "AmdRuntimeStyle", WatchKind::Knob, [](const NrSettings& s) { return float(s.runtimeStyle); } },
    { "AmdToneCurve", WatchKind::Knob, [](const NrSettings& s) { return float(s.toneCurve); } },
    { "AmdToneLift", WatchKind::Knob, [](const NrSettings& s) { return s.toneLift; } },
    { "AmdUseGameExposure", WatchKind::Knob, [](const NrSettings& s) { return float(s.useGameExposure); } },
    { "AmdDanielFastMode", WatchKind::Knob, [](const NrSettings& s) { return float(s.danielQuality); } }, // -1 auto, 1 Fast, 0 Reference
    { "AutoMask", WatchKind::OnOff, [](const NrSettings& s) { return s.autoMask ? 1.f : 0.f; } },
};
constexpr size_t kWatchedCount = std::size(kWatchedSettings);
constexpr unsigned kSettingsStableFrames = 30;
using WatchedValues = std::array<float, kWatchedCount>;
struct SettingsLogWatch
{
    bool primed = false;         // the baseline was taken (first NR frame)
    WatchedValues logged {};     // the values the log last stated (the baseline at first)
    WatchedValues pending {};    // the newest values, and how many frames they have held
    unsigned stableFrames = 0;
};
bool SameValue(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }
bool SameValues(const WatchedValues& a, const WatchedValues& b)
{
    for (size_t i = 0; i < kWatchedCount; ++i)
        if (!SameValue(a[i], b[i]))
            return false;
    return true;
}
std::string WatchedText(WatchKind kind, float v)
{
    switch (kind)
    {
    case WatchKind::OnOff: return v != 0.f ? "on" : "off";
    case WatchKind::ShaperLimit: return v == 1.f ? "F1" : v == 2.f ? "F2" : "literal";
    case WatchKind::ShaperScope: return v != 0.f ? "below 100%" : "both";
    case WatchKind::Composition: return v == 1.f ? "RenoDX" : "Classic";
    case WatchKind::Knob:
        if (v < 0.f)
            return "auto";
        [[fallthrough]];
    case WatchKind::Number:
    default:
    {
        char text[32];
        std::snprintf(text, sizeof text, "%.4g", v);
        return text;
    }
    }
}
void NoteSettingsChange(SettingsLogWatch& w, const AmdPreSr::Settings& s)
{
    WatchedValues now;
    for (size_t i = 0; i < kWatchedCount; ++i)
        now[i] = kWatchedSettings[i].get(s);
    if (!w.primed)
    {
        w.primed = true;
        w.logged = w.pending = now;
        return;
    }
    if (!SameValues(now, w.pending))
    {
        w.pending = now;
        w.stableFrames = 0;
        return;
    }
    if (SameValues(w.pending, w.logged) || ++w.stableFrames < kSettingsStableFrames)
        return;
    std::string line = "AMD settings:";
    const char* separator = " ";
    for (size_t i = 0; i < kWatchedCount; ++i)
    {
        if (SameValue(w.logged[i], w.pending[i]))
            continue;
        line += separator;
        line += kWatchedSettings[i].key;
        line += ' ' + WatchedText(kWatchedSettings[i].kind, w.logged[i]) + " -> " +
                WatchedText(kWatchedSettings[i].kind, w.pending[i]);
        separator = " | ";
    }
    w.logged = w.pending;
    LogLine(line);
}
} // namespace

static bool Run(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params, ID3D12CommandQueue* q, bool post,
                unsigned long long epoch)
{
    // The process is exiting (C1-A): the colour stays the title's. true, not false: false would send the frame to the
    // NVIDIA path under a non-default ini.
    if (exiting.load(std::memory_order_acquire))
        return true;
    // A single backend consumes one SR stream even if the engine rotates worker threads.
    // Serialize shared settling/identity state; thread-local replacement ownership stays unchanged.
    std::lock_guard frameGuard(frameMutex);
    // Which backend this process runs is decided once, when the first one is built; the
    // menu says "restart the game" for a change made after that.
    bool lmxxf = backend.load() ? activeRuntime.load() == 2 : LmxxfWanted();
    if (!backend.load())
    {
        // Informational only (see HipRuntimeVersion): the version never picks the runtime.
        static bool saidHip = false;
        const int hipVer = HipRuntimeVersion();
        if (!saidHip)
        {
            saidHip = true;
            const auto& hip = ProbeHip();
            std::string line =
                hipVer >= 0 ? "AMD neural: HIP runtime " + std::to_string(hipVer) + ", driver " +
                                  (hip.driver >= 0 ? std::to_string(hip.driver) : std::string("unknown")) + " (amdhip64_7.dll)"
                            : "AMD neural: HIP not available - " + hip.failure + " (amdhip64_7.dll)";
            if (hipVer >= 0)
            {
                // (0.3.3.2) Which HIP index is the game's adapter (the RX 7900 XTX report: iGPU = HIP 0).
                LUID game {};
                bool gameKnown = false;
                ID3D12Device* dev = nullptr;
                if (cmd && SUCCEEDED(cmd->GetDevice(IID_PPV_ARGS(&dev))))
                {
                    game = dev->GetAdapterLuid();
                    gameKnown = true;
                    dev->Release();
                }
                std::string list;
                for (size_t i = 0; i < hip.devices.size(); ++i)
                    list += (i ? " | " : "") + std::to_string(i) + " " + hip.devices[i].first +
                            (gameKnown && std::memcmp(&hip.devices[i].second, &game, sizeof game) == 0 ? " (game)" : "");
                line += "; HIP devices: " + (list.empty() ? std::string("none reported") : list);
            }
            Message(line.c_str());
        }
        // Only a HIP that cannot be used at all sends the lmxxf choice elsewhere.
        if (lmxxf && hipVer == -1)
        {
            static bool saidNoHip = false;
            if (!saidNoHip)
            {
                saidNoHip = true;
                Message((std::string("AMD neural: HIP is not available on this system (amdhip64_7.dll / hipInit) - ") +
                         ProbeHip().failure +
                         (HasFiles() ? "; running danielblnc's runtime instead" : "; danielblnc's runtime is not installed, no neural pass"))
                            .c_str());
            }
            lmxxf = false;
        }
        // Self-healing on the Vulkan bridge: the lmxxf backend keeps lmxxf_vk_launch.pending only
        // while its first answer is outstanding (LmxxfBackend.cpp, kVkLaunchMarker): written before
        // PrepareFrame, removed at the first answer or when an attempt demonstrably failed without
        // hanging. Still there = the last lmxxf session on a Vulkan title stopped before its first
        // answer (it froze or the game was closed first), so this start does not run lmxxf.
        if (lmxxf && State::Instance().api == API::Vulkan)
        {
            std::error_code ec;
            if (std::filesystem::exists(Directory() / kVkLaunchMarker, ec))
            {
                static bool saidPending = false;
                if (!saidPending)
                {
                    saidPending = true;
                    Message(HasFiles()
                                ? "AMD neural: the previous lmxxf session on this Vulkan title stopped before its first answer "
                                  "(it froze or the game was closed first) - running danielblnc's runtime this time; delete "
                                  "lmxxf_vk_launch.pending to retry lmxxf"
                                : "AMD neural: the previous lmxxf session on this Vulkan title stopped before its first answer "
                                  "(it froze or the game was closed first) and danielblnc's runtime is not installed - no neural "
                                  "pass this time; delete lmxxf_vk_launch.pending to retry lmxxf");
                }
                lmxxf = false;
            }
        }
    }
    // Vulkan titles reach the pass through OptiScaler's Vulkan-on-D3D12 bridge, which records and
    // executes its D3D12 list inside the game's Evaluate, after a queue Wait on a fence that only
    // the game's next vkQueueSubmit signals, and at the next Evaluate waits on the CPU for that
    // frame's D3D12 work. The lmxxf runtime used to upload its weights lazily in the first job, with
    // a hipStreamSynchronize behind that Wait: a deadlock (Indiana Jones and the Great Circle:
    // lmxxf_backend.log ends at "first frame prepared"). On a Vulkan title (Settings::lmxxfCpuWait)
    // the runtime now warms the network before the first HIP fence wait, the host consumes each
    // answer with a bounded CPU wait (the queue never waits on HIP), and queue drains are skipped.
    // Said once, because the frame time pays for it. danielblnc's Vulkan line is said where its
    // backend is built, below, so it appears only when that runtime really runs.
    if (lmxxf && !backend.load() && State::Instance().api == API::Vulkan)
    {
        static bool saidVulkan = false;
        if (!saidVulkan)
        {
            saidVulkan = true;
            Message("AMD neural: Vulkan title - the lmxxf runtime loads its weights and runs the network once before the "
                    "first HIP fence wait (a one-time hitch), then each answer is taken with a bounded CPU wait (the frame waits, "
                    "as under danielblnc's inline mode; Model interleave 2 halves the cost)");
        }
    }
    if (!HasFiles() && !lmxxf)
        return false;
    ID3D12Device* device = nullptr;
    if (!cmd || !params || FAILED(cmd->GetDevice(IID_PPV_ARGS(&device))))
        return true;
    // (0.3.3.2, C7-A) Another device's frame: untouched, before anything of the NR stream is touched
    // (see OnBackendDevice).
    if (ID3D12Device* const built = backendDevice.load(std::memory_order_acquire);
        built && Config::Instance()->AmdDeviceGate.value_or_default() && !OnBackendDevice(device, built))
    {
        NoteForeignDevice(device, built);
        device->Release();
        return true;
    }
    thread_local LUID checkedAdapter {};
    thread_local bool checked = false, amd = false;
    const auto adapter = device->GetAdapterLuid();
    if (!checked || adapter.HighPart != checkedAdapter.HighPart || adapter.LowPart != checkedAdapter.LowPart)
    {
        amd = IsAmd(device);
        checkedAdapter = adapter;
        checked = true;
    }
    if (!amd)
    {
        device->Release();
        return false;
    }
    if (!q)
    {
        q = reinterpret_cast<ID3D12CommandQueue*>(State::Instance().currentCommandQueue);
        // (0.3.3.2, C7-A) The swapchain's queue is only the hint the backend is built with, and never
        // one on another adapter than the frame's (whose fences it could not signal or wait on).
        if (q && !backend.load() && Config::Instance()->AmdDeviceGate.value_or_default())
        {
            ID3D12Device* qd = nullptr;
            if (SUCCEEDED(q->GetDevice(IID_PPV_ARGS(&qd))))
            {
                const bool sameAdapter = IsEqualLUID(qd->GetAdapterLuid(), device->GetAdapterLuid());
                qd->Release();
                if (!sameAdapter)
                {
                    device->Release();
                    Message("AMD pre-SR: waiting for the game command queue (the swapchain's queue is on another adapter "
                            "than the upscaler's frames; [DlssNr] AmdDeviceGate)");
                    return true;
                }
            }
        }
    }
    if (!q)
    {
        device->Release();
        Message("AMD pre-SR: waiting for the game command queue");
        return true;
    }
    // (0.3.4, P1) The device danielblnc's backend is built on (the frame's, or the one behind a proven proxy), or its
    // refusal; decided once, before the hooks below are installed (see PairNrDevice).
    ID3D12Device* danielDevice = nullptr;
    if (!backend.load() && !PairNrDevice(device, q, lmxxf, danielDevice))
    {
        device->Release();
        return true;
    }
    std::lock_guard initGuard(initMutex);
    auto b = backend.load();
    if (!b)
    {
        executeOriginal = reinterpret_cast<ExecuteFn>((*reinterpret_cast<void***>(q))[10]);
        // FG can expose a proxy present queue. Hook the device's execution
        // implementation so actual render submissions are still observed.
        ID3D12CommandQueue* probe = nullptr;
        D3D12_COMMAND_QUEUE_DESC queueDesc {};
        if (SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&probe))))
        {
            executeOriginal = reinterpret_cast<ExecuteFn>((*reinterpret_cast<void***>(probe))[10]);
            probe->Release();
        }
        exitOriginal = reinterpret_cast<ExitFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlExitUserProcess"));
        LONG err = DetourTransactionBegin();
        if (err == NO_ERROR)
            err = DetourUpdateThread(GetCurrentThread());
        if (err == NO_ERROR)
            err = DetourAttach(reinterpret_cast<PVOID*>(&executeOriginal), Execute);
        if (err == NO_ERROR && exitOriginal)
            err = DetourAttach(reinterpret_cast<PVOID*>(&exitOriginal), Exit);
        if (err == NO_ERROR)
            err = DetourTransactionCommit();
        else
            DetourTransactionAbort();
        if (err != NO_ERROR)
        {
            device->Release();
            Message("AMD pre-SR: could not install submission notification");
            return true;
        }
        // (0.3.3.2) The discard evidence ([DlssNr] AmdNeuralListRecovery), decided once, here.
        if (Config::Instance()->AmdNeuralListRecovery.value_or_default())
            InstallResetHook(device);
        // ILION: no additional process-termination telemetry hook.
        {
            // One line in amd_bridge.log saying what this GPU can run, before the first
            // runtime error would say it in its own words. The adapter NR runs on (0.3.3.2, C7-B).
            NoteNrAdapter(device);
            const auto& g = GpuSupportInfo();
            // (0.3.4) The compute units, and "(experimental)" on the 12+ CU APUs.
            Message(("AMD neural: GPU " + g.name +
                     (g.target.empty() ? ""
                                       : " (" + g.target +
                                             (g.computeUnits > 0 ? ", " + std::to_string(g.computeUnits) + " CUs" : "") + ")") +
                     " - " + g.note)
                        .c_str());
            ApplyGpuClassDefaults();
        }
        {
            // danielblnc's standalone beside the game (0.3.3.2): reads a few DLLs of about 10 MB, so once and
            // on its own thread, never on this one (the menu-hash stall: IdentifyRuntime's comment).
            static std::once_flag standaloneOnce;
            std::call_once(standaloneOnce, [] {
                try
                {
                    std::thread(CheckForeignStandalone).detach();
                }
                catch (...)
                {
                }
            });
        }
        {
            // Which d3dcompiler_47 our own shaders go through (SystemCompiler.h), and whether the
            // game brought one of its own - the Where Winds Meet / Wuthering Waves report, where
            // the temporal pass failed to build on 0.3.0 and not on 0.1.0, is answered by this line.
            const auto& sc = DlssNr::SysCompiler::Get();
            std::string line = "AMD shaders: compiler " + (sc.path.empty() ? std::string("linked d3dcompiler_47 (System32 copy unavailable)")
                                                                        : wstring_to_string(sc.path));
            if (!sc.foreign.empty())
                line += "; the process also carries " + wstring_to_string(sc.foreign) + " (the game's own copy; our import used to bind to it)";
            Message(line.c_str());
        }
        if (lmxxf)
        {
            b = new Lmxxf::Backend(device, q, Directory());
            activeRuntime.store(2);
            Message("AMD neural: lmxxf runtime selected (LmxxfNrRuntime.dll + LmxxfNrRuntime.pak); "
                    "its edit lands one frame late, carried by the motion vectors");
        }
        else
        {
            // (0.3.4, P1) On the device behind the game's proxy device when PairNrDevice proved one (the device of
            // `q`), so the runtime's own lists and queue are one identity family; the frame's device otherwise.
            b = new AmdPreSr::Backend(danielDevice ? danielDevice : device, q, Directory());
            if (danielDevice)
                nrOnDeviceBehindProxy.store(true, std::memory_order_release); // before the backend is published
            activeRuntime.store(1);
            // danielblnc's counterpart of the lmxxf Vulkan line above, said once (the backend is
            // built once). danielblnc works on the bridge (Indiana Jones: every frame, no timeouts),
            // but its log reads like a driver fault: the inline flag check fails because the
            // runtime's store is queued behind the bridge's Wait on the game's next vkQueueSubmit,
            // which cannot come while the game is inside Evaluate (see HipRuntimeVersion above).
            // The runtime reads dlssnr_on_amd.ini and has an async mode ("ini changed Async=%d"), so
            // the line does not call the key ignored; it says why the key must stay as it is: async
            // passes pre-upscale colour through untouched. With the bridge's late copy Wait
            // ([DlssNr] AmdVkLateCopyWait) on, the "5 s" and "NOT seen" sentences are expected not to
            // apply, so that case gets its own line, as in the Neural tab (DlssNr_Menu.cpp). The test is
            // the bridge's own gate (IFeature_VkwDx12::ProcessVulkanTextures), read when the backend is
            // built. When the key is on by default, keep only that line.
            if (State::Instance().api == API::Vulkan)
            {
                const bool lateCopyWait = Config::Instance()->DlssNrEnabled.value_or_default() &&
                                          Config::Instance()->AmdVkLateCopyWait.value_or_default();
                if (lateCopyWait)
                    Message("AMD neural: Vulkan title - danielblnc's runtime runs NR inline, every frame. [DlssNr] "
                            "AmdVkLateCopyWait is on (experimental, not yet tested in a game): the first-frame pause and "
                            "the \"inline flag check ... NOT seen\" line in dlssnr_on_amd.log are expected to be gone. "
                            "Leave dlssnr_on_amd.ini as it is: AMDNR drives this runtime inline, and async mode would "
                            "pass the colour through untouched");
                else
                    Message("AMD neural: Vulkan title - danielblnc's runtime runs NR inline, every frame; the first NR frame "
                            "of a session pauses about 5 s. The \"inline flag check: a store from the game's queue was NOT "
                            "seen ... update the driver, or set Inline=0 in dlssnr_on_amd.ini\" line in dlssnr_on_amd.log "
                            "comes from the Vulkan bridge's queue order, not the driver, and its \"HIP runtime 0\" reads 0 on "
                            "every machine under OptiScaler. Leave dlssnr_on_amd.ini as it is: AMDNR drives this runtime "
                            "inline, and async mode would pass the colour through untouched");
            }
        }
        device->AddRef(); // (0.3.3.2) the device gate's reference, kept like the backend
        backendDevice.store(device, std::memory_order_release);
        backend.store(b);
    }
    device->Release();
    // The hook observes this list when the current frame is submitted and then
    // binds the actual queue before waking HIP. Engines that rotate command-list
    // objects may never submit the same object twice, so do not require a prior
    // observation here.
    ClearStatus(epoch);
    // The swapchain's present queue can change when FG is enabled. It is
    // only a bootstrap hint; Submitted identifies the queue executing our list.
    AmdPreSr::Frame f {};
    // Pre-SR takes the colour the upscaler is about to read. Post-RR takes the output Ray
    // Regeneration just wrote, at display resolution, and the backend writes its result back
    // into that same texture: there is no upscaler after it to hand a replacement to.
    f.writeBack = post;
    f.inputWriteBack = !post;
    f.colour = Resource(params, post ? NVSDK_NGX_Parameter_Output : NVSDK_NGX_Parameter_Color);
    f.motion = Resource(params, NVSDK_NGX_Parameter_MotionVectors);
    f.depth = Resource(params, NVSDK_NGX_Parameter_Depth);
    f.exposure = Resource(params, NVSDK_NGX_Parameter_ExposureTexture);
    params->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &f.preExposure);
    params->Get(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, &f.exposureScale);
    UINT renderW = 0, renderH = 0;
    params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &renderW);
    params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &renderH);
    if (post)
    {
        params->Get(NVSDK_NGX_Parameter_OutWidth, &f.width);
        params->Get(NVSDK_NGX_Parameter_OutHeight, &f.height);
    }
    else
    {
        f.width = renderW;
        f.height = renderH;
    }
    if (f.colour)
    {
        const auto extent = f.colour->GetDesc();
        if (!f.width) f.width = static_cast<UINT>(extent.Width);
        if (!f.height) f.height = extent.Height;
    }
    UINT x = 0, y = 0, flags = 0, reset = 0;
    params->Get(post ? NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X : NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, &x);
    params->Get(post ? NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y : NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, &y);
    if (x || y)
    {
        Message(post ? "AMD neural: nonzero output subrect origin unsupported"
                     : "AMD pre-SR: nonzero colour subrect origin unsupported");
        return true;
    }
    auto haveFlags = params->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &flags) == NVSDK_NGX_Result_Success;
    if (!haveFlags && !post && State::Instance().currentFeature) {
        flags = State::Instance().currentFeature->GetFeatureFlags();
        haveFlags = true;
    }
    // Which grid the vectors are on. Pre-SR only declares display-sized vectors (the backend
    // resamples those onto the render grid; render-sized ones need nothing). Post-RR always
    // declares them: under a display-sized colour they are either render-sized, when the
    // game says so (or, lacking flags, when the allocation is smaller than the output), or
    // display-sized, and either way the same resample brings them onto the model's grid.
    bool renderSizedMotion = haveFlags && (flags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes);
    if (post && !haveFlags && f.motion)
        renderSizedMotion = f.motion->GetDesc().Width < f.width;
    if (f.motion && (post || (haveFlags && !renderSizedMotion)))
    {
        if (post && renderSizedMotion)
        {
            f.motionWidth = renderW;
            f.motionHeight = renderH;
        }
        else
        {
            params->Get(NVSDK_NGX_Parameter_OutWidth, &f.motionWidth);
            params->Get(NVSDK_NGX_Parameter_OutHeight, &f.motionHeight);
        }
        if (!f.motionWidth) f.motionWidth = static_cast<UINT>(f.motion->GetDesc().Width);
        if (!f.motionHeight) f.motionHeight = f.motion->GetDesc().Height;
    }
    if (post && f.depth)
    {
        // Depth (and the reactive mask) are render-sized under the display-sized colour.
        f.guideWidth = renderW ? renderW : static_cast<UINT>(f.depth->GetDesc().Width);
        f.guideHeight = renderH ? renderH : f.depth->GetDesc().Height;
    }
    // (0.3.3.2, C6-B) One stream per presented frame, decided before the settle and the identity below
    // so another stream never restarts this one (see OneStreamPerFrame). Nothing is recorded for a
    // stream passed through, so no list is left pending in either backend.
    if (Config::Instance()->AmdOneStreamPerFrame.value_or_default())
    {
        static StreamWatch streams[2]; // pre-SR, post-RR
        if (!OneStreamPerFrame(streams[post ? 1 : 0], post, params, UINT64(f.width) * f.height, epoch))
            return true;
    }
    // Let SR finish its reconfiguration before rebuilding the private HIP model.
    // Do not retain or replay the old image while input sizes are settling.
    static UINT settlingWidth=0, settlingHeight=0;
    static float settlingScale=1.f;
    static ULONGLONG settlingSince=0;
    // Dynamic NR resolution: hold a frame-time target by nudging the model's
    // working scale in discrete, debounced steps. Each change rebuilds the model
    // (0.3.4, P10: at once, a scale-only change no longer settles 300 ms), so it must stay rare - this is console-style
    // stepped DRS, not per-frame scaling. The manual NR resolution is the ceiling.
    float sessionScale=Config::Instance()->AmdNrScale.value_or_default();
    if (Config::Instance()->AmdDynamicRes.value_or_default())
    {
        // (0.3.3.2 rebuild, #33) Hysteresis and a precise clock: DynamicNr.h has the rules and what they replace.
        static DlssNr::DynamicNr::Controller dynamicNr;
        static std::chrono::steady_clock::time_point dynLast {};
        const auto dynNow = std::chrono::steady_clock::now();
        const double dtMs = dynLast.time_since_epoch().count()
                                ? std::chrono::duration<double, std::milli>(dynNow - dynLast).count()
                                : 0.0;
        dynLast = dynNow;
        const ULONGLONG t = GetTickCount64();
        // frames in the settle window run no NR (Run returns before Record): cheaper, so left out of the average
        const bool settling = settlingSince != 0 && t - settlingSince < 300;
        // lmxxf rebuilds leak about 100 MB each unless its runtime reuses its HIP imports (R3); then no cap
        const bool capChanges = activeRuntime.load() == 2 && Lmxxf::ImportPoolState() != 1;
        std::string dynLog;
        sessionScale = dynamicNr.Update(t, dtMs, settling,
                                        std::clamp(Config::Instance()->AmdDynamicTargetFps.value_or_default(), 30, 240),
                                        Config::Instance()->AmdNrScale.value_or_default(), capChanges, dynLog);
        if (!dynLog.empty())
            LogLine(dynLog);
        // ILION dev.51: a copy for the menu (DynamicNrState); the controller and its result are unchanged
        {
            DynamicNrReadout r;
            r.running = true;
            r.scale = sessionScale;
            r.ceiling = Config::Instance()->AmdNrScale.value_or_default();
            r.floor = (std::min)(DlssNr::DynamicNr::Controller::kSteps[DlssNr::DynamicNr::Controller::kLast], r.ceiling);
            r.averageMs = dynamicNr.emaMs;
            r.targetMs = 1000.0 / std::clamp(Config::Instance()->AmdDynamicTargetFps.value_or_default(), 30, 240);
            r.changes = dynamicNr.changes;
            r.held = capChanges && dynamicNr.changes >= DlssNr::DynamicNr::Controller::kLmxxfChangeCap;
            r.upBlocked = dynamicNr.upBlockedUntil > t;
            std::scoped_lock lock(dynamicReadoutLock);
            dynamicReadout = r;
            dynamicReadoutTick = t;
        }
    }
    const float requestedScale=sessionScale;
    const auto now=GetTickCount64();
    // (0.3.4, P10) Only an input-size change restarts the settle; an NR scale change alone rebuilds at once, unless it
    // follows the previous change within the settle time (BridgeRules.h, NeedsSettle).
    static ULONGLONG lastSettingsChange=0;
    if(settlingWidth!=f.width || settlingHeight!=f.height || settlingScale!=requestedScale) {
        using namespace DlssNr::BridgeRules;
        const SettingsChange change=ClassifyChange(settlingWidth,settlingHeight,settlingScale,f.width,f.height,requestedScale);
        const bool settle=NeedsSettle(change,lastSettingsChange?now-lastSettingsChange:~0ull);
        lastSettingsChange=now;
        b->TraceBoundary("settings change: input " + std::to_string(settlingWidth) + "x" +
            std::to_string(settlingHeight) + " -> " + std::to_string(f.width) + "x" +
            std::to_string(f.height) + "; NR scale " + std::to_string(settlingScale) +
            " -> " + std::to_string(requestedScale) + (settle ? "" : "; NR scale change: no settle"));
        settlingWidth=f.width;settlingHeight=f.height;settlingScale=requestedScale;
        if(settle) settlingSince=now;
        b->InvalidateHistory();
    }
    if(now-settlingSince<300) {
        Message("AMD neural: waiting for resolution settings to settle");
        return true;
    }
    const FrameIdentity current { f.colour, f.motion, f.depth, f.width, f.height };
    // Resource addresses rotate in Unreal's frame buffers. Only an extent
    // change requires warm-up; pointer equality can suppress every frame.
    const bool sameFrame = current.width == lastFrame.width &&
                           current.height == lastFrame.height;
    if (!sameFrame)
    {
        lastFrame = current;
        stableFrames = 0;
        b->InvalidateHistory();
        Message("AMD pre-SR: warming up after an upscaler/resource change");
        return true;
    }
    if (stableFrames < 2 && ++stableFrames < 2)
    {
        Message("AMD pre-SR: warming up after an upscaler/resource change");
        return true;
    }
    f.depthInverted = (flags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) != 0;
    params->Get(NVSDK_NGX_Parameter_Reset, &reset);
    f.reset = reset != 0;
    // The reactive mask, read the same tolerant way as every other resource pointer:
    // titles set either the typed overload or the raw void**, and the same key can be
    // either depending on the engine.
    f.reactive = Resource(params, NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask);
    params->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &f.jitterX);
    params->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &f.jitterY);
    if (!std::isfinite(f.jitterX) || !std::isfinite(f.jitterY)) { f.jitterX = 0; f.jitterY = 0; }
    params->Get(NVSDK_NGX_Parameter_MV_Scale_X, &f.motionScaleX);
    params->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &f.motionScaleY);
    const auto& cfg = *Config::Instance();
    if (post)
        // A finished upscaler output arrives as a UAV unless the ini says otherwise: the same
        // assumption the NVIDIA-path post pass makes.
        f.colourState = cfg.OutputResourceBarrier.has_value()
                            ? static_cast<D3D12_RESOURCE_STATES>(cfg.OutputResourceBarrier.value())
                            : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    else if (cfg.ColorResourceBarrier.has_value())
        f.colourState = static_cast<D3D12_RESOURCE_STATES>(cfg.ColorResourceBarrier.value());
    if (!post && !cfg.ColorResourceBarrier.has_value() &&
        (State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
         State::Instance().gameEngine == GameEngineType::Unreal ||
         (State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine)))
        f.colourState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (cfg.MVResourceBarrier.has_value())
        f.motionState = static_cast<D3D12_RESOURCE_STATES>(cfg.MVResourceBarrier.value());
    if (cfg.DepthResourceBarrier.has_value())
        f.depthState = static_cast<D3D12_RESOURCE_STATES>(cfg.DepthResourceBarrier.value());
    if (cfg.ExposureResourceBarrier.has_value())
        f.exposureState = static_cast<D3D12_RESOURCE_STATES>(cfg.ExposureResourceBarrier.value());
    KeepApuInterleaveOff();
    AmdPreSr::Settings s = BuildSettings(sessionScale);
    {
        // SJ-LOG (0.3.4): a log line when a watched dial changed and has held for 30 frames (see NoteSettingsChange).
        static SettingsLogWatch settingsLog;
        NoteSettingsChange(settingsLog, s);
    }
    {
        // Display-referred colour, detected the way the NVIDIA path decides passthrough
        // (DlssNr_Dx12.cpp: the IsHDR create flag AND a format that can hold linear light), plus
        // the user's own statement (AmdEncoding sRGB / Gamma 2.2). Only the RenoDX composition
        // reads it - both backends refuse that mode on such a frame, since its tail is linear
        // only - so Classic is not touched. Without create flags only the format decides.
        const DXGI_FORMAT format = f.colour ? f.colour->GetDesc().Format : DXGI_FORMAT_UNKNOWN;
        const bool notHdr = haveFlags && !(flags & NVSDK_NGX_DLSS_Feature_Flags_IsHDR);
        const bool notLinear = !ColourHoldsLinearHdr(format);
        const bool encoded = s.encoding == 2 || s.encoding == 3;
        f.displayReferred = notHdr || notLinear || encoded;
        // Logged once per change of the answer and its reasons; a title that alternates two
        // upscaler contexts stops being logged after a few lines. (0.3.4, FB-L10) The RenoDX fallback clause only
        // when RenoDX is the composition chosen (AmdComposition 1): Classic has nothing to fall back from.
        const bool renoDxFallsBack = f.displayReferred && s.composition == 1;
        static int lastState = -1, changes = 0;
        const int state = (notHdr ? 1 : 0) | (notLinear ? 2 : 0) | (encoded ? 4 : 0) | (haveFlags ? 8 : 0) |
                          (renoDxFallsBack ? 16 : 0);
        if (state != lastState && changes < 8)
        {
            lastState = state;
            ++changes;
            std::string line = "AMD neural: colour " + std::string(post ? "(after Ray Regeneration) " : "") + "is " +
                               (f.displayReferred ? "display-referred" : "linear") + " - " +
                               (haveFlags ? (notHdr ? "no IsHDR create flag" : "IsHDR create flag") : "no create flags") +
                               ", DXGI format " + std::to_string(static_cast<int>(format)) +
                               (notLinear ? " (cannot hold linear HDR)" : " (can hold linear HDR)") +
                               (encoded ? (s.encoding == 2 ? ", AmdEncoding sRGB" : ", AmdEncoding Gamma 2.2") : "") +
                               (renoDxFallsBack ? "; AmdComposition 1 (RenoDX) falls back to Classic on it" : "");
            if (changes == 8)
                line += " (further changes are not logged)";
            LogLine(line);
        }
    }
    // lmxxf's counterpart of danielblnc's Vulkan gap line (AmdPreSr.cpp, Record, "AMD vk: no Record
    // for N ms"), here because the bridge sees every Record of both runtimes: the two causes it
    // separates (the game did not reach Evaluate, or the Vulkan bridge's CPU wait for the previous
    // frame held it) do not depend on the runtime. Counted from the previous Record's return, so
    // lmxxf's own bounded answer wait inside Record is not part of it. Run() holds frameMutex.
    static ULONGLONG lmxxfVkRecordReturned = 0;
    static UINT64 lmxxfVkRecordGaps = 0;
    const bool lmxxfVkGapCheck = s.vulkanBridge && activeRuntime.load() == 2;
    if (lmxxfVkGapCheck && lmxxfVkRecordReturned != 0)
    {
        const ULONGLONG gapMs = GetTickCount64() - lmxxfVkRecordReturned;
        if (gapMs > 1000)
        {
            ++lmxxfVkRecordGaps;
            if (lmxxfVkRecordGaps <= 10 || lmxxfVkRecordGaps % 10 == 0)
                LogLine("AMD vk (lmxxf): no Record for " + std::to_string(gapMs) +
                        " ms - either the game did not reach Evaluate or the bridge's previous-frame wait held it "
                        "(see OptiScaler.log 'has not completed after'); gap " + std::to_string(lmxxfVkRecordGaps));
        }
    }
    if (f.inputWriteBack && f.colour) {
        const auto d = f.colour->GetDesc();
        bool supported = d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
            d.SampleDesc.Count == 1 && d.DepthOrArraySize == 1 && d.MipLevels == 1 &&
            d.Width >= f.width && d.Height >= f.height;
        const bool fp16 = d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || d.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS;
        if (supported && !fp16) {
            auto format = d.Format;
            if (format == DXGI_FORMAT_R8G8B8A8_TYPELESS) format = DXGI_FORMAT_R8G8B8A8_UNORM;
            if (format == DXGI_FORMAT_R10G10B10A2_TYPELESS) format = DXGI_FORMAT_R10G10B10A2_UNORM;
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support { format };
            ID3D12Device* owner = nullptr;
            supported = (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) &&
                SUCCEEDED(f.colour->GetDevice(IID_PPV_ARGS(&owner))) && owner &&
                SUCCEEDED(owner->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof support)) &&
                (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
            if (owner) owner->Release();
        }
        if (!supported) {
            Message("Before NR skipped: input format/layout cannot be written back safely; select After");
            return true;
        }
    }
    const auto result = b->Record(cmd, f, s);
    if (result && b->GetStats().completedModelFrames > 0)
        activityCandidate.store(cmd, std::memory_order_release);
    // (0.3.3.2) The list the Reset hook watches from now on: whatever Record left on it (both backends
    // can leave work pending even without a result) is lost if the game resets it unexecuted.
    if (listRecovery.load(std::memory_order_relaxed))
        watchedList.store(cmd, std::memory_order_release);
    if (lmxxfVkGapCheck)
        lmxxfVkRecordReturned = GetTickCount64();
    if (result && !post && !f.inputWriteBack)
    {
        originalColour = f.colour;
        replacedParams = params;
        replacementColour = result;
        params->Set(NVSDK_NGX_Parameter_Color, result);
    }
    // Post-RR: the backend wrote the frame back into the output itself; nothing to swap.
    // One line in amd_bridge.log so a session can be told apart from a pre-SR one.
    static bool saidPost = false;
    if (post && result && !saidPost)
    {
        saidPost = true;
        Message(("AMD neural after Ray Regeneration: output " + std::to_string(f.width) + "x" +
                 std::to_string(f.height) + ", guides " + std::to_string(f.guideWidth) + "x" +
                 std::to_string(f.guideHeight) + ", NR scale " + std::to_string(sessionScale))
                    .c_str());
    }
    return true;
}
bool Before(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params, ID3D12CommandQueue* q, unsigned long long epoch)
{
    return RuntimeGate() && Run(cmd, params, q, false, epoch);
}
bool After(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params, ID3D12CommandQueue* q, unsigned long long epoch)
{
    return RuntimeGate() && Run(cmd, params, q, true, epoch);
}
void ListDiscarded(ID3D12CommandList* list)
{
    CancelActivity(list);
    if (!list || !listRecovery.load(std::memory_order_acquire))
        return;
    // The watched list is disarmed with it, so a later Reset of it is not reported a second time.
    ID3D12CommandList* expected = list;
    watchedList.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    if (auto b = backend.load())
        b->ListReset(list, true);
}
bool ListRecoveryOn()
{
    return listRecovery.load(std::memory_order_acquire);
}
// (0.3.4 preview) The colour chain: AMDNR Screen GI (dlssnr/gi/GiSeam.h) may replace the title's colour before NR,
// and NR then replaces GI's output. The last link is what the upscaler reads; Restore unwinds NR's link, then GI's,
// so the title's colour is back in the block. With [AmdGi] Enabled=false the seam holds nothing: Replacement is
// nullptr and its Restore changes no parameter, so these three behave exactly as before.
bool HasReplacement(NVSDK_NGX_Parameter* params)
{
    return (params && replacedParams == params && originalColour);
}
void Restore(NVSDK_NGX_Parameter* params)
{
    if (params && replacedParams == params)
    {
        params->Set(NVSDK_NGX_Parameter_Color, originalColour);
        replacedParams = nullptr;
        originalColour = nullptr;
        replacementColour = nullptr;
    }

}
ID3D12Resource* Replacement(const NVSDK_NGX_Parameter* params)
{
    return params && replacedParams == params ? replacementColour : nullptr;
}
void InvalidateHistory()
{
    CancelActivity();
    if (auto b = backend.load())
        b->InvalidateHistory();
}
void UpscalerSkipped()
{
    auto b = backend.load();
    if (!b)
        return;
    b->InvalidateHistory();
    static std::atomic<bool> said { false };
    if (!said.exchange(true))
        LogLine("AMD neural: the upscaler did not write this frame (first frames skipped, backend change or recreate, "
                "root signature, FSR 2.1.2 fallback) - no neural pass after it, NR history restarts (noted once; "
                "[DlssNr] AmdSkipUnwrittenFrames)");
}
void RequestCapture(unsigned delayMs)
{
    if (auto b = backend.load())
        b->RequestCapture(delayMs);
}
void TraceContextRelease(unsigned int handle, bool after)
{
    if (auto b = backend.load())
        b->TraceBoundary(std::string(after ? "after" : "before") +
                         " SR context release handle=" + std::to_string(handle));
}
AmdPreSr::Stats Stats()
{
    if (auto b = backend.load())
        return b->GetStats();
    return {};
}

std::string Status()
{
    {
        std::lock_guard l(messageMutex);
        if (!message.empty())
            return message;
    }
    if (auto b = backend.load())
        return b->Status();
    return "AMD pre-SR: idle";
}
AmdPreSr::Settings CurrentSettings()
{
    return BuildSettings(Config::Instance()->AmdNrScale.value_or_default());
}
bool BackendActive()
{
    return backend.load() != nullptr;
}
void SetCompositionNote(const std::string& note)
{
    std::lock_guard l(compositionNoteMutex);
    compositionNote = note;
}
std::string CompositionNote()
{
    std::lock_guard l(compositionNoteMutex);
    return compositionNote;
}
std::string SessionHeader()
{
    std::string header = ::SessionHeaderText();
    if (header.empty()) // no BeginLogSession in this process: the same facts, said here
        header = "AMDNR session start: tick " + std::to_string(GetTickCount64()) + ", pid " +
                 std::to_string(GetCurrentProcessId()) + ", exe=\"" + wstring_to_string(Util::ExePath().filename().wstring()) +
                 "\", " + "ILION " ILION_VERSION_STR " NR experimental";
    return header;
}
const char* ProductName() { return "ILION " ILION_VERSION_STR " NR experimental"; }
void NoteToggle(bool on, const char* source)
{
    auto b = backend.load();
    const std::string line = std::string("NR switched ") + (on ? "on" : "off") + " (" + (source ? source : "?") +
                             ") at recorded frame " + (b ? std::to_string(b->RecordedFrames()) : std::string("0"));
    LogLine("AMD neural: " + line);
    // The backend's own log, next to its Record lines. NoteLine, not TraceBoundary: danielblnc's
    // TraceBoundary goes through Log(), which replaced a stopped backend's "AMD idle: stopped - <why>"
    // status for the rest of the session, and it waited on the recording lock from the menu thread.
    if (b)
        b->NoteLine("AMD neural: " + line);
}
bool RuntimeStopped()
{
    if (auto b = backend.load())
        return b->Stopped();
    return false;
}
bool NrOnDeviceBehindProxy()
{
    return nrOnDeviceBehindProxy.load(std::memory_order_acquire);
}
std::string NrDeviceRefusal()
{
    std::lock_guard l(refusalMutex);
    return nrDeviceRefusal;
}
std::string ForeignStandalone()
{
    std::lock_guard l(foreignMutex);
    return foreignStandalone;
}
// VRAM and RAM telemetry for the backends' logs. The leak audit found growth that only a game can
// confirm (lmxxf per frame and per chain rebuild, danielblnc per new NR size above about 1 MP),
// and no tester log had a single memory figure in it. DXGI's LOCAL segment - CurrentUsage is
// this process's use of the adapter's dedicated memory, Budget what the OS grants it right now -
// and the process's PrivateUsage, which is Task Manager's "Commit size". DXGI rather than the
// per-process GPU counter: that counter, and Task Manager's per-process "Dedicated GPU memory"
// column, counted a HIP-imported buffer twice in the audit's probe (+128 MB per 64 MB buffer).
// The adapter is looked up once per device LUID, with adapter spoofing skipped as in IsAmd, and
// kept (never released, like the pass modules: nothing may be torn down at unload), so a log line
// never creates a DXGI factory after the first.
std::string MemoryTelemetry(ID3D12Device* device)
{
    static std::mutex adapterMutex;
    static IDXGIAdapter3* adapter = nullptr;
    static LUID adapterLuid {};
    static bool looked = false;
    std::string vram = "?", budget = "?", commit = "?";
    if (device)
    {
        std::lock_guard l(adapterMutex);
        const LUID luid = device->GetAdapterLuid();
        if (!looked || luid.LowPart != adapterLuid.LowPart || luid.HighPart != adapterLuid.HighPart)
        {
            looked = true;
            adapterLuid = luid;
            if (adapter)
            {
                adapter->Release();
                adapter = nullptr;
            }
            struct PhysicalAdapterScope
            {
                uint64_t id = SkipSpoof::AddEntry(SkipSpoofType::Thread);
                ~PhysicalAdapterScope() { SkipSpoof::RemoveEntry(id); }
            } physicalAdapterScope;
            IDXGIFactory4* f = nullptr;
            if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f))))
            {
                if (FAILED(f->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))))
                    adapter = nullptr;
                f->Release();
            }
        }
        DXGI_QUERY_VIDEO_MEMORY_INFO m {};
        if (adapter && SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &m)))
        {
            vram = std::to_string(m.CurrentUsage >> 20);
            budget = std::to_string(m.Budget >> 20);
        }
    }
    PROCESS_MEMORY_COUNTERS_EX pmc {};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        commit = std::to_string(pmc.PrivateUsage >> 20);
    return " vramMB=" + vram + " budgetMB=" + budget + " privateMB=" + commit;
}
} // namespace DlssNr::AmdBridge

void AmdNrRequestCapture(unsigned delayMs) { DlssNr::AmdBridge::RequestCapture(delayMs); }
