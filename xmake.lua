







set_project("gpxinput")
set_version("1.0.0")

set_allowedmodes("debug", "release")
set_defaultmode("release")
set_languages("c++17")


set_runtimes("MD")

add_rules("mode.debug", "mode.release")
add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS", "UNICODE", "_UNICODE")
add_cxxflags("/EHsc", "/W3", "/utf-8")
add_includedirs("include")

if is_mode("release") then
    set_optimize("fastest")
else
    set_optimize("none")
    set_symbols("debug")
end


target("gen_def")
    set_kind("binary")
    add_files("tools/gen_def/gen_def.cpp")
    set_targetdir("$(builddir)/tools")



local proxy_sources = {
    "proxy/gp_proxy.cpp",
    "proxy/gp_real.cpp",
    "proxy/gp_hooks.cpp",
    "proxy/gp_engine.cpp",
    "proxy/gp_ipc_client.cpp",
    "proxy/gp_hid.cpp",
    "proxy/gp_capture.cpp",
    "proxy/gp_haptics.cpp",
    "proxy/gp_gamestate.cpp",
    "proxy/gp_config.cpp",
    "proxy/gp_log.cpp",
}

local function add_proxy(name, def_file)
    target(name)
        set_kind("shared")
        set_filename(name .. ".dll")
        set_basename(name)

        add_files(proxy_sources)
        add_files(def_file)

        add_includedirs("proxy", "minhook/include")
        add_linkdirs("minhook/lib")
        add_links("libMinHook.x64")


        add_syslinks("setupapi", "hid", "user32")


        set_targetdir("$(builddir)/proxy/" .. name)

        if is_mode("release") then
            set_symbols("hidden")
        end
    target_end()
end

add_proxy("xinput1_4",   "proxy/exports_xinput1_4.def")
add_proxy("xinput1_3",   "proxy/exports_xinput1_3.def")
add_proxy("xinput9_1_0", "proxy/exports_xinput9_1_0.def")



target("gpxinput_rdr2")
    set_kind("shared")
    set_filename("gpxinput_rdr2.asi")
    add_files("asi_rdr2/gpxinput_rdr2.cpp")
    add_includedirs("asi_rdr2")
    set_targetdir("$(builddir)/bin")


target("gp_monitor")
    set_kind("binary")
    add_files("tools/gp_monitor/gp_monitor.cpp")
    set_targetdir("$(builddir)/bin")


target("xinput_test")
    set_kind("binary")
    add_files("tools/xinput_test/xinput_test.cpp")
    set_targetdir("$(builddir)/tools")
