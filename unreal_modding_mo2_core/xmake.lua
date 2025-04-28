target("unreal_modding_mo2_core")
    set_kind("static")
    add_files("src/*.cpp")
    add_includedirs("include", { public = true })
