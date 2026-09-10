#include "ModConfig.h"

#include <UE4SSProgram.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace RC;
namespace fs = std::filesystem;

ModConfig& ModConfig::Get() {
    static ModConfig instance;
    return instance;
}

std::wstring ModConfig::GetModDirectory() {
    auto working_directory = UE4SSProgram::get_program().get_working_directory();
    auto main_directory = fs::path(working_directory) / STR("Mods") / STR("SphereZones");
    return main_directory.wstring() + L"\\";
}

std::wstring ModConfig::GetConfigDirectory() {
    return (fs::path(GetModDirectory()) / STR("config")).wstring() + L"\\";
}

static std::string Trim(std::string value) {
    auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    value.erase(0, first);
    value.erase(value.find_last_not_of(" \t\r\n") + 1);
    return value;
}

static bool ToBool(const std::string& value) {
    return value == "true" || value == "1" || value == "yes";
}

static int ToInt(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

void ModConfig::Load() {
    std::wstring configPath = GetConfigDirectory() + L"settings.ini";
    std::ifstream file(configPath);

    if (!file.is_open()) {
        Output::send(STR("[SphereZones] No settings.ini found, using defaults\n"));
        return;
    }

    std::string line;
    std::string section;

    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[') {
            auto end = line.find(']');
            if (end != std::string::npos) section = line.substr(1, end - 1);
            continue;
        }

        auto pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = Trim(line.substr(0, pos));
        std::string value = Trim(line.substr(pos + 1));

        if (section == "General") {
            if (key == "Enabled") Enabled = ToBool(value);
            else if (key == "DebugLogging") DebugLogging = ToBool(value);
            else if (key == "ZonesFile") ZonesFile = value;
        } else if (section == "Messages") {
            if (key == "ShowBlockMessage") ShowBlockMessage = ToBool(value);
            else if (key == "NotifyCooldownSeconds") NotifyCooldownSeconds = ToInt(value, NotifyCooldownSeconds);
            else if (key == "DamageBlockMessage") DamageBlockMessage = value;
            else if (key == "StructureDamageBlockMessage") StructureDamageBlockMessage = value;
            else if (key == "BuildBlockMessage") BuildBlockMessage = value;
            else if (key == "DismantleBlockMessage") DismantleBlockMessage = value;
            else if (key == "SignEditBlockMessage") SignEditBlockMessage = value;
            else if (key == "GroundMountBlockMessage") GroundMountBlockMessage = value;
            else if (key == "FlyingMountBlockMessage") FlyingMountBlockMessage = value;
            else if (key == "ZoneLockedMessage") ZoneLockedMessage = value;
            else if (key == "ZoneLevelMessage") ZoneLevelMessage = value;
        }
    }

    if (NotifyCooldownSeconds < 0) NotifyCooldownSeconds = 0;

    Output::send(STR("[SphereZones] Config loaded\n"));
}
