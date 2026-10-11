#pragma once

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

// Visual comparison of the captures of ILION: the list of saved captures, loading them, the pictures the
// comparator shows and the measurements between a reference and another capture.
//
// Everything here runs on the CPU over 8 bit pictures, nothing of the game or of the upscaler is touched. The
// measurements compare two still pictures, they say how much a picture differs from the one picked as the
// reference. They say nothing about what happens in motion (ghosting, shimmering, stability over time).
class IlionVisual
{
  public:
    // 8 bit RGBA, top row first
    struct Image
    {
        int width = 0;
        int height = 0;
        bool from16Bit = false; // HDR10 capture, its PQ values were reduced to 8 bit
        std::vector<uint8_t> rgba;

        bool Empty() const { return width <= 0 || height <= 0 || rgba.empty(); }
    };

    enum class Origin
    {
        Unknown,  // a png without the json of ILION next to it
        Native,   // captured without upscaling (no upscaler, or render resolution equal to the output)
        Upscaled,
    };

    struct Capture
    {
        std::string id; // file name without extension
        std::filesystem::path path;
        bool hasMeta = false;
        int width = 0;
        int height = 0;
        bool hdr = false;
        std::string dateTime;
        std::string game;
        std::string upscaler;
        std::string resolution;
        std::string fsrVersion;
        std::string dllPath;
        std::string dllFileVersion;
        std::string dllSha256;
        std::string frameGeneration;

        std::string Name() const
        {
            if (!fsrVersion.empty())
                return fsrVersion;

            if (hasMeta && GetOrigin() == Origin::Native)
                return "Native";

            return hasMeta && !upscaler.empty() ? upscaler : id;
        }

        Origin GetOrigin() const
        {
            if (!hasMeta)
                return Origin::Unknown;

            unsigned renderWidth = 0, renderHeight = 0, outWidth = 0, outHeight = 0;

            if (sscanf_s(resolution.c_str(), "%ux%u -> %ux%u", &renderWidth, &renderHeight, &outWidth, &outHeight) != 4)
                return Origin::Native;

            return renderWidth == outWidth && renderHeight == outHeight ? Origin::Native : Origin::Upscaled;
        }
    };

    // What was measured between the reference and another capture
    struct Metrics
    {
        static constexpr int GridX = 6;
        static constexpr int GridY = 4;

        bool valid = false;
        bool sizeMismatch = false;
        bool depthMismatch = false; // one is HDR10 and the other isn't

        bool identical = false;
        double ssim = 0.0;             // mean structural similarity of the luma, 1 = same structure
        double psnr = 0.0;             // dB, of the luma
        double meanDifference = 0.0;   // mean absolute difference of the luma, in levels of 255
        double differentPercent = 0.0; // pixels with a channel more than 2 levels apart
        double clearlyPercent = 0.0;   // pixels with a channel more than 16 levels apart

        double edgeStrengthReference = 0.0; // mean luma gradient, levels per pixel
        double edgeStrengthTest = 0.0;
        double edgeChangePercent = 0.0;  // of the test against the reference
        bool edgesAvailable = false;     // the reference has edges to look at
        double edgesKeptPercent = 0.0;   // edges of the reference still at least half as strong
        double overshootPercent = 0.0;   // pixels next to an edge brighter or darker than everything around them
                                         // in the reference, by more than 4 levels
        double overshootMean = 0.0;      // levels, over the pixels next to an edge

        int shiftX = 0; // displacement that matches the two pictures best
        int shiftY = 0;
        bool shifted = false;
        double sceneCorrelation = 0.0;
        bool sceneMismatch = false;

        double regionDifference[GridY][GridX] = {};
        int worstRegionX = 0;
        int worstRegionY = 0;

        // Scene or camera aren't the same, the numbers above don't describe the upscaler
        bool Unreliable() const { return sceneMismatch || shifted; }
    };

