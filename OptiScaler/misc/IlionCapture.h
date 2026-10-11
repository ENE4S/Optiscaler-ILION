#pragma once

#include <windows.h>
#include <d3d12.h>
#include <wincodec.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

// Capture of the game's final image for the visual comparison of ILION (DX12).
//
// The image is copied from the swapchain back buffer right before the menu is drawn on it, so it is what the
// game presents, without anything of ILION. Nothing waits for the GPU: the copy is recorded in the command list
// the menu already submits, a fence tells in later frames when it finished, and reading the pixels back plus
// writing the PNG happens on a thread of its own.
//
// 8 bit back buffers are saved as 24 bit PNG, 10 bit ones (HDR10) as 48 bit PNG with their values as they are
// (PQ encoded, not converted). Floating point back buffers (scRGB) are not supported.
class IlionCapture
{
  public:
    enum class State : int
    {
        Idle,
        Requested,  // waiting for the next presented frame
        WaitingGpu, // copy submitted, waiting for the GPU to finish it
        Encoding,   // pixels are being read back and written
        Done,
        Failed,
    };

    // What was in use when the capture was asked for, filled by the caller
    struct Meta
    {
        std::string game;
        std::string upscaler;
        std::string resolution;
        std::string fsrVersion;
        std::string dllPath;
        std::string dllFileVersion;
        std::string dllSha256;
        std::string frameGeneration;
    };

    static State GetState() { return (State) _state.load(std::memory_order_acquire); }

    static bool Busy()
    {
        const auto state = GetState();
        return state == State::Requested || state == State::WaitingGpu || state == State::Encoding;
    }

    static std::string LastFile()
    {
        std::scoped_lock lock(_mutex);
        return _lastFile;
    }

    static std::string LastError()
    {
        std::scoped_lock lock(_mutex);
        return _lastError;
    }

    // From the menu. The image of the next presented frame gets saved into the folder.
    static bool Request(const Meta& meta, const std::filesystem::path& folder)
    {
        if (Busy())
            return false;

        {
            std::scoped_lock lock(_mutex);
            _meta = meta;
            _folder = folder;
            _lastError.clear();
            _lastFile.clear();
        }

        _requestTick = GetTickCount64();
        _state.store((int) State::Requested, std::memory_order_release);
        return true;
    }

    // A capture is waiting for a frame
    static bool Wanted() { return GetState() == State::Requested; }

    // Records the copy of the back buffer. It has to be in the PRESENT state and is left in it.
    static void RecordCopy(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, ID3D12Resource* backBuffer)
    {
        if (!Wanted() || device == nullptr || commandList == nullptr || backBuffer == nullptr)
            return;

        const auto desc = backBuffer->GetDesc();

        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 ||
            BytesPerPixel(desc.Format) == 0)
        {
            Fail("The format of the game's image isn't supported for captures");
            return;
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
        UINT rows = 0;
        UINT64 rowSize = 0;
        UINT64 totalSize = 0;

        device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowSize, &totalSize);

