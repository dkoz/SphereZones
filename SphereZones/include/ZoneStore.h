#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Zones {

struct Permission {
    bool HasWorld = false;
    std::vector<std::string> World;
    bool HasDamage = false;
    std::vector<std::string> Damage;

    bool GovernsExtendedWorld = false;
};

bool IsExtendedWorldAction(const std::string& action);

struct NamedPermission {
    std::string Instance;
    Permission Perms;
};

struct Point {
    double X = 0.0;
    double Y = 0.0;
};

struct Zone {
    std::string Name;
    std::vector<Point> Points;

    double MinX = 0.0;
    double MaxX = 0.0;
    double MinY = 0.0;
    double MaxY = 0.0;

    std::vector<uint8_t> AddStatus;
    std::vector<NamedPermission> Permissions;
    bool HasPermissions = false;

    bool Locked = false;
    int32_t MinimumLevel = 0;

    bool HasEntryLock() const { return Locked || MinimumLevel > 0; }

    bool IsGlobal = false;

    const Permission* FindPermission(const std::string& instance) const;
};

class Store {
public:
    static Store& Get();

    bool Load();

    bool IsLoaded() const { return m_Loaded; }
    size_t ZoneCount() const { return m_Zones.size(); }

    const Zone& ZoneAt(double worldX, double worldY) const;

    static bool IsDamageAllowed(const Zone& zone, const std::string& attacker, const std::string& target);

    static bool IsWorldActionAllowed(const Zone& zone, const std::string& action);

private:
    Store() = default;

    std::vector<Zone> m_Zones;
    Zone m_Global;
    bool m_Loaded = false;
};

}
