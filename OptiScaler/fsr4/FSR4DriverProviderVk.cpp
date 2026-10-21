#include "pch.h"
#include "FSR4DriverProviderVk.h"

#include "FSR4PipelineLayouts.h"
#include "FSR4Versions.h"

#include <Config.h>
#include <State.h>
#include <Util.h>
#include <ffx_upscale.h>
#include "vk/ffx_api_vk.h"

#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#define SPV_ENABLE_UTILITY_CODE
#include "../../external/FidelityFX-SDK/sdk/tools/ffx_shader_compiler/libs/SPIRV-Reflect/include/spirv/unified1/spirv.h"
#undef SPV_ENABLE_UTILITY_CODE

// From upstream: the shader rewrite the driver provider's contexts go through
// What the providers receive in IsSupported, same as ffxCreateBackendVKDesc without the header
struct ProviderBackendVk
{
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    PFN_vkGetDeviceProcAddr gdpa;
};

// Shader modules created by the driver provider get rewritten before reaching the driver:
// AMD's FSR 4 shaders read/write images declared with a 16 bit float sampled type, which only AMD's own
// driver accepts, so they get rewritten to 32 bit float images.
namespace
{
struct DriverDeviceFunctions
{
    PFN_vkGetDeviceProcAddr getDeviceProcAddr;
    PFN_vkCreateShaderModule createShaderModule;
};

std::mutex driverDevicesMutex;
std::unordered_map<VkDevice, DriverDeviceFunctions> driverDevices;
PfnFfxCreateContext driverCreateContext = nullptr;

DriverDeviceFunctions GetDriverDeviceFunctions(VkDevice device)
{
    std::lock_guard lock(driverDevicesMutex);
    const auto entry = driverDevices.find(device);
    return entry != driverDevices.end() ? entry->second : DriverDeviceFunctions {};
}

template <typename Word> std::vector<std::span<Word>> ParseSpirvInstructions(std::span<Word> code)
{
    if (code.size() < 5 || code[0] != SpvMagicNumber)
        return {};

    std::vector<std::span<Word>> instructions;
    for (size_t offset = 5; offset < code.size();)
    {
        const size_t count = code[offset] >> 16;
        if (count == 0 || count > code.size() - offset)
            return {};
        instructions.push_back(code.subspan(offset, count));
        offset += count;
    }
    return instructions;
}

void Emit(std::vector<uint32_t>& output, SpvOp opcode, std::initializer_list<uint32_t> operands)
{
    output.push_back((static_cast<uint32_t>(operands.size() + 1) << 16) | opcode);
    output.insert(output.end(), operands.begin(), operands.end());
}

static std::vector<uint32_t> NormalizeHalfImageIO(std::span<const uint32_t> code)
{
    const auto originalInstructions = ParseSpirvInstructions(code);
    if (originalInstructions.empty())
        return {};

    std::unordered_set<uint32_t> halfTypes;
    uint32_t floatType = 0;
    for (auto instruction : originalInstructions)
    {
        if ((instruction[0] & 0xffff) != SpvOpTypeFloat || instruction.size() != 3)
            continue;
        if (instruction[2] == 16)
            halfTypes.insert(instruction[1]);
        else if (instruction[2] == 32)
            floatType = instruction[1];
    }

    const bool hasHalfImages = std::any_of(originalInstructions.begin(), originalInstructions.end(),
                                           [&](auto instruction)
                                           {
                                               return (instruction[0] & 0xffff) == SpvOpTypeImage &&
                                                      instruction.size() >= 9 && halfTypes.contains(instruction[2]);
                                           });
    if (!hasHalfImages || floatType == 0)
        return {};

    std::vector<uint32_t> normalized(code.begin(), code.end());
    const auto instructions = ParseSpirvInstructions(std::span<uint32_t>(normalized));
    auto nextId = code[3];
    std::vector<uint32_t> declarations;

    std::unordered_map<uint32_t, uint32_t> promotedTypes;
    for (auto type : halfTypes)
        promotedTypes[type] = floatType;

    std::unordered_map<uint32_t, uint32_t> floatVectorTypes;
    for (auto instruction : instructions)
    {
        if ((instruction[0] & 0xffff) == SpvOpTypeVector && instruction.size() == 4 && instruction[2] == floatType)
            floatVectorTypes.try_emplace(instruction[3], instruction[1]);
    }
    for (auto instruction : instructions)
    {
        if ((instruction[0] & 0xffff) != SpvOpTypeVector || instruction.size() != 4 ||
            !halfTypes.contains(instruction[2]))
            continue;

        const auto existing = floatVectorTypes.find(instruction[3]);
        const auto promoted = existing != floatVectorTypes.end() ? existing->second : nextId++;
        promotedTypes[instruction[1]] = promoted;
        if (existing == floatVectorTypes.end())
            Emit(declarations, SpvOpTypeVector, { promoted, floatType, instruction[3] });
    }

    std::unordered_map<uint32_t, uint32_t> halfValueTypes;
    for (auto instruction : instructions)
    {
        bool hasResult = false, hasType = false;
        SpvHasResultAndType(static_cast<SpvOp>(instruction[0] & 0xffff), &hasResult, &hasType);
        if (hasResult && hasType && instruction.size() >= 3 && promotedTypes.contains(instruction[1]))
            halfValueTypes[instruction[2]] = instruction[1];
    }

    std::unordered_map<uint32_t, uint32_t> aliases;
    std::vector<std::span<uint32_t>> canonicalTypes;
    auto resolveType = [&](uint32_t& type)
    {
        if (const auto alias = aliases.find(type); alias != aliases.end())
            type = alias->second;
    };
    for (auto instruction : instructions)
    {
        const auto opcode = static_cast<SpvOp>(instruction[0] & 0xffff);
        if (opcode == SpvOpTypeImage && instruction.size() >= 9)
        {
            if (halfTypes.contains(instruction[2]))
                instruction[2] = floatType;
        }
        else if (opcode == SpvOpTypeSampledImage && instruction.size() == 3)
            resolveType(instruction[2]);
        else if (opcode == SpvOpTypePointer && instruction.size() == 4)
            resolveType(instruction[3]);
        else
            continue;

        const auto existing =
            std::find_if(canonicalTypes.begin(), canonicalTypes.end(),
                         [&](auto candidate)
                         {
                             return candidate[0] == instruction[0] &&
                                    std::equal(candidate.begin() + 2, candidate.end(), instruction.begin() + 2);
                         });
        if (existing != canonicalTypes.end())
            aliases[instruction[1]] = (*existing)[1];
        else
            canonicalTypes.push_back(instruction);
    }
    for (auto instruction : instructions)
    {
        const auto opcode = static_cast<SpvOp>(instruction[0] & 0xffff);
        bool hasResult = false, hasType = false;
        SpvHasResultAndType(opcode, &hasResult, &hasType);
        if (hasResult && hasType && instruction.size() >= 3)
            resolveType(instruction[1]);
        if ((opcode == SpvOpName || opcode == SpvOpDecorate) && instruction.size() >= 3)
            resolveType(instruction[1]);
        if (opcode == SpvOpTypeFunction)
            for (size_t i = 2; i < instruction.size(); ++i)
                resolveType(instruction[i]);
    }

    std::vector<uint32_t> result(code.begin(), code.begin() + 5);
    result.reserve(code.size() + declarations.size());
    bool declarationsAdded = false;
    for (auto instruction : instructions)
    {
        const auto opcode = static_cast<SpvOp>(instruction[0] & 0xffff);
        if ((opcode == SpvOpTypeImage || opcode == SpvOpTypeSampledImage || opcode == SpvOpTypePointer) &&
            instruction.size() >= 2 && aliases.contains(instruction[1]))
            continue;
        if (opcode == SpvOpFunction && !declarationsAdded)
        {
            result.insert(result.end(), declarations.begin(), declarations.end());
            declarationsAdded = true;
        }
        const auto offset = result.size();
        if (opcode >= SpvOpImageSampleImplicitLod && opcode <= SpvOpImageRead && instruction.size() >= 4)
        {
            const auto promoted = promotedTypes.find(instruction[1]);
            if (promoted != promotedTypes.end())
            {
                const auto temporary = nextId++;
                result.insert(result.end(), instruction.begin(), instruction.end());
                result[offset + 1] = promoted->second;
                result[offset + 2] = temporary;
                Emit(result, SpvOpFConvert, { instruction[1], instruction[2], temporary });
                continue;
            }
        }
        if (opcode == SpvOpImageWrite && instruction.size() >= 4)
        {
            const auto type = halfValueTypes.find(instruction[3]);
            if (type != halfValueTypes.end())
            {
                const auto temporary = nextId++;
                Emit(result, SpvOpFConvert, { promotedTypes.at(type->second), temporary, instruction[3] });
                const auto writeOffset = result.size();
                result.insert(result.end(), instruction.begin(), instruction.end());
                result[writeOffset + 3] = temporary;
                continue;
            }
        }
        result.insert(result.end(), instruction.begin(), instruction.end());
    }
    result[3] = nextId;
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDriverShaderModule(VkDevice device, const VkShaderModuleCreateInfo* info,
                                                        const VkAllocationCallbacks* allocator, VkShaderModule* module)
{
    const auto functions = GetDriverDeviceFunctions(device);
    if (functions.createShaderModule == nullptr)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (info == nullptr || info->pCode == nullptr || info->codeSize % sizeof(uint32_t) != 0)
        return functions.createShaderModule(device, info, allocator, module);

    const std::span<const uint32_t> original { info->pCode, info->codeSize / sizeof(uint32_t) };
    const auto code = NormalizeHalfImageIO(original);
    if (code.empty())
        return functions.createShaderModule(device, info, allocator, module);

    auto compatibleInfo = *info;
    compatibleInfo.pCode = code.data();
    compatibleInfo.codeSize = code.size() * sizeof(uint32_t);
    LOG_DEBUG("rewrote driver shader ({} -> {} bytes)", info->codeSize, compatibleInfo.codeSize);
    return functions.createShaderModule(device, &compatibleInfo, allocator, module);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDriverDeviceProcAddr(VkDevice device, const char* name)
{
    const auto functions = GetDriverDeviceFunctions(device);
    if (name != nullptr && strcmp(name, "vkCreateShaderModule") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&CreateDriverShaderModule);
    return functions.getDeviceProcAddr != nullptr ? functions.getDeviceProcAddr(device, name) : nullptr;
}

ffxReturnCode_t CreateWithDriverShaders(PfnFfxCreateContext original, ffxContext* context,
                                        ffxCreateContextDescHeader* desc, const ffxAllocationCallbacks* allocator)
{
    for (auto header = desc; header != nullptr; header = header->pNext)
    {
        if (header->type != FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_V2)
            continue;

        auto backend = reinterpret_cast<ffxCreateBackendVKDesc*>(header);
        const auto gdpa = backend->vkDeviceProcAddr;
        if (gdpa == nullptr)
            break;
        const auto createShader =
            reinterpret_cast<PFN_vkCreateShaderModule>(gdpa(backend->vkDevice, "vkCreateShaderModule"));
        if (createShader == nullptr)
            break;
        {
            std::lock_guard lock(driverDevicesMutex);
            driverDevices[backend->vkDevice] = { gdpa, createShader };
        }
        backend->vkDeviceProcAddr = &GetDriverDeviceProcAddr;
        const auto result = original(context, desc, allocator);
        backend->vkDeviceProcAddr = gdpa;
        return result;
    }
    return original(context, desc, allocator);
}

ffxReturnCode_t CreateDriverContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                    const ffxAllocationCallbacks* allocator)
{
    return CreateWithDriverShaders(driverCreateContext, context, desc, allocator);
}

} // namespace


namespace
{
// The first amdvkffx64.dll of ILION's FSR 4 folders, the OptiScaler folder or the installed driver whose file passes
// the check (64 bit dll exporting UpdateFfxApiProvider)
std::wstring FindDriverDll()
{
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;

    const auto ilionFsr4 = Util::ExePath().parent_path() / L"ILION" / L"Escaladores" / L"FSR4";
    if (std::filesystem::is_directory(ilionFsr4, ec))
    {
        std::vector<std::filesystem::path> folders;
        for (const auto& sub : std::filesystem::directory_iterator(ilionFsr4, ec))
        {
            if (sub.is_directory(ec))
                folders.push_back(sub.path());
        }

        std::ranges::sort(folders);
        for (const auto& folder : folders)
            candidates.push_back(folder / L"amdvkffx64.dll");
    }

    candidates.push_back(std::filesystem::path(Config::Instance()->MainDllPath.value_or(L"")) / L"amdvkffx64.dll");

    for (const auto& [luid, path] : Util::GetDriverStore())
        candidates.push_back(path / L"amdvkffx64.dll");

    for (const auto& candidate : candidates)
    {
        if (!std::filesystem::exists(candidate, ec))
            continue;

        bool driverProvider = false;
        std::string problem;

        if (FSR4Versions::Validate(candidate.wstring(), &problem, &driverProvider) && driverProvider)
            return candidate.wstring();

        LOG_INFO("{} not used: {}", wstring_to_string(candidate.wstring()), problem.empty() ? "not a provider" : problem);
    }

    return L"";
}
} // namespace

bool FSR4DriverProviderVk::Init()
{
    static bool initTried = false;

    if (initTried)
        return IsAvailable();

    initTried = true;

    const auto path = FindDriverDll();
    if (path.empty())
    {
        LOG_INFO("No AMD Vulkan driver FSR provider (amdvkffx64.dll) found");
        return false;
    }

    FSR4DriverProvider::Upscaler upscaler;
    std::string problem;

    {
        // amdvkffx64 only gives out providers when it sees signed FFX SDK dlls; it stands in for them when missing
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};

        if (!FSR4DriverProvider::LoadUpscaler(path,
                                              { L"amd_fidelityfx_loader_vk.dll", L"amd_fidelityfx_upscaler_vk.dll",
                                                L"amd_fidelityfx_framegeneration_vk.dll" },
                                              { 3u }, true, upscaler, problem))
        {
            LOG_WARN("AMD Vulkan driver provider {} not used: {}", wstring_to_string(path), problem);
            return false;
        }
    }

    FSR4PipelineLayouts::Hook(upscaler.module);

    _upscaler = std::move(upscaler);
    _path = path;
    driverCreateContext = reinterpret_cast<PfnFfxCreateContext>(_upscaler.functions.createContext);
    return true;
}

PfnFfxCreateContext FSR4DriverProviderVk::CreateContext() { return IsAvailable() ? &CreateDriverContext : nullptr; }

bool FSR4DriverProviderVk::IsSupported(VkDevice device, VkPhysicalDevice physicalDevice, PFN_vkGetDeviceProcAddr gdpa)
{
    if (!IsAvailable())
        return false;

    if (_upscaler.isSupported == nullptr)
        return true;

    ProviderBackendVk backend { device, physicalDevice, gdpa };

    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
    return _upscaler.isSupported(&backend);
}
