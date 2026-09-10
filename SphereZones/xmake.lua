local projectName = "SphereZones"

target(projectName)
    add_rules("ue4ss.mod")
    set_languages("cxxlatest")
    add_cxflags("/EHa", {force = true})
    add_includedirs("include")
    add_headerfiles("include/**.h", "include/**.hpp")
    add_files("src/**.cpp")
    add_extrafiles("assets/config/settings.ini", "assets/config/zones.json")
