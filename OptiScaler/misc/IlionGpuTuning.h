#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace IlionGpuTuning
{
enum Field { CoreMin, CoreMax, Voltage, Power, Memory, FieldCount };
inline constexpr std::array<const char*, FieldCount> fieldKeys{"CoreMin", "CoreMax", "Voltage", "PowerLimit", "VRAMMax"};

struct Range
{
    int min = 0, max = 0, step = 0;
    bool contains(int value) const;
};
struct Point
{
    int temperature = 0, speed = 0;
    bool operator==(const Point&) const = default;
};
struct Values
{
    std::array<std::optional<int>, FieldCount> fields;
    std::vector<Point> curve;
    std::optional<bool> zero;
    bool operator==(const Values&) const = default;
};
struct Device
{
    int id;
    std::string name, key, label;
};
enum class ProfileKind { Fans, Undervolt, Complete };
struct ProfileInfo { std::string name; ProfileKind kind; };
struct Snapshot
{
    std::vector<Device> devices;
    std::vector<ProfileInfo> profiles;
    std::string key, error;
    Values live, defaults;
    std::array<Range, FieldCount> ranges;
    Range temperature, speed;
    std::array<std::optional<double>, 8> metrics;
    std::optional<bool> factory;
    bool canReset = false, boardPower = false, fanEditable = false, autoTuning = false;
    bool canRestoreCurve = false;
    std::vector<Point> checkedCurve;
    std::string curveError;
};
enum class Action { Read, Apply, Restore, RestoreCurve, Save, Load, Rename, Delete };
struct Request
{
    Action action;
    std::string key, profile;
    Values expected, draft;
    std::optional<bool> atFactory;
    ProfileKind kind = ProfileKind::Complete;
    bool overwrite = false, customFan = false;
    std::string newName;
};
struct Result
{
    Action action;
    Snapshot snapshot;
    std::string message;
    std::optional<Values> loaded;
    std::optional<bool> loadedCustomFan;
    ProfileKind loadedKind = ProfileKind::Complete;
};
const char* ValidateDraft(const Snapshot& before, const Values& draft);
std::optional<std::array<Field, FieldCount>> WriteOrder(const Values& current, const Values& desired);
bool Start(Request request);
std::shared_ptr<const Result> Latest();
}

