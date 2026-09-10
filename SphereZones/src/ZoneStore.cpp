#include "ZoneStore.h"

#include "ModConfig.h"

#include <DynamicOutput/DynamicOutput.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

#include "vendor/json.hpp"

using namespace RC;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Zones {

const Permission* Zone::FindPermission(const std::string& instance) const {
    for (const auto& entry : Permissions) {
        if (entry.Instance == instance) return &entry.Perms;
    }
    return nullptr;
}

Store& Store::Get() {
    static Store instance;
    return instance;
}

static bool ContainsValue(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

static const char* const EXTENDED_WORLD_ACTIONS[] = {"EditSign", "GroundMount", "FlyingMount"};

bool IsExtendedWorldAction(const std::string& action) {
    for (const char* name : EXTENDED_WORLD_ACTIONS) {
        if (action == name) return true;
    }
    return false;
}

static void ParsePermissions(const json& node, std::vector<NamedPermission>& out, bool& hasAny,
                             bool currentFormat) {
    hasAny = false;
    if (!node.is_object()) return;

    for (auto it = node.begin(); it != node.end(); ++it) {
        if (!it.value().is_object()) continue;

        NamedPermission entry;
        entry.Instance = it.key();

        auto world = it.value().find("world");
        if (world != it.value().end() && world->is_array()) {
            entry.Perms.HasWorld = true;
            entry.Perms.GovernsExtendedWorld = currentFormat;
            for (const auto& value : *world) {
                if (!value.is_string()) continue;
                auto name = value.get<std::string>();
                if (IsExtendedWorldAction(name)) entry.Perms.GovernsExtendedWorld = true;
                entry.Perms.World.push_back(std::move(name));
            }
        }

        auto damage = it.value().find("damage");
        if (damage != it.value().end() && damage->is_array()) {
            entry.Perms.HasDamage = true;
            for (const auto& value : *damage) {
                if (value.is_string()) entry.Perms.Damage.push_back(value.get<std::string>());
            }
        }

        out.push_back(std::move(entry));
    }

    hasAny = !out.empty();
}

static void ComputeBounds(Zone& zone) {
    zone.MinX = std::numeric_limits<double>::max();
    zone.MaxX = std::numeric_limits<double>::lowest();
    zone.MinY = std::numeric_limits<double>::max();
    zone.MaxY = std::numeric_limits<double>::lowest();

    for (const auto& point : zone.Points) {
        zone.MinX = std::min(zone.MinX, point.X);
        zone.MaxX = std::max(zone.MaxX, point.X);
        zone.MinY = std::min(zone.MinY, point.Y);
        zone.MaxY = std::max(zone.MaxY, point.Y);
    }
}

static bool InsidePolygon(const Zone& zone, double x, double y) {
    const auto& points = zone.Points;
    const size_t count = points.size();
    if (count < 3) return false;

    if (x < zone.MinX || x > zone.MaxX || y < zone.MinY || y > zone.MaxY) return false;

    bool oddNodes = false;
    size_t j = count - 1;

    for (size_t i = 0; i < count; ++i) {
        const Point& pi = points[i];
        const Point& pj = points[j];

        if ((pi.Y < y && pj.Y >= y) || (pj.Y < y && pi.Y >= y)) {
            if (pi.X + (y - pi.Y) / (pj.Y - pi.Y) * (pj.X - pi.X) < x) {
                oddNodes = !oddNodes;
            }
        }
        j = i;
    }

    return oddNodes;
}

bool Store::Load() {
    m_Zones.clear();
    m_Global = Zone{};
    m_Global.Name = "Global";
    m_Global.IsGlobal = true;
    m_Loaded = false;

    auto path = fs::path(ModConfig::GetConfigDirectory()) / ModConfig::Get().ZonesFile;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        Output::send(STR("[SphereZones] Could not open zones file: {}\n"), path.wstring());
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    json root;
    try {
        root = json::parse(buffer.str(), nullptr, true, true);
    } catch (const std::exception& error) {
        std::string what = error.what() ? error.what() : "unknown";
        Output::send(STR("[SphereZones] Failed to parse zones file: {}\n"), std::wstring(what.begin(), what.end()));
        return false;
    }

    if (!root.is_object()) {
        Output::send(STR("[SphereZones] Zones file root is not an object\n"));
        return false;
    }

    int fileVersion = 1;
    auto version = root.find("version");
    if (version != root.end() && version->is_number_integer()) {
        fileVersion = version->get<int>();
    }
    const bool currentFormat = fileVersion >= 2;

    auto global = root.find("global");
    if (global != root.end() && global->is_object()) {
        auto perms = global->find("permissions");
        if (perms != global->end()) {
            ParsePermissions(*perms, m_Global.Permissions, m_Global.HasPermissions, currentFormat);
        }
    }

    auto zones = root.find("zones");
    if (zones != root.end() && zones->is_array()) {
        int index = 0;
        for (const auto& node : *zones) {
            ++index;
            if (!node.is_object()) continue;

            Zone zone;

            auto name = node.find("name");
            zone.Name = (name != node.end() && name->is_string())
                            ? name->get<std::string>()
                            : ("Zone " + std::to_string(index));

            auto points = node.find("points");
            if (points != node.end() && points->is_array()) {
                for (const auto& pointNode : *points) {
                    if (!pointNode.is_object()) continue;
                    auto x = pointNode.find("x");
                    auto y = pointNode.find("y");
                    if (x == pointNode.end() || y == pointNode.end()) continue;
                    if (!x->is_number() || !y->is_number()) continue;
                    zone.Points.push_back({x->get<double>(), y->get<double>()});
                }
            }

            if (zone.Points.size() < 3) {
                Output::send(STR("[SphereZones] Skipping zone {} with fewer than 3 points\n"), index);
                continue;
            }

            ComputeBounds(zone);

            auto addStatus = node.find("addStatus");
            if (addStatus != node.end() && addStatus->is_array()) {
                for (const auto& statusNode : *addStatus) {
                    if (!statusNode.is_number_integer()) continue;
                    auto value = statusNode.get<int64_t>();

                    if (value < 0 || value > 255) continue;
                    zone.AddStatus.push_back(static_cast<uint8_t>(value));
                }
            }

            auto locked = node.find("locked");
            if (locked != node.end() && locked->is_boolean()) {
                zone.Locked = locked->get<bool>();
            }

            auto minLevel = node.find("minimumLevel");
            if (minLevel != node.end() && minLevel->is_number_integer()) {
                auto value = minLevel->get<int64_t>();
                zone.MinimumLevel = (value > 0 && value < 1000) ? static_cast<int32_t>(value) : 0;
            }

            auto perms = node.find("permissions");
            if (perms != node.end()) {
                ParsePermissions(*perms, zone.Permissions, zone.HasPermissions, currentFormat);
            }

            m_Zones.push_back(std::move(zone));
        }
    }

    m_Loaded = true;

    Output::send(STR("[SphereZones] Loaded {} zone(s) from {} (format v{})\n"),
                 m_Zones.size(), path.filename().wstring(), fileVersion);

    if (!currentFormat) {
        Output::send(STR("[SphereZones] Zone file predates sign and mount permissions; those stay allowed everywhere until it is re-exported from the map tool\n"));
    }

    if (ModConfig::Get().DebugLogging) {
        for (const auto& zone : m_Zones) {
            Output::send(STR("[SphereZones]   '{}' {} points, bounds X[{:.0f}..{:.0f}] Y[{:.0f}..{:.0f}], {} status, locked={}, minLevel={}\n"),
                         std::wstring(zone.Name.begin(), zone.Name.end()),
                         zone.Points.size(), zone.MinX, zone.MaxX, zone.MinY, zone.MaxY,
                         zone.AddStatus.size(), zone.Locked, zone.MinimumLevel);
        }
    }

    return true;
}

const Zone& Store::ZoneAt(double worldX, double worldY) const {
    for (const auto& zone : m_Zones) {
        if (InsidePolygon(zone, worldX, worldY)) return zone;
    }
    return m_Global;
}

bool Store::IsDamageAllowed(const Zone& zone, const std::string& attacker, const std::string& target) {
    if (!zone.HasPermissions) return true;

    const Permission* perms = zone.FindPermission(attacker);
    if (!perms) return true;
    if (!perms->HasDamage) return true;

    return ContainsValue(perms->Damage, target);
}

bool Store::IsWorldActionAllowed(const Zone& zone, const std::string& action) {
    if (!zone.HasPermissions) return true;

    const Permission* perms = zone.FindPermission("Player");
    if (!perms) return true;
    if (!perms->HasWorld) return true;

    if (IsExtendedWorldAction(action) && !perms->GovernsExtendedWorld) return true;

    return ContainsValue(perms->World, action);
}

}
