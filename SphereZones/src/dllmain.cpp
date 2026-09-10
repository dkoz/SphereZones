#include "Guard.h"
#include "ModConfig.h"
#include "ZoneGuard.h"
#include "ZoneStore.h"
#include "ZoneTracker.h"

#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>

#include <string>

using namespace RC;
using namespace RC::Unreal;

class SphereZonesMod : public CppUserModBase {
public:
    SphereZonesMod() : CppUserModBase() {
        ModName = STR("SphereZones");
        ModVersion = STR("1.0.1");
        ModDescription = STR("Polygon zones for Palworld");
        ModAuthors = STR("Caffeinemodz");
    }

    auto on_unreal_init() -> void override {
        ModConfig::Get().Load();

        if (!ModConfig::Get().Enabled) {
            Output::send(STR("[SphereZones] Disabled via config\n"));
            return;
        }

        if (!Zones::Store::Get().Load()) {
            Output::send(STR("[SphereZones] No zones loaded, nothing will be enforced\n"));
        }

        Hook::RegisterProcessEventPreCallback([](UObject* Context, UFunction* Function, void* Parms) {

            if (!Function || !ZoneGuard::IsWatchedFunction(Function)) return;

            static thread_local bool inDispatch = false;
            if (inDispatch) return;
            inDispatch = true;
            struct Leave { ~Leave() { inDispatch = false; } } leave;

            try {
                Guard::Run(STR("ProcessEvent dispatch"), [&] {
                    ZoneGuard::OnProcessEvent(Context, Function, Parms);
                });
            } catch (...) {
                Output::send(STR("[SphereZones] ProcessEvent dispatch threw; callback kept alive\n"));
            }
        });

        ZoneGuard::InstallActionHooks();

        Output::send(STR("[SphereZones] Loaded, {} zone(s) active\n"), Zones::Store::Get().ZoneCount());
    }
};

#define SPHERE_ZONES_API __declspec(dllexport)
extern "C" {
    SPHERE_ZONES_API RC::CppUserModBase* start_mod() {
        return new SphereZonesMod();
    }

    SPHERE_ZONES_API void uninstall_mod(RC::CppUserModBase* mod) {
        delete mod;
    }
}
