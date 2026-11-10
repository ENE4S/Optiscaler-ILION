#pragma once

#include <d3d12.h>

#include <filesystem>
#include <string>

// ILION v1.4.0 fase 2 exp5: what XeFG asks D3D12 for while ILION calls XeMFG XMX (realdody). XeFG's Init reports only
// "Failed to create heap"; ILION's own device hooks (D3D12_Hooks: CreateHeap, CreateHeap1, CreateDescriptorHeap,
// GetResourceAllocationInfo) see the request and D3D12's answer. Only with XeMFG XMX as the DLSSG replacement, only on
// the thread inside a call into it. A heap D3D12 refuses is tried once more without its optional flags / alignment
// (a host compatibility retry, as OptiScaler's other D3D12 fixes); everything goes to ilion_xemfg_d3d12.log next to
// realdody's DLL and to the menu's summary
namespace IlionXmxProbe
{
// Set by Nvngx_XeMfgXmx around every call into realdody's DLL (Init, CreateFeature, Evaluate, Release...)
class Scope
{
  public:
    Scope();
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

// Off unless [Plugin.XeMFG XMX] IlionD3D12Probe = true (a diagnosis tool, not for every player)
bool Enabled();
// The hooks are installed (XeMFG XMX was the replacement when the device was made)
void SetInstalled(bool installed);
// Inside a call into XeMFG XMX on this thread
bool Active();
// The log goes next to realdody's DLL
void SetFolder(const std::filesystem::path& folder);

void OnAllocationInfo(UINT visibleMask, UINT count, const D3D12_RESOURCE_DESC* descs,
                      const D3D12_RESOURCE_ALLOCATION_INFO& result);
void OnCreateHeap(const char* api, const D3D12_HEAP_DESC& desc, HRESULT hr, const void* heap = nullptr);
// exp6: what XeFG does with those heaps and what it checks
void OnFeature(D3D12_FEATURE feature, const void* data, UINT size, HRESULT hr);
void OnResource(const char* api, const std::string& where, const std::string& desc, HRESULT hr, const void* resource);
void OnOther(const std::string& text);
void OnHeapRetry(const char* api, const D3D12_HEAP_DESC& desc, HRESULT hr);
void OnCreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC& desc, HRESULT hr);

// A heap request D3D12 refused can be asked again with these (false: nothing optional to take out)
bool RetryDesc(const D3D12_HEAP_DESC& desc, D3D12_HEAP_DESC& retry);

struct Summary
{
    bool installed = false;
    int heaps = 0;               // heap creations seen inside XeMFG XMX
    int heapFailures = 0;        // refused by D3D12
    int retriesOk = 0;           // refused, then made by the retry
    int invalidAllocations = 0;  // GetResourceAllocationInfo answered "invalid" (UINT64_MAX)
    std::string lastFailure;     // the last refused request and its HRESULT
    std::string lastInvalid;     // the last resource D3D12 called invalid
};
Summary Get();

std::string Describe(const D3D12_HEAP_DESC& desc);
std::string Describe(const D3D12_RESOURCE_DESC& desc);
} // namespace IlionXmxProbe