    // Saved result of a measurement, for the conclusions of the benchmark comparison
    struct SavedResult
    {
        std::string dateTime;
        std::string game;
        std::string referenceId;
        std::string referenceName;
        std::string referenceSha256;
        std::string referenceOrigin;
        std::string testId;
        std::string testName;
        std::string testSha256;
        double ssim = 0.0;
        double meanDifference = 0.0;
        double differentPercent = 0.0;
        double edgeChangePercent = 0.0;
        double edgesKeptPercent = 0.0;
        double overshootPercent = 0.0;
        bool unreliable = false;
    };

    // Every png of the folder, oldest first. Those of ILION carry their json, any other png can be compared too.
    static std::vector<Capture> ListCaptures(const std::filesystem::path& folder)
    {
        std::vector<Capture> captures;
        std::error_code ec;

        for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
        {
            if (!entry.is_regular_file(ec))
                continue;

            auto extension = entry.path().extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);

            if (extension != L".png")
                continue;

            Capture capture;
            capture.path = entry.path();
            capture.id = Narrow(entry.path().stem().wstring());

            if (!ReadPngSize(capture.path, capture.width, capture.height))
                continue;

            auto jsonPath = entry.path();
            jsonPath.replace_extension(L".json");

            std::ifstream json(jsonPath, std::ios::binary);

            if (json.is_open())
            {
                std::stringstream buffer;
                buffer << json.rdbuf();
                const std::string text = buffer.str();

                capture.hasMeta = true;
                capture.dateTime = JsonText(text, "date_time");
                capture.game = JsonText(text, "game");
                capture.upscaler = JsonText(text, "upscaler");
                capture.resolution = JsonText(text, "render_to_output");
                capture.fsrVersion = JsonText(text, "fsr_version");
                capture.dllPath = JsonText(text, "dll_path");
                capture.dllFileVersion = JsonText(text, "dll_file_version");
                capture.dllSha256 = JsonText(text, "dll_sha256");
                capture.frameGeneration = JsonText(text, "frame_generation");
                capture.hdr = text.find("\"hdr10_pq_values\": true") != std::string::npos;
            }

            captures.push_back(std::move(capture));
        }

        std::sort(captures.begin(), captures.end(), [](const Capture& a, const Capture& b) { return a.id < b.id; });

