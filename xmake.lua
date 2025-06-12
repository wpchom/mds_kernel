option("conf", function()
end)

option("core", function()
    set_showmenu(true)
    set_values("cortex-m/thumbv7em", "risc-v/riscv")
end)

target("mds_kernel", function()
    set_kind("static")

    add_includedirs("inc")

    add_options("conf")
    if has_config("conf") then
        add_defines("CONFIG_MDS_CONFIG_FILE=\"$(conf)\"")
    end

    add_files("src/*.c")
    add_files("src/lib/*.c")
    add_files("src/mem/*.c")
    add_files("src/sys/**.c")

    add_options("core")
    if has_config("core") then
        add_files("src/core/$(core).c")
    end

    on_load(function(target)
        if is_plat("macosx") then
            target:add("defines", "CONFIG_MDS_INIT_SECTION=\"__TEXT__,init_\"")
            target:add("defines", "CONFIG_MDS_LOG_FORMAT_SECTION=\"__TEXT__,logfmt_\"")
        end
    end)
end)
