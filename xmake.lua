-- include subprojects
includes("lib/commonlibsse-ng")

-- set project constants
set_project("TentacleCombat")
set_version("1.0.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")

-- define targets
target("TentacleCombat")
    set_version("1.1.1")
    add_rules("commonlibsse-ng.plugin", {
        name = "TentacleCombat",
        author = "W1ter",
        description = "Apocrypha's tentacles stay up and fight, and can be cut in half"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
