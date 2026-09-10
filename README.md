# Sphere Zones

Sphere Zones is a Palworld server mod that lets server owners draw custom areas on the map with their own rules.

This mod was created by DecioLuvier/PWAnother and is now maintained by me. The original mod was written in Lua and has been ported to C++.

## What a zone can control

| Rule | Description |
| --- | --- |
| **Damage** | Controls which entity types can damage other entity types inside the zone. Supports players, held Pals, base Pals, wild Pals, NPCs, and structures. |
| **Building** | Controls whether players can place structures. |
| **Dismantling** | Controls whether players can remove structures. |
| **Sign Editing** | Controls whether players can edit signboards. |
| **Ground Mounts** | Controls whether players can use ground mounts. |
| **Flying Mounts** | Controls whether players can use flying mounts. |
| **Status Effects** | Applies configured status effects when a player enters the zone and maintains them while they remain inside. |
| **Entry Lock** | Prevents players from entering the zone when `locked` is enabled. |
| **Level Requirement** | Prevents players below the configured `minimumLevel` from entering the zone. |

Server admins bypass build, dismantle, sign editing, mounts and entry locks.

## Repository layout

```
SphereZones/     UE4SS C++ mod, built with xmake
ZonesTool/       Flask + Leaflet map tool, packaged with PyInstaller
```

## Installing the mod

Build it inside an RE-UE4SS checkout, with the mod folder placed in `cppmods/` and registered in `cppmods/xmake.lua`:

```
xmake build SphereZones
```

Then copy the result into the server:

```
ue4ss/Mods/SphereZones/
├── dlls/main.dll
├── enabled.txt
└── config/
    ├── settings.ini
    └── zones.json
```

`zones.json` is read once at startup, so zone edits need a server restart.

## Building the map tool

```
python -m venv venv
venv\Scripts\activate
pip install -r ZonesTool/requirements.txt
pyinstaller ZonesTool/build.spec
```

Map tiles are not in the repository because of their size. Without them the tool runs but the map renders blank.

## Zone file format

```json
{
  "version": 2,
  "global": { "permissions": {} },
  "zones": [
    {
      "name": "Boss Arena",
      "points": [ { "x": -350000, "y": 240000 } ],
      "locked": false,
      "minimumLevel": 30,
      "addStatus": [5, 19],
      "permissions": {
        "Player": {
          "world": ["Build", "Dismantle", "EditSign", "GroundMount", "FlyingMount"],
          "damage": ["WildPal", "NPC"]
        }
      }
    }
  ]
}
```

`world` and `damage` are allow-lists. An entry that is present is permitted, one that is missing is denied. A category left out entirely is unrestricted, which is not the same as an empty list.

`version` tells the mod the file knows about `EditSign`, `GroundMount` and `FlyingMount`. A file without it predates those three and they stay allowed everywhere until it is re-exported.

`addStatus` takes `EPalStatusID` values. Poison is 5, Burn is 19.

## License

Released under the MIT License. See [LICENSE](LICENSE) for the full text.

This software is provided as is, without warranty of any kind. Use it at your own risk.

If you use this code in your own project, please provide credit and keep a link back to
this repository.
