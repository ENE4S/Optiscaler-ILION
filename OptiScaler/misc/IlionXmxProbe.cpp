#include "pch.h"
#include "IlionXmxProbe.h"

#include <atomic>
#include <format>
#include <fstream>
#include <mutex>

namespace
{
thread_local int depth = 0;
std::atomic<bool> installed = false;

std::mutex lock;
std::filesystem::path folder;
std::ofstream file;
bool fileOpened = false;
int lines = 0;
constexpr int maxLines = 4000;
IlionXmxProbe::Summary summary;

void Write(const std::string& text)
{
    // lock held
    if (!fileOpened)
    {
        if (folder.empty())
            return;
        fileOpened = true;
        file.open(folder / L"ilion_xemfg_d3d12.log", std::ios::out | std::ios::trunc);
        if (file.is_open())
        {
            SYSTEMTIME t {};
            GetLocalTime(&t);
            file << std::format("ILION XeMFG XMX D3D12 probe, {:02}:{:02}:{:02}. Requests XeFG makes to D3D12 while ILION "
                                "calls XeMFG XMX\n",
                                t.wHour, t.wMinute, t.wSecond);
        }
    }

    if (!file.is_open() || lines >= maxLines)
        return;

    SYSTEMTIME t {};
    GetLocalTime(&t);
    file << std::format("[{:02}:{:02}:{:02}.{:03}] {}\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, text);
    if (++lines == maxLines)
        file << "(line limit reached)\n";
    file.flush();
}

const char* HeapTypeName(D3D12_HEAP_TYPE type)
{
    switch (type)
    {
    case D3D12_HEAP_TYPE_DEFAULT:
        return "DEFAULT";
    case D3D12_HEAP_TYPE_UPLOAD:
        return "UPLOAD";
    case D3D12_HEAP_TYPE_READBACK:
        return "READBACK";
    case D3D12_HEAP_TYPE_CUSTOM:
        return "CUSTOM";
    default:
        return "GPU_UPLOAD/other";
    }
}

const char* DimensionName(D3D12_RESOURCE_DIMENSION dimension)
{
    switch (dimension)
    {
    case D3D12_RESOURCE_DIMENSION_BUFFER:
        return "BUFFER";
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        return "TEX1D";
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        return "TEX2D";
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
        return "TEX3D";
    default:
        return "UNKNOWN";
    }
}

// The flags a heap needs for what it holds (resource heap tier 1 categories); the rest are optional behaviour
constexpr UINT kCategoryFlags = D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES |
                                D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;
} // namespace

namespace IlionXmxProbe
{
Scope::Scope() { depth++; }
Scope::~Scope() { depth--; }

bool Enabled()
{
    const auto value = Config::Instance()->GetRawValue("Plugin.XeMFG XMX", "IlionD3D12Probe");
    return value.has_value() && (_stricmp(value->c_str(), "true") == 0 || *value == "1");
}

void SetInstalled(bool value)
{
    installed = value;
    std::scoped_lock guard(lock);
    summary.installed = value;
}

bool Active() { return depth > 0 && installed.load(); }

void SetFolder(const std::filesystem::path& value)
{
    std::scoped_lock guard(lock);
    if (folder.empty())
        folder = value;
}

std::string Describe(const D3D12_HEAP_DESC& desc)
{
    return std::format("size {} B ({:.1f} MB), type {} (cpu page {}, pool {}, nodes {}/{}), alignment {}, flags 0x{:X}",
                       desc.SizeInBytes, desc.SizeInBytes / 1048576.0, HeapTypeName(desc.Properties.Type),
                       (int) desc.Properties.CPUPageProperty, (int) desc.Properties.MemoryPoolPreference,
                       desc.Properties.CreationNodeMask, desc.Properties.VisibleNodeMask, desc.Alignment,
                       (UINT) desc.Flags);
}

std::string Describe(const D3D12_RESOURCE_DESC& desc)
{
    return std::format("{} {}x{}x{} mips {} format {} samples {} layout {} alignment {} flags 0x{:X}",
                       DimensionName(desc.Dimension), desc.Width, desc.Height, desc.DepthOrArraySize, desc.MipLevels,
                       (int) desc.Format, desc.SampleDesc.Count, (int) desc.Layout, desc.Alignment, (UINT) desc.Flags);
}

void OnAllocationInfo(UINT visibleMask, UINT count, const D3D12_RESOURCE_DESC* descs,
                      const D3D12_RESOURCE_ALLOCATION_INFO& result)
{
    const bool invalid = result.SizeInBytes == UINT64_MAX;
    std::scoped_lock guard(lock);
    if (invalid)
    {
        summary.invalidAllocations++;
        summary.lastInvalid = count > 0 && descs != nullptr ? Describe(descs[0]) : std::string("no desc");
    }

    Write(std::format("GetResourceAllocationInfo: {} resource(s), visible mask {} -> size {} alignment {}{}", count,
                      visibleMask, result.SizeInBytes, result.Alignment, invalid ? "  ** INVALID **" : ""));
    for (UINT i = 0; descs != nullptr && i < count && i < 16; i++)
        Write(std::format("    [{}] {}", i, Describe(descs[i])));
}

void OnCreateHeap(const char* api, const D3D12_HEAP_DESC& desc, HRESULT hr, const void* heap)
{
    std::scoped_lock guard(lock);
    summary.heaps++;
    if (FAILED(hr))
    {
        summary.heapFailures++;
        summary.lastFailure = std::format("{}: 0x{:08X}, {}", api, (UINT) hr, Describe(desc));
    }
    Write(std::format("{}: {} -> 0x{:08X} heap {}{}", api, Describe(desc), (UINT) hr, heap,
                      FAILED(hr) ? "  ** REFUSED **" : ""));
}

void OnFeature(D3D12_FEATURE feature, const void* data, UINT size, HRESULT hr)
{
    std::string bytes;
    const auto* b = (const unsigned char*) data;
    for (UINT i = 0; b != nullptr && SUCCEEDED(hr) && i < size && i < 24; i++)
        bytes += std::format("{:02X}", b[i]);

    std::scoped_lock guard(lock);
    Write(std::format("CheckFeatureSupport: feature {} size {} -> 0x{:08X} data {}", (int) feature, size, (UINT) hr, bytes));
}

void OnResource(const char* api, const std::string& where, const std::string& desc, HRESULT hr, const void* resource)
{
    std::scoped_lock guard(lock);
    if (FAILED(hr))
    {
        summary.heapFailures++;
        summary.lastFailure = std::format("{}: 0x{:08X}, {} {}", api, (UINT) hr, where, desc);
    }
    Write(std::format("{}: {} {} -> 0x{:08X} resource {}{}", api, where, desc, (UINT) hr, resource,
                      FAILED(hr) ? "  ** REFUSED **" : ""));
}

void OnOther(const std::string& text)
{
    std::scoped_lock guard(lock);
    Write(text);
}

void OnHeapRetry(const char* api, const D3D12_HEAP_DESC& desc, HRESULT hr)
{
    std::scoped_lock guard(lock);
    if (SUCCEEDED(hr))
        summary.retriesOk++;
    Write(std::format("{} retry: {} -> 0x{:08X}{}", api, Describe(desc), (UINT) hr,
                      SUCCEEDED(hr) ? "  (made by ILION's retry)" : "  (still refused)"));
}

void OnCreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC& desc, HRESULT hr)
{
    std::scoped_lock guard(lock);
    Write(std::format("CreateDescriptorHeap: type {} count {} flags 0x{:X} node {} -> 0x{:08X}{}", (int) desc.Type,
                      desc.NumDescriptors, (UINT) desc.Flags, desc.NodeMask, (UINT) hr,
                      FAILED(hr) ? "  ** REFUSED **" : ""));
}

bool RetryDesc(const D3D12_HEAP_DESC& desc, D3D12_HEAP_DESC& retry)
{
    // A size D3D12 itself called invalid can't be helped
    if (desc.SizeInBytes == 0 || desc.SizeInBytes == UINT64_MAX)
        return false;

    retry = desc;
    retry.Flags = (D3D12_HEAP_FLAGS) ((UINT) desc.Flags & kCategoryFlags);
    if (desc.Alignment != 0 && desc.Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT &&
        desc.Alignment != D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT)
        retry.Alignment = 0;
    // Its size rounded up to the 64 KB a heap is made of
    retry.SizeInBytes = (desc.SizeInBytes + 65535ull) & ~65535ull;

    return retry.Flags != desc.Flags || retry.Alignment != desc.Alignment || retry.SizeInBytes != desc.SizeInBytes;
}

Summary Get()
{
    std::scoped_lock guard(lock);
    return summary;
}
} // namespace IlionXmxProbe
