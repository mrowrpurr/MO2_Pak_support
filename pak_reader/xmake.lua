target("pak_reader")
    set_kind("static")
    add_files("src/*.cpp")
    add_includedirs("include", { public = true })
