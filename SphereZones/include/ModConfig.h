#pragma once

#include <string>

class ModConfig {
public:
    static ModConfig& Get();
    void Load();

    static std::wstring GetModDirectory();

    static std::wstring GetConfigDirectory();

    bool Enabled = true;
    bool DebugLogging = false;

    std::string ZonesFile = "zones.json";

    bool ShowBlockMessage = true;
    int NotifyCooldownSeconds = 6;
    std::string DamageBlockMessage = "Damage blocked in {zone}";
    std::string StructureDamageBlockMessage = "Structure damage blocked in {zone}";
    std::string BuildBlockMessage = "Building blocked in {zone}";
    std::string DismantleBlockMessage = "Dismantling blocked in {zone}";
    std::string SignEditBlockMessage = "Sign editing blocked in {zone}";
    std::string GroundMountBlockMessage = "Mounts are not allowed in {zone}";
    std::string FlyingMountBlockMessage = "Flying mounts are not allowed in {zone}";
    std::string ZoneLockedMessage = "{zone} is locked";
    std::string ZoneLevelMessage = "{zone} requires level {level}";

private:
    ModConfig() = default;
};
