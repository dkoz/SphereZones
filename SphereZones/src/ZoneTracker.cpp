#include "ZoneTracker.h"

#include "Guard.h"
#include "ModConfig.h"
#include "Utils.h"
#include "ZoneStore.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace ZoneTracker {

static constexpr int JOIN_SETTLE_SECONDS = 4;

struct TrackedPlayer {
    std::string ZoneName;
    std::vector<uint8_t> AppliedStatus;
    std::chrono::steady_clock::time_point FirstSeen{};
    bool Settled = false;

    UObject* Pawn = nullptr;

    Utils::FVectorLite LastAllowed{};
    Utils::FRotatorLite LastAllowedRotation{};
    bool HasLastAllowed = false;

    bool RideBlocked = false;

    int DismountAttempts = 0;

    int SafePointAttempts = 0;
};

static constexpr int MAX_DISMOUNT_ATTEMPTS = 3;
static constexpr int MAX_SAFE_POINT_ATTEMPTS = 2;

static std::unordered_map<int32_t, TrackedPlayer> g_Players;

static std::unordered_map<UObject*, std::chrono::steady_clock::time_point> g_LastEvaluated;

void Reset() {
    g_Players.clear();
    g_LastEvaluated.clear();
}

static UObject* GetStatusComponent(UObject* pawn) {
    if (!pawn) return nullptr;

    auto prop = pawn->GetPropertyByNameInChain(STR("StatusComponent"));
    if (!prop) return nullptr;

    auto ptr = prop->ContainerPtrToValuePtr<uint8_t>(pawn);
    if (!ptr) return nullptr;

    return *reinterpret_cast<UObject**>(ptr);
}

static void CallStatusFunction(UObject* statusComponent, const wchar_t* funcName, uint8_t statusId) {
    UFunction* fn = statusComponent->GetFunctionByNameInChain(funcName);
    if (!fn) return;

    struct { uint8_t StatusID; uint8_t Pad[7]; } params{};
    params.StatusID = statusId;
    statusComponent->ProcessEvent(fn, &params);
}

static bool HasStatus(UObject* statusComponent, uint8_t statusId) {
    UFunction* fn = statusComponent->GetFunctionByNameInChain(STR("GetExecutionStatus"));
    if (!fn) return false;

    struct { uint8_t StatusID; uint8_t Pad[7]; UObject* ReturnValue; } params{};
    params.StatusID = statusId;
    statusComponent->ProcessEvent(fn, &params);
    return params.ReturnValue != nullptr;
}

static void RefreshStatuses(UObject* pawn, const Zones::Zone& zone, int32_t playerId) {
    if (zone.AddStatus.empty()) return;

    UObject* statusComponent = GetStatusComponent(pawn);
    if (!statusComponent) return;

    for (uint8_t statusId : zone.AddStatus) {
        if (HasStatus(statusComponent, statusId)) continue;

        CallStatusFunction(statusComponent, STR("AddStatus"), statusId);
        if (ModConfig::Get().DebugLogging) {
            Output::send(STR("[SphereZones] Player {} AddStatus({}) reapplied after expiry\n"), playerId, statusId);
        }
    }
}

static void ClearStatuses(UObject* pawn, TrackedPlayer& tracked, int32_t playerId) {
    if (tracked.AppliedStatus.empty()) return;

    UObject* statusComponent = GetStatusComponent(pawn);
    if (statusComponent) {
        for (uint8_t statusId : tracked.AppliedStatus) {
            CallStatusFunction(statusComponent, STR("RemoveStatus"), statusId);
            if (ModConfig::Get().DebugLogging) {
                Output::send(STR("[SphereZones] Player {} RemoveStatus({})\n"), playerId, statusId);
            }
        }
    }

    tracked.AppliedStatus.clear();
}