        D3D12_HEAP_PROPERTIES heap {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC bufferDesc {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = totalSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ReleasePending();

        if (device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                            nullptr, IID_PPV_ARGS(&_readback)) != S_OK ||
            device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)) != S_OK)
        {
            ReleasePending();
            Fail("Can't create the buffers for the capture");
            return;
        }

        D3D12_RESOURCE_BARRIER barrier {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffer;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        commandList->ResourceBarrier(1, &barrier);

        D3D12_TEXTURE_COPY_LOCATION source {};
        source.pResource = backBuffer;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION destination {};
        destination.pResource = _readback;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;

        commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commandList->ResourceBarrier(1, &barrier);

        _width = (UINT) desc.Width;
        _height = desc.Height;
        _format = desc.Format;
        _rowPitch = footprint.Footprint.RowPitch;
        _totalSize = totalSize;
        _recorded = true;
    }

    // Right after the command list with the copy was submitted to the queue
    static void Submitted(ID3D12CommandQueue* queue)
    {
        if (!_recorded || queue == nullptr || _fence == nullptr)
            return;

        _recorded = false;
        _fenceValue = 1;
        queue->Signal(_fence, _fenceValue);

        _submitTick = GetTickCount64();
        _state.store((int) State::WaitingGpu, std::memory_order_release);
    }

    // Once per presented frame, never waits
    static void Poll()
    {
        const auto state = GetState();

        // No frame came through the capture point, e.g. the menu isn't drawn as an overlay here
        if (state == State::Requested && GetTickCount64() - _requestTick > 3000)
        {
            Fail("The game's image couldn't be reached, captures need DX12 with the overlay menu");
            return;
        }

        if (state != State::WaitingGpu)
            return;

        if (_fence->GetCompletedValue() < _fenceValue)
        {
            if (GetTickCount64() - _submitTick > 5000)
            {
                ReleasePending();
                Fail("The GPU didn't finish the copy in time");
            }

            return;
        }

        _state.store((int) State::Encoding, std::memory_order_release);

        // The thread owns the buffer from here on
        ID3D12Resource* readback = _readback;
        _readback = nullptr;

        _fence->Release();
        _fence = nullptr;

        Meta meta;
        std::filesystem::path folder;

        {
            std::scoped_lock lock(_mutex);
            meta = _meta;
            folder = _folder;
        }

        std::thread(
            [readback, meta, folder, width = _width, height = _height, format = _format, rowPitch = _rowPitch,
             totalSize = _totalSize]
            {
                std::string error;
                std::string file;

                const bool ok =
                    ReadAndSave(readback, width, height, format, rowPitch, totalSize, meta, folder, &file, &error);

                readback->Release();

                {
                    std::scoped_lock lock(_mutex);
                    _lastFile = file;
                    _lastError = error;
                }

                _state.store((int) (ok ? State::Done : State::Failed), std::memory_order_release);
            })
            .detach();
    }

    static void Dismiss()
    {
        const auto state = GetState();

        if (state == State::Done || state == State::Failed)
            _state.store((int) State::Idle, std::memory_order_release);
    }

    // 3 for the 8 bit formats, 6 for the 10 bit one (saved as 16 bit per channel), 0 when not supported
    static UINT OutputBytesPerPixel(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return 3;

        case DXGI_FORMAT_R10G10B10A2_UNORM:
            return 6;

        default:
            return 0;
        }
    }

    // Pixels of the back buffer to what gets written: BGR 8 bit, or RGB 16 bit for the 10 bit format
    static bool ConvertPixels(const BYTE* source, UINT width, UINT height, UINT rowPitch, DXGI_FORMAT format,
                              std::vector<BYTE>& out)
    {
        const UINT outBytes = OutputBytesPerPixel(format);

        if (outBytes == 0)
            return false;

        out.resize((size_t) width * height * outBytes);

        for (UINT y = 0; y < height; y++)
        {
            const BYTE* in = source + (size_t) y * rowPitch;
            BYTE* to = out.data() + (size_t) y * width * outBytes;

            for (UINT x = 0; x < width; x++, in += 4, to += outBytes)
            {
                switch (format)
                {
                case DXGI_FORMAT_R8G8B8A8_UNORM:
                case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                    to[0] = in[2];
                    to[1] = in[1];
                    to[2] = in[0];
                    break;

                case DXGI_FORMAT_B8G8R8A8_UNORM:
                case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                    to[0] = in[0];
                    to[1] = in[1];
                    to[2] = in[2];
                    break;

                default:
                {
                    uint32_t value;
                    memcpy(&value, in, sizeof(value));

                    // 10 bit to 16 bit, the top bits are repeated at the bottom so full scale stays full scale
                    const auto widen = [](uint32_t ten) { return (uint16_t) ((ten << 6) | (ten >> 4)); };

                    const uint16_t channels[3] = { widen(value & 0x3FF), widen((value >> 10) & 0x3FF),
                                                   widen((value >> 20) & 0x3FF) };
                    memcpy(to, channels, sizeof(channels));
                    break;
                }
                }
            }
        }

        return true;
    }

    // PNG through the imaging component of Windows, lossless
    static bool WritePng(const std::filesystem::path& path, const std::vector<BYTE>& pixels, UINT width, UINT height,
                         UINT bytesPerPixel, std::string* error)
    {
        const auto fail = [&](const char* text)
        {
            if (error != nullptr)
                *error = text;

            return false;
        };

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool comOwned = comResult == S_OK || comResult == S_FALSE;

        IWICImagingFactory* factory = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapEncoder* encoder = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;

        WICPixelFormatGUID pixelFormat = bytesPerPixel == 6 ? GUID_WICPixelFormat48bppRGB : GUID_WICPixelFormat24bppBGR;
        const WICPixelFormatGUID wantedFormat = pixelFormat;

        bool ok = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)) == S_OK &&
                  factory->CreateStream(&stream) == S_OK &&
                  stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE) == S_OK &&
                  factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder) == S_OK &&
                  encoder->Initialize(stream, WICBitmapEncoderNoCache) == S_OK &&
                  encoder->CreateNewFrame(&frame, nullptr) == S_OK && frame->Initialize(nullptr) == S_OK &&
                  frame->SetSize(width, height) == S_OK && frame->SetPixelFormat(&pixelFormat) == S_OK;

        // The encoder may answer with another format, the pixels are only right for the one that was asked for
        ok = ok && IsEqualGUID(pixelFormat, wantedFormat) &&
             frame->WritePixels(height, width * bytesPerPixel, (UINT) pixels.size(), (BYTE*) pixels.data()) == S_OK &&
             frame->Commit() == S_OK && encoder->Commit() == S_OK;

        if (frame != nullptr)
            frame->Release();

        if (encoder != nullptr)
            encoder->Release();

        if (stream != nullptr)
            stream->Release();

        if (factory != nullptr)
            factory->Release();

        if (comOwned)
            CoUninitialize();

        if (!ok)
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            return fail("Can't write the PNG file");
        }

        return true;
    }

  private:
    inline static std::atomic<int> _state { (int) State::Idle };
    inline static std::mutex _mutex;
    inline static Meta _meta;
    inline static std::filesystem::path _folder;
    inline static std::string _lastFile;
    inline static std::string _lastError;

    inline static ID3D12Resource* _readback = nullptr;
    inline static ID3D12Fence* _fence = nullptr;
    inline static UINT64 _fenceValue = 0;
    inline static bool _recorded = false;
    inline static UINT _width = 0;
    inline static UINT _height = 0;
    inline static UINT _rowPitch = 0;
    inline static UINT64 _totalSize = 0;
    inline static DXGI_FORMAT _format = DXGI_FORMAT_UNKNOWN;
    inline static uint64_t _requestTick = 0;
    inline static uint64_t _submitTick = 0;

    static UINT BytesPerPixel(DXGI_FORMAT format) { return OutputBytesPerPixel(format) != 0 ? 4 : 0; }

    static void ReleasePending()
    {
        if (_readback != nullptr)
        {
            _readback->Release();
            _readback = nullptr;
        }

        if (_fence != nullptr)
        {
            _fence->Release();
            _fence = nullptr;
        }

        _recorded = false;
    }

    static void Fail(const char* text)
    {
        {
            std::scoped_lock lock(_mutex);
            _lastError = text;
        }

        _state.store((int) State::Failed, std::memory_order_release);
    }

    // Only letters, digits, dot, dash and underscore go into a file name
    static std::string SafeName(const std::string& text)
    {
        std::string out;

        for (const char c : text)
        {
            const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                              c == '-' || c == '_';
            out += keep ? c : '_';
        }

        return out.empty() ? std::string("capture") : out.substr(0, 60);
    }

    static std::string JsonEscape(const std::string& text)
    {
        std::string out;

        for (const char c : text)
        {
            if (c == '"' || c == '\\')
            {
                out += '\\';
                out += c;
            }
            else if ((unsigned char) c >= 0x20)
            {
                out += c;
            }
        }

        return out;
    }

    static bool ReadAndSave(ID3D12Resource* readback, UINT width, UINT height, DXGI_FORMAT format, UINT rowPitch,
                            UINT64 totalSize, const Meta& meta, const std::filesystem::path& folder,
                            std::string* file, std::string* error)
    {
        std::vector<BYTE> pixels;

        {
            void* mapped = nullptr;
            // The last row has no padding, the buffer is smaller than row pitch times height
            const D3D12_RANGE range { 0, (SIZE_T) totalSize };

            if (readback->Map(0, &range, &mapped) != S_OK || mapped == nullptr)
            {
                *error = "Can't read the capture back from the GPU";
                return false;
            }

            const bool converted = ConvertPixels((const BYTE*) mapped, width, height, rowPitch, format, pixels);

            const D3D12_RANGE nothing { 0, 0 };
            readback->Unmap(0, &nothing);

            if (!converted)
            {
                *error = "The format of the game's image isn't supported for captures";
                return false;
            }
        }

        std::error_code ec;
        std::filesystem::create_directories(folder, ec);

        SYSTEMTIME time;
        GetLocalTime(&time);

        char stamp[32];
        snprintf(stamp, sizeof(stamp), "%04d%02d%02d_%02d%02d%02d", time.wYear, time.wMonth, time.wDay, time.wHour,
                 time.wMinute, time.wSecond);

        const std::string name = !meta.fsrVersion.empty() ? meta.fsrVersion : meta.upscaler;
        std::string base = std::string(stamp) + "_" + SafeName(name);

        // Existing captures are never overwritten
        for (int n = 2; std::filesystem::exists(folder / (base + ".png"), ec) && n < 100; n++)
            base = std::string(stamp) + "_" + SafeName(name) + "_" + std::to_string(n);

        const auto pngPath = folder / (base + ".png");
        const UINT outBytes = OutputBytesPerPixel(format);

        if (!WritePng(pngPath, pixels, width, height, outBytes, error))
            return false;

        char dateTime[32];
        snprintf(dateTime, sizeof(dateTime), "%04d-%02d-%02d %02d:%02d:%02d", time.wYear, time.wMonth, time.wDay,
                 time.wHour, time.wMinute, time.wSecond);

        std::ofstream json(folder / (base + ".json"), std::ios::binary);

        json << "{\n";
        json << "  \"id\": \"" << JsonEscape(base) << "\",\n";
        json << "  \"file\": \"" << JsonEscape(base + ".png") << "\",\n";
        json << "  \"date_time\": \"" << dateTime << "\",\n";
        json << "  \"game\": \"" << JsonEscape(meta.game) << "\",\n";
        json << "  \"width\": " << width << ",\n";
        json << "  \"height\": " << height << ",\n";
        json << "  \"bits_per_channel\": " << (outBytes == 6 ? 16 : 8) << ",\n";
        json << "  \"source_format\": " << (int) format << ",\n";
        json << "  \"hdr10_pq_values\": " << (outBytes == 6 ? "true" : "false") << ",\n";
        json << "  \"upscaler\": \"" << JsonEscape(meta.upscaler) << "\",\n";
        json << "  \"render_to_output\": \"" << JsonEscape(meta.resolution) << "\",\n";
        json << "  \"fsr_version\": \"" << JsonEscape(meta.fsrVersion) << "\",\n";
        json << "  \"dll_path\": \"" << JsonEscape(meta.dllPath) << "\",\n";
        json << "  \"dll_file_version\": \"" << JsonEscape(meta.dllFileVersion) << "\",\n";
        json << "  \"dll_sha256\": \"" << JsonEscape(meta.dllSha256) << "\",\n";
        json << "  \"frame_generation\": \"" << JsonEscape(meta.frameGeneration) << "\",\n";
        json << "  \"source\": \"swapchain back buffer before the ILION menu is drawn\"\n";
        json << "}\n";

        *file = pngPath.string();
        return true;
    }
};
