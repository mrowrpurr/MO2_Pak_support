target("mo2_mock")
    set_kind("static")
    add_files("src/*.cpp")
    add_includedirs("include", { public = true })
