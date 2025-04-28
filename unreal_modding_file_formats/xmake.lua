target("unreal_modding_file_formats")
    set_kind("static")
    add_files("src/*.cpp")
    add_includedirs("include", { public = true })
