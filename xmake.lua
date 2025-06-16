option("conf", function()
end)

option("core", function()
    set_showmenu(true)
    set_values("cortex-m/thumbv7em", "risc-v/riscv")
end)

option("priority_max", function()
    set_default(32)
end)

target("kernel", function()
    set_kind("static")

    add_options("conf")
    if has_config("conf") then
        add_defines("CONFIG_MDS_CONFIG_FILE=\"$(conf)\"", {
            public = true
        })
    end

    add_includedirs("inc", {
        public = true
    })

    add_files("src/log.c", "src/object.c", "src/device.c")

    add_options("core")
    if has_config("core") then
        add_files("core/$(core).c")
    end

    add_files("src/lib/**.c")
    add_files("src/mem/**.c")

    add_options("priority_max")
    if (get_config("priority_max") == "0") then
        add_files("src/nosys.c")
    else
        add_files("src/sys/**.c")
    end

    -- ld section for macos
    on_load(function(target)
        if is_plat("macosx") then
            target:add("defines", "CONFIG_MDS_INIT_SECTION=\"__TEXT__,init_\"", {
                public = true
            })
            target:add("defines", "CONFIG_MDS_LOG_FORMAT_SECTION=\"__TEXT__,logfmt_\"", {
                public = true
            })
        end
    end)

end)