        return captures;
    }

    // Removes the png and the json of the captures, returns how many were removed
    static int DeleteCaptures(const std::vector<Capture>& captures, const std::vector<std::string>& ids)
    {
        int removed = 0;

        for (const auto& capture : captures)
        {
            if (std::find(ids.begin(), ids.end(), capture.id) == ids.end())
                continue;

            std::error_code ec;

            if (std::filesystem::remove(capture.path, ec))
                removed++;

            auto jsonPath = capture.path;
            jsonPath.replace_extension(L".json");
            std::filesystem::remove(jsonPath, ec);
        }

        return removed;
    }

    // Decodes a png into 8 bit RGBA through the imaging component of Windows. Not for the render thread.
    static bool LoadPng(const std::filesystem::path& path, Image& image)
    {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool comOwned = comResult == S_OK || comResult == S_FALSE;

        IWICImagingFactory* factory = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;

        UINT width = 0, height = 0;
        WICPixelFormatGUID sourceFormat = {};

        bool ok = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)) == S_OK &&
                  factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                     &decoder) == S_OK &&
                  decoder->GetFrame(0, &frame) == S_OK && frame->GetSize(&width, &height) == S_OK &&
                  frame->GetPixelFormat(&sourceFormat) == S_OK && factory->CreateFormatConverter(&converter) == S_OK &&
                  converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom) == S_OK;

        // A picture this big isn't a capture of a game
        ok = ok && width > 0 && height > 0 && width <= 16384 && height <= 16384;

        if (ok)
        {
            image.width = (int) width;
            image.height = (int) height;
            image.from16Bit = IsEqualGUID(sourceFormat, GUID_WICPixelFormat48bppRGB) ||
                              IsEqualGUID(sourceFormat, GUID_WICPixelFormat64bppRGBA);
            image.rgba.resize((size_t) width * height * 4);

            ok = converter->CopyPixels(nullptr, width * 4, (UINT) image.rgba.size(), image.rgba.data()) == S_OK;
        }

        if (converter != nullptr)
            converter->Release();

        if (frame != nullptr)
            frame->Release();

        if (decoder != nullptr)
            decoder->Release();

        if (factory != nullptr)
            factory->Release();

        if (comOwned)
            CoUninitialize();

        if (!ok)
            image = Image();

        return ok;
    }

    // --- Pictures of the comparator -------------------------------------------------------------------------

    // Draws the part of the image around (centerX, centerY) into an RGBA buffer of outWidth x outHeight. A zoom
    // of 1 is one pixel of the image per pixel drawn; above it the pixels are repeated as they are, never
    // smoothed, below it they are averaged.
    static void RenderView(const Image& image, double centerX, double centerY, double zoom, int outWidth, int outHeight,
                           uint8_t* out)
    {
        Rasterize(image.width, image.height, centerX, centerY, zoom, outWidth, outHeight, out,
                  [&](int x, int y, int rgb[3])
                  {
                      const uint8_t* pixel = image.rgba.data() + ((size_t) y * image.width + x) * 4;
                      rgb[0] = pixel[0];
                      rgb[1] = pixel[1];
                      rgb[2] = pixel[2];
                  });
    }

    // The same view of the absolute difference between two images of the same size, multiplied by gain
    static void RenderDifference(const Image& a, const Image& b, double centerX, double centerY, double zoom, int gain,
                                 int outWidth, int outHeight, uint8_t* out)
    {
        if (a.width != b.width || a.height != b.height)
        {
            Rasterize(0, 0, centerX, centerY, zoom, outWidth, outHeight, out, [](int, int, int*) {});
            return;
        }

        Rasterize(a.width, a.height, centerX, centerY, zoom, outWidth, outHeight, out,
                  [&](int x, int y, int rgb[3])
                  {
                      const size_t offset = ((size_t) y * a.width + x) * 4;

                      for (int c = 0; c < 3; c++)
                          rgb[c] = (std::min)(255, std::abs((int) a.rgba[offset + c] - (int) b.rgba[offset + c]) * gain);
                  });
    }

    // --- Measurements ---------------------------------------------------------------------------------------

    static Metrics Compare(const Image& reference, const Image& test)
    {
        Metrics m;

        if (reference.Empty() || test.Empty())
            return m;

        if (reference.width != test.width || reference.height != test.height)
        {
            m.sizeMismatch = true;
            return m;
        }

        if (reference.from16Bit != test.from16Bit)
        {
            m.depthMismatch = true;
            return m;
        }

        const int width = reference.width;
        const int height = reference.height;

        // Too small for the windows used below
        if (width < 64 || height < 64)
            return m;

        const size_t count = (size_t) width * height;

        std::vector<uint8_t> lumaReference(count), lumaTest(count);

        // Luma and the differences of the pixels as they are
        {
            const double cellWidth = (double) width / Metrics::GridX;
            const double cellHeight = (double) height / Metrics::GridY;

            double regionSum[Metrics::GridY][Metrics::GridX] = {};
            double regionCount[Metrics::GridY][Metrics::GridX] = {};
            double sumAbs = 0.0, sumSquare = 0.0;
            size_t different = 0, clearly = 0;

            for (int y = 0; y < height; y++)
            {
                const uint8_t* rowReference = reference.rgba.data() + (size_t) y * width * 4;
                const uint8_t* rowTest = test.rgba.data() + (size_t) y * width * 4;
                const int cellY = (std::min)(Metrics::GridY - 1, (int) (y / cellHeight));

                for (int x = 0; x < width; x++)
                {
                    const uint8_t* a = rowReference + x * 4;
                    const uint8_t* b = rowTest + x * 4;

                    const uint8_t lumaA = Luma(a);
                    const uint8_t lumaB = Luma(b);

                    lumaReference[(size_t) y * width + x] = lumaA;
                    lumaTest[(size_t) y * width + x] = lumaB;

                    const int channel = (std::max)({ std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2]) });

                    if (channel > 2)
                        different++;

                    if (channel > 16)
                        clearly++;

                    const int difference = std::abs((int) lumaA - (int) lumaB);
                    sumAbs += difference;
                    sumSquare += (double) difference * difference;

                    const int cellX = (std::min)(Metrics::GridX - 1, (int) (x / cellWidth));
                    regionSum[cellY][cellX] += difference;
                    regionCount[cellY][cellX] += 1.0;
                }
            }

            m.meanDifference = sumAbs / count;
            m.differentPercent = 100.0 * different / count;
            m.clearlyPercent = 100.0 * clearly / count;
            m.identical = different == 0 && sumAbs == 0.0;

            const double mse = sumSquare / count;
            m.psnr = mse > 0.0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : 0.0;

            double worst = -1.0;

            for (int y = 0; y < Metrics::GridY; y++)
            {
                for (int x = 0; x < Metrics::GridX; x++)
                {
                    m.regionDifference[y][x] = regionCount[y][x] > 0.0 ? regionSum[y][x] / regionCount[y][x] : 0.0;

                    if (m.regionDifference[y][x] > worst)
                    {
                        worst = m.regionDifference[y][x];
                        m.worstRegionX = x;
                        m.worstRegionY = y;
                    }
                }
            }
        }

        m.ssim = Ssim(lumaReference, lumaTest, width, height);

        MeasureEdges(lumaReference, lumaTest, width, height, m);
        MeasureOvershoot(lumaReference, lumaTest, width, height, m);
        MeasureAlignment(lumaReference, lumaTest, width, height, m);

        m.valid = true;
        return m;
    }

    // --- Saved results ----------------------------------------------------------------------------------------

    static constexpr const char* ResultsHeader =
        "date_time,game,reference_id,reference_name,reference_sha256,reference_origin,test_id,test_name,test_sha256,"
        "ssim,mean_difference,different_percent,edge_change_percent,edges_kept_percent,overshoot_percent,unreliable";

    // The measurement of a pair replaces the one that was saved before for the same two captures
    static bool SaveResult(const std::filesystem::path& folder, const SavedResult& result)
    {
        auto results = LoadResults(folder);

        std::erase_if(results, [&](const SavedResult& r)
                      { return r.referenceId == result.referenceId && r.testId == result.testId; });

        results.push_back(result);

        return WriteResults(folder, results);
    }

    static std::vector<SavedResult> LoadResults(const std::filesystem::path& folder)
    {
        std::vector<SavedResult> results;
        std::ifstream file(folder / L"visual_results.csv", std::ios::binary);
        std::string line;

        if (!file.is_open() || !std::getline(file, line))
            return results;

        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            const auto fields = SplitCsv(line);

            if (fields.size() < 16)
                continue;

            SavedResult r;
            r.dateTime = fields[0];
            r.game = fields[1];
            r.referenceId = fields[2];
            r.referenceName = fields[3];
            r.referenceSha256 = fields[4];
            r.referenceOrigin = fields[5];
            r.testId = fields[6];
            r.testName = fields[7];
            r.testSha256 = fields[8];
            r.ssim = atof(fields[9].c_str());
            r.meanDifference = atof(fields[10].c_str());
            r.differentPercent = atof(fields[11].c_str());
            r.edgeChangePercent = atof(fields[12].c_str());
            r.edgesKeptPercent = atof(fields[13].c_str());
            r.overshootPercent = atof(fields[14].c_str());
            r.unreliable = fields[15] == "1";
            results.push_back(std::move(r));
        }

        return results;
    }

    // Results of captures that were deleted go with them
    static void ForgetResults(const std::filesystem::path& folder, const std::vector<std::string>& captureIds)
    {
        auto results = LoadResults(folder);
        const size_t before = results.size();

        std::erase_if(results, [&](const SavedResult& r)
                      {
                          return std::find(captureIds.begin(), captureIds.end(), r.referenceId) != captureIds.end() ||
                                 std::find(captureIds.begin(), captureIds.end(), r.testId) != captureIds.end();
                      });

        if (results.size() != before)
            WriteResults(folder, results);
    }

    static const char* OriginText(Origin origin)
    {
        return origin == Origin::Native ? "native" : origin == Origin::Upscaled ? "upscaled" : "unknown";
    }

  private:
    static std::string Narrow(const std::wstring& text)
    {
        if (text.empty())
            return {};

        const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int) text.size(), nullptr, 0, nullptr, nullptr);
        std::string out(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int) text.size(), out.data(), size, nullptr, nullptr);
        return out;
    }

    // Width and height from the header of the file, without decoding it
    static bool ReadPngSize(const std::filesystem::path& path, int& width, int& height)
    {
        std::ifstream file(path, std::ios::binary);
        unsigned char header[24] = {};

        if (!file.read((char*) header, sizeof(header)))
            return false;

        static const unsigned char signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

        if (memcmp(header, signature, 8) != 0 || memcmp(header + 12, "IHDR", 4) != 0)
            return false;

        width = (header[16] << 24) | (header[17] << 16) | (header[18] << 8) | header[19];
        height = (header[20] << 24) | (header[21] << 16) | (header[22] << 8) | header[23];

        return width > 0 && height > 0;
    }

    // Value of a text field of the flat json the capture writes
    static std::string JsonText(const std::string& json, const char* key)
    {
        const std::string pattern = std::string("\"") + key + "\"";
        size_t at = json.find(pattern);

        if (at == std::string::npos)
            return {};

        at = json.find(':', at + pattern.size());

        if (at == std::string::npos)
            return {};

        at = json.find_first_not_of(" \t\r\n", at + 1);

        if (at == std::string::npos || json[at] != '"')
            return {};

        std::string out;

        for (size_t i = at + 1; i < json.size(); i++)
        {
            if (json[i] == '\\' && i + 1 < json.size())
                out += json[++i];
            else if (json[i] == '"')
                break;
            else
                out += json[i];
        }

        return out;
    }

    static std::string CsvField(const std::string& text)
    {
        std::string out = "\"";

        for (const char c : text)
        {
            if (c == '"')
                out += "\"\"";
            else if (c != '\r' && c != '\n')
                out += c;
        }

        return out + "\"";
    }

    static std::vector<std::string> SplitCsv(const std::string& line)
    {
        std::vector<std::string> fields;
        std::string field;
        bool quoted = false;

        for (size_t i = 0; i < line.size(); i++)
        {
            const char c = line[i];

            if (quoted)
            {
                if (c == '"' && i + 1 < line.size() && line[i + 1] == '"')
                {
                    field += '"';
                    i++;
                }
                else if (c == '"')
                {
                    quoted = false;
                }
                else
                {
                    field += c;
                }
            }
            else if (c == '"')
            {
                quoted = true;
            }
            else if (c == ',')
            {
                fields.push_back(field);
                field.clear();
            }
            else
            {
                field += c;
            }
        }

        fields.push_back(field);
        return fields;
    }

    static bool WriteResults(const std::filesystem::path& folder, const std::vector<SavedResult>& results)
    {
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);

        std::ofstream file(folder / L"visual_results.csv", std::ios::binary | std::ios::trunc);

        if (!file.is_open())
            return false;

        file << ResultsHeader << "\n";

        for (const auto& r : results)
        {
            char numbers[160];
            snprintf(numbers, sizeof(numbers), "%.5f,%.3f,%.3f,%.2f,%.2f,%.3f,%d", r.ssim, r.meanDifference,
                     r.differentPercent, r.edgeChangePercent, r.edgesKeptPercent, r.overshootPercent, r.unreliable ? 1 : 0);

            file << CsvField(r.dateTime) << ',' << CsvField(r.game) << ',' << CsvField(r.referenceId) << ','
                 << CsvField(r.referenceName) << ',' << CsvField(r.referenceSha256) << ',' << CsvField(r.referenceOrigin)
                 << ',' << CsvField(r.testId) << ',' << CsvField(r.testName) << ',' << CsvField(r.testSha256) << ','
                 << numbers << "\n";
        }

        return file.good();
    }

    // Rec. 709 weights over the values as they are stored
    static uint8_t Luma(const uint8_t* rgb)
    {
        return (uint8_t) ((rgb[0] * 2126 + rgb[1] * 7152 + rgb[2] * 722 + 5000) / 10000);
    }

    template <typename PixelAt>
    static void Rasterize(int sourceWidth, int sourceHeight, double centerX, double centerY, double zoom, int outWidth,
                          int outHeight, uint8_t* out, PixelAt pixelAt)
    {
        static constexpr uint8_t background[3] = { 18, 18, 20 };

        if (zoom <= 0.0)
            zoom = 1.0;

        // Samples per axis under each drawn pixel when the image is shown smaller than it is
        const int samples = zoom >= 1.0 ? 1 : (std::min)(4, (int) std::ceil(1.0 / zoom));

        // Column of the image under every sample of every drawn column, -1 outside of it
        std::vector<int> columns((size_t) outWidth * samples);

        for (int ox = 0; ox < outWidth; ox++)
        {
            for (int s = 0; s < samples; s++)
            {
                const double sx = centerX + (ox + (s + 0.5) / samples - outWidth * 0.5) / zoom;
                const int x = (int) std::floor(sx);
                columns[(size_t) ox * samples + s] = x >= 0 && x < sourceWidth ? x : -1;
            }
        }

        for (int oy = 0; oy < outHeight; oy++)
        {
            uint8_t* row = out + (size_t) oy * outWidth * 4;

            int rows[4];

            for (int s = 0; s < samples; s++)
            {
                const double sy = centerY + (oy + (s + 0.5) / samples - outHeight * 0.5) / zoom;
                const int y = (int) std::floor(sy);
                rows[s] = y >= 0 && y < sourceHeight ? y : -1;
            }

            for (int ox = 0; ox < outWidth; ox++)
            {
                int sum[3] = { 0, 0, 0 };
                int used = 0;

                for (int sy = 0; sy < samples; sy++)
                {
                    if (rows[sy] < 0)
                        continue;

                    for (int sx = 0; sx < samples; sx++)
                    {
                        const int x = columns[(size_t) ox * samples + sx];

                        if (x < 0)
                            continue;

                        int rgb[3];
                        pixelAt(x, rows[sy], rgb);
                        sum[0] += rgb[0];
                        sum[1] += rgb[1];
                        sum[2] += rgb[2];
                        used++;
                    }
                }

                uint8_t* pixel = row + ox * 4;

                if (used == 0)
                {
                    pixel[0] = background[0];
                    pixel[1] = background[1];
                    pixel[2] = background[2];
                }
                else
                {
                    pixel[0] = (uint8_t) (sum[0] / used);
                    pixel[1] = (uint8_t) (sum[1] / used);
                    pixel[2] = (uint8_t) (sum[2] / used);
                }

                pixel[3] = 255;
            }
        }
    }

    // Mean SSIM of the luma over 8x8 windows every 4 pixels, with the constants of the original definition
    static double Ssim(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int width, int height)
    {
        const double c1 = (0.01 * 255.0) * (0.01 * 255.0);
        const double c2 = (0.03 * 255.0) * (0.03 * 255.0);

        double total = 0.0;
        size_t windows = 0;

        for (int y = 0; y + 8 <= height; y += 4)
        {
            for (int x = 0; x + 8 <= width; x += 4)
            {
                unsigned sumA = 0, sumB = 0, sumAA = 0, sumBB = 0, sumAB = 0;

                for (int j = 0; j < 8; j++)
                {
                    const uint8_t* rowA = a.data() + (size_t) (y + j) * width + x;
                    const uint8_t* rowB = b.data() + (size_t) (y + j) * width + x;

                    for (int i = 0; i < 8; i++)
                    {
                        const unsigned va = rowA[i], vb = rowB[i];
                        sumA += va;
                        sumB += vb;
                        sumAA += va * va;
                        sumBB += vb * vb;
                        sumAB += va * vb;
                    }
                }

                const double meanA = sumA / 64.0;
                const double meanB = sumB / 64.0;
                const double varianceA = sumAA / 64.0 - meanA * meanA;
                const double varianceB = sumBB / 64.0 - meanB * meanB;
                const double covariance = sumAB / 64.0 - meanA * meanB;

                total += ((2.0 * meanA * meanB + c1) * (2.0 * covariance + c2)) /
                         ((meanA * meanA + meanB * meanB + c1) * (varianceA + varianceB + c2));
                windows++;
            }
        }

        return windows > 0 ? total / windows : 0.0;
    }

    // Sobel gradient of the luma, 4 units are one level of difference per pixel across a straight edge
    static int Gradient(const std::vector<uint8_t>& luma, int width, int x, int y)
    {
        const uint8_t* above = luma.data() + (size_t) (y - 1) * width + x;
        const uint8_t* row = above + width;
        const uint8_t* below = row + width;

        const int gx = (above[1] + 2 * row[1] + below[1]) - (above[-1] + 2 * row[-1] + below[-1]);
        const int gy = (below[-1] + 2 * below[0] + below[1]) - (above[-1] + 2 * above[0] + above[1]);

        return (int) std::sqrt((double) (gx * gx + gy * gy));
    }

    static void MeasureEdges(const std::vector<uint8_t>& reference, const std::vector<uint8_t>& test, int width,
                             int height, Metrics& m)
    {
        // An edge is a step of about 25 levels or more
        constexpr int edgeThreshold = 100;

        double sumReference = 0.0, sumTest = 0.0;
        size_t pixels = 0, edgesReference = 0, kept = 0;

        for (int y = 1; y < height - 1; y++)
        {
            for (int x = 1; x < width - 1; x++)
            {
                const int gradientReference = Gradient(reference, width, x, y);
                const int gradientTest = Gradient(test, width, x, y);

                sumReference += gradientReference;
                sumTest += gradientTest;
                pixels++;

                if (gradientReference >= edgeThreshold)
                {
                    edgesReference++;

                    if (gradientTest * 2 >= gradientReference)
                        kept++;
                }
            }
        }

        m.edgeStrengthReference = sumReference / pixels / 4.0;
        m.edgeStrengthTest = sumTest / pixels / 4.0;
        m.edgeChangePercent =
            m.edgeStrengthReference > 0.0 ? (m.edgeStrengthTest / m.edgeStrengthReference - 1.0) * 100.0 : 0.0;

        // With hardly any edge in the reference the percentages below mean nothing
        m.edgesAvailable = edgesReference >= pixels / 2000;

        if (m.edgesAvailable)
            m.edgesKeptPercent = 100.0 * kept / edgesReference;
    }

    // Pixels of the test that go beyond everything the reference has in the 5x5 pixels around them, looked at
    // only where the reference has an edge inside those 5x5. That is what a halo or the ringing of sharpening
    // looks like in numbers, but texture detail the reference lacks gives the same reading.
    static void MeasureOvershoot(const std::vector<uint8_t>& reference, const std::vector<uint8_t>& test, int width,
                                 int height, Metrics& m)
    {
        size_t nearEdge = 0, over = 0;
        double sum = 0.0;

        // Every second pixel in both directions is plenty for a percentage
        for (int y = 2; y < height - 2; y += 2)
        {
            for (int x = 2; x < width - 2; x += 2)
            {
                int lowest = 255, highest = 0;

                for (int j = -2; j <= 2; j++)
                {
                    const uint8_t* row = reference.data() + (size_t) (y + j) * width + x;

                    for (int i = -2; i <= 2; i++)
                    {
                        lowest = (std::min)(lowest, (int) row[i]);
                        highest = (std::max)(highest, (int) row[i]);
                    }
                }

                if (highest - lowest < 25)
                    continue;

                nearEdge++;

                const int value = test[(size_t) y * width + x];
                const int beyond = (std::max)(0, value - highest) + (std::max)(0, lowest - value);

                sum += beyond;

                if (beyond > 4)
                    over++;
            }
        }

        if (nearEdge > 0)
        {
            m.overshootPercent = 100.0 * over / nearEdge;
            m.overshootMean = sum / nearEdge;
        }
    }

    // Mean absolute difference with the test displaced by (dx, dy), over a grid of pixels of the middle part
    static double ShiftedDifference(const std::vector<uint8_t>& reference, const std::vector<uint8_t>& test, int width,
                                    int height, int dx, int dy, int margin)
    {
        double sum = 0.0;
        size_t samples = 0;

        for (int y = margin; y < height - margin; y += 8)
        {
            const uint8_t* rowReference = reference.data() + (size_t) y * width;
            const uint8_t* rowTest = test.data() + (size_t) (y + dy) * width;

            for (int x = margin; x < width - margin; x += 8)
            {
                sum += std::abs((int) rowReference[x] - (int) rowTest[x + dx]);
                samples++;
            }
        }

        return samples > 0 ? sum / samples : 0.0;
    }

    // Whether both pictures show the same scene from the same place
    static void MeasureAlignment(const std::vector<uint8_t>& reference, const std::vector<uint8_t>& test, int width,
                                 int height, Metrics& m)
    {
        // Correlation of the pictures reduced to blocks of 16x16 pixels: what is where, without the fine detail
        {
            const int blocksX = width / 16;
            const int blocksY = height / 16;

            double sumA = 0.0, sumB = 0.0, sumAA = 0.0, sumBB = 0.0, sumAB = 0.0;

            for (int by = 0; by < blocksY; by++)
            {
                for (int bx = 0; bx < blocksX; bx++)
                {
                    unsigned blockA = 0, blockB = 0;

                    for (int j = 0; j < 16; j++)
                    {
                        const size_t offset = (size_t) (by * 16 + j) * width + bx * 16;

                        for (int i = 0; i < 16; i++)
                        {
                            blockA += reference[offset + i];
                            blockB += test[offset + i];
                        }
                    }

                    const double a = blockA / 256.0;
                    const double b = blockB / 256.0;

                    sumA += a;
                    sumB += b;
                    sumAA += a * a;
                    sumBB += b * b;
                    sumAB += a * b;
                }
            }

            const double n = (double) blocksX * blocksY;
            const double varianceA = sumAA / n - (sumA / n) * (sumA / n);
            const double varianceB = sumBB / n - (sumB / n) * (sumB / n);
            const double covariance = sumAB / n - (sumA / n) * (sumB / n);

            // A flat picture (a loading screen) has nothing to correlate
            m.sceneCorrelation =
                varianceA > 1.0 && varianceB > 1.0 ? covariance / std::sqrt(varianceA * varianceB) : (m.identical ? 1.0 : 0.0);
            m.sceneMismatch = m.sceneCorrelation < 0.90;
        }

        // Displacement of up to 27 pixels that matches best: first every 4 pixels, then pixel by pixel around it
        {
            const int margin = 32;

            if (width <= margin * 4 || height <= margin * 4)
                return;

            const double still = ShiftedDifference(reference, test, width, height, 0, 0, margin);

            double best = still;
            int bestX = 0, bestY = 0;

            for (int dy = -24; dy <= 24; dy += 4)
            {
                for (int dx = -24; dx <= 24; dx += 4)
                {
                    const double difference = ShiftedDifference(reference, test, width, height, dx, dy, margin);

                    if (difference < best)
                    {
                        best = difference;
                        bestX = dx;
                        bestY = dy;
                    }
                }
            }

            const int coarseX = bestX, coarseY = bestY;

            for (int dy = coarseY - 3; dy <= coarseY + 3; dy++)
            {
                for (int dx = coarseX - 3; dx <= coarseX + 3; dx++)
                {
                    const double difference = ShiftedDifference(reference, test, width, height, dx, dy, margin);

                    if (difference < best)
                    {
                        best = difference;
                        bestX = dx;
                        bestY = dy;
                    }
                }
            }

            m.shiftX = bestX;
            m.shiftY = bestY;

            // Only when moving the picture makes it clearly more alike, a still scene also has pixels that fit
            // a little better somewhere else
            m.shifted = (bestX != 0 || bestY != 0) && best < still * 0.80;
        }
    }
};
