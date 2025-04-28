target("utoc_reader_prototype")
    set_kind("static")
    add_files("src/*.cpp")
    add_includedirs("include", { public = true })