static void ApplyStatuses(UObject* pawn, TrackedPlayer& tracked, const Zones::Zone& zone, int32_t playerId) {
    if (zone.AddStatus.empty()) return;

    UObject* statusComponent = GetStatusComponent(pawn);
    if (!statusComponent) return;

    for (uint8_t statusId : zone.AddStatus) {
        CallStatusFunction(statusComponent, STR("AddStatus"), statusId);
        tracked.AppliedStatus.push_back(statusId);
        if (ModConfig::Get().DebugLogging) {
            Output::send(STR("[SphereZones] Player {} AddStatus({})\n"), playerId, statusId);
        }
    }
}

static const char* EntryDenialReason(const Zones::Zone& zone, UObject* pawn, int32_t& outLevel) {
    outLevel = 0;
    if (!zone.HasEntryLock()) return nullptr;

    if (zone.Locked) return "locked";

    if (zone.MinimumLevel > 0) {
        outLevel = Utils::GetCharacterLevel(pawn);
        if (outLevel > 0 && outLevel < zone.MinimumLevel) return "level";
    }

    return nullptr;
}

static void EnforceMountRules(UObject* controller, UObject* pawn, TrackedPlayer& tracked,
                              const Zones::Zone& zone, int32_t playerId) {
    auto& cfg = ModConfig::Get();

    if (Utils::IsAdmin(controller)) {
        if (tracked.RideBlocked) {
            Utils::SetRideDisabled(pawn, false);
            tracked.RideBlocked = false;
        }
        return;
    }

    bool groundAllowed = Zones::Store::IsWorldActionAllowed(zone, "GroundMount");
    bool flyAllowed = Zones::Store::IsWorldActionAllowed(zone, "FlyingMount");

    bool wantBlock = !groundAllowed || !flyAllowed;
    if (wantBlock != tracked.RideBlocked) {
        if (Utils::SetRideDisabled(pawn, wantBlock)) {
            tracked.RideBlocked = wantBlock;
            if (cfg.DebugLogging) {
                Output::send(STR("[SphereZones] Player {} ride block {} in '{}'\n"),
                             playerId, wantBlock,
                             std::wstring(zone.Name.begin(), zone.Name.end()));
            }
        }
    }

    if (!Utils::IsRiding(controller)) {
        tracked.DismountAttempts = 0;
        return;
    }

    bool flying = Utils::IsRidingFlyPal(controller);
    if (flying ? flyAllowed : groundAllowed) {
        tracked.DismountAttempts = 0;
        return;
    }

    if (tracked.DismountAttempts >= MAX_DISMOUNT_ATTEMPTS) return;

    tracked.DismountAttempts++;
    bool onFoot = Utils::Dismount(controller);

    if (onFoot) {
        tracked.DismountAttempts = 0;
    } else if (tracked.DismountAttempts >= MAX_DISMOUNT_ATTEMPTS) {
        Output::send(STR("[SphereZones] Player {} would not dismount in '{}' after {} attempts, giving up until they move or remount\n"),
                     playerId, Utils::Widen(zone.Name), MAX_DISMOUNT_ATTEMPTS);
    }

    if (cfg.DebugLogging) {
        Output::send(STR("[SphereZones] Player {} dismount attempt {} in '{}' (flying={}, onFoot={})\n"),
                     playerId, tracked.DismountAttempts, Utils::Widen(zone.Name), flying, onFoot);
    }

    if (!cfg.ShowBlockMessage) return;
    if (!Utils::ShouldNotify(playerId, flying ? "flymount" : "groundmount")) return;

    Utils::NotifyPlayer(controller, Utils::ReplaceAll(
        flying ? cfg.FlyingMountBlockMessage : cfg.GroundMountBlockMessage, "{zone}", zone.Name));
}

static void PushBack(UObject* controller, UObject* pawn, TrackedPlayer& tracked,
                     const Zones::Zone& zone, const char* reason, int32_t level, int32_t playerId) {
    auto& cfg = ModConfig::Get();

    if (!tracked.HasLastAllowed) {

        if (tracked.SafePointAttempts >= MAX_SAFE_POINT_ATTEMPTS) return;

        tracked.SafePointAttempts++;
        bool sent = Utils::TeleportToSafePoint(controller);

        Output::send(STR("[SphereZones] Player {} connected inside '{}' with no recorded position; safe point teleport attempt {} sent={}\n"),
                     playerId, Utils::Widen(zone.Name), tracked.SafePointAttempts, sent);

        if (tracked.SafePointAttempts >= MAX_SAFE_POINT_ATTEMPTS) {

            Output::send(STR("[SphereZones] Player {} still inside '{}' after {} safe point attempts, leaving them be\n"),
                         playerId, Utils::Widen(zone.Name), MAX_SAFE_POINT_ATTEMPTS);
        }

        if (cfg.ShowBlockMessage && Utils::ShouldNotify(playerId, "entry")) {
            std::string message = std::string(reason) == "level"
                                      ? Utils::ReplaceAll(cfg.ZoneLevelMessage, "{level}", std::to_string(zone.MinimumLevel))
                                      : cfg.ZoneLockedMessage;
            Utils::NotifyPlayer(controller, Utils::ReplaceAll(message, "{zone}", zone.Name));
        }
        return;
    }

    bool moved = Utils::TeleportActor(pawn, tracked.LastAllowed, tracked.LastAllowedRotation);

    if (cfg.DebugLogging) {
        Output::send(STR("[SphereZones] Player {} denied entry to '{}' ({}, level={}), teleport={} back to X={:.0f} Y={:.0f}\n"),
                     playerId, Utils::Widen(zone.Name), Utils::Widen(reason ? reason : ""),
                     level, moved, tracked.LastAllowed.X, tracked.LastAllowed.Y);
    }

    if (!moved || !cfg.ShowBlockMessage) return;
    if (!Utils::ShouldNotify(playerId, "entry")) return;

    std::string message = std::string(reason) == "level"
                              ? Utils::ReplaceAll(cfg.ZoneLevelMessage, "{level}", std::to_string(zone.MinimumLevel))
                              : cfg.ZoneLockedMessage;
    Utils::NotifyPlayer(controller, Utils::ReplaceAll(message, "{zone}", zone.Name));
}

static void EvaluatePlayer(UObject* controller, UObject* pawn, int32_t playerId,
                           const Utils::FVectorLite& location,
                           std::chrono::steady_clock::time_point now) {
    auto& store = Zones::Store::Get();
    auto& cfg = ModConfig::Get();

    const Zones::Zone& zone = store.ZoneAt(location.X, location.Y);

    TrackedPlayer& tracked = g_Players[playerId];

    if (tracked.FirstSeen == std::chrono::steady_clock::time_point{}) {
        tracked.FirstSeen = now;
    }

    if (!tracked.Settled) {
        auto waited = std::chrono::duration_cast<std::chrono::seconds>(now - tracked.FirstSeen).count();
        if (waited < JOIN_SETTLE_SECONDS) {
            if (cfg.DebugLogging) {
                Output::send(STR("[SphereZones] Player {} settling, ignoring X={:.0f} Y={:.0f} ('{}')\n"),
                             playerId, location.X, location.Y,
                             std::wstring(zone.Name.begin(), zone.Name.end()));
            }
            return;
        }

        tracked.Settled = true;
        tracked.Pawn = pawn;
        tracked.ZoneName = zone.Name;

        if (cfg.DebugLogging) {
            Output::send(STR("[SphereZones] Player {} joined in '{}' at X={:.0f} Y={:.0f}\n"),
                         playerId, std::wstring(zone.Name.begin(), zone.Name.end()),
                         location.X, location.Y);
        }

        if (!zone.IsGlobal) {
            Utils::NotifyPlayer(controller, zone.Name);
            ApplyStatuses(pawn, tracked, zone, playerId);
        }

        int32_t baselineLevel = 0;
        if (!EntryDenialReason(zone, pawn, baselineLevel)) {
            tracked.LastAllowed = location;
            Utils::GetActorRotation(pawn, tracked.LastAllowedRotation);
            tracked.HasLastAllowed = true;
        }
        return;
    }

    if (tracked.Pawn != pawn) {
        if (cfg.DebugLogging) {
            Output::send(STR("[SphereZones] Player {} pawn changed, re-deriving zone\n"), playerId);
        }
        tracked.Pawn = pawn;
        tracked.AppliedStatus.clear();
        tracked.ZoneName.clear();

        tracked.RideBlocked = false;
    }

    int32_t level = 0;
    const char* denial = Utils::IsAdmin(controller) ? nullptr : EntryDenialReason(zone, pawn, level);

    if (denial) {
        PushBack(controller, pawn, tracked, zone, denial, level, playerId);
        return;
    }

    tracked.LastAllowed = location;
    Utils::GetActorRotation(pawn, tracked.LastAllowedRotation);
    tracked.HasLastAllowed = true;

    tracked.SafePointAttempts = 0;

    EnforceMountRules(controller, pawn, tracked, zone, playerId);

    if (tracked.ZoneName == zone.Name) {
        RefreshStatuses(pawn, zone, playerId);
        return;
    }

    if (cfg.DebugLogging) {
        Output::send(STR("[SphereZones] Player {} zone '{}' -> '{}' at X={:.0f} Y={:.0f}\n"),
                     playerId,
                     std::wstring(tracked.ZoneName.begin(), tracked.ZoneName.end()),
                     std::wstring(zone.Name.begin(), zone.Name.end()),
                     location.X, location.Y);
    }

    ClearStatuses(pawn, tracked, playerId);

    tracked.ZoneName = zone.Name;

    if (!zone.IsGlobal) {
        Utils::NotifyPlayer(controller, zone.Name);
    }

    ApplyStatuses(pawn, tracked, zone, playerId);
}

static constexpr int EVALUATE_INTERVAL_SECONDS = 2;

static void HandleMovement(UObject* character) {
    auto& store = Zones::Store::Get();
    if (!store.IsLoaded()) return;
    if (!Guard::IsReadableObject(character)) return;

    auto now = std::chrono::steady_clock::now();

    auto it = g_LastEvaluated.find(character);
    if (it != g_LastEvaluated.end()) {
        if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count() < EVALUATE_INTERVAL_SECONDS) {
            return;
        }
        it->second = now;
    } else {
        g_LastEvaluated.emplace(character, now);
    }

    UObject* controller = nullptr;
    if (auto prop = character->GetPropertyByNameInChain(STR("Controller"))) {
        if (auto ptr = prop->ContainerPtrToValuePtr<uint8_t>(character)) {
            controller = *reinterpret_cast<UObject**>(ptr);
        }
    }
    if (!Guard::IsReadableObject(controller)) return;

    int32_t playerId = Utils::ExtractControllerId(controller);
    if (playerId == 0) return;

    Utils::FVectorLite location{};
    if (!Utils::GetActorLocation(character, location)) return;

    EvaluatePlayer(controller, character, playerId, location, now);
}

void OnPlayerMoved(UObject* character) {

    try {
        Guard::Run(STR("ZoneTracker::HandleMovement"), [&] { HandleMovement(character); });
    } catch (const std::exception& error) {
        std::string what = error.what() ? error.what() : "unknown";
        Output::send(STR("[SphereZones] Movement handler threw {}; hook kept alive\n"), std::wstring(what.begin(), what.end()));
    } catch (...) {
        Output::send(STR("[SphereZones] Movement handler threw an unknown exception; hook kept alive\n"));
    }
}

void ForgetPlayer(int32_t playerId) {
    g_Players.erase(playerId);
}

}
