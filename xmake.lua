add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})
add_repositories("local-repo build")
add_requires("argparse")
add_requires("spdlog")
set_license("GPL")

target("avs-operator")
    set_kind("binary")
    set_languages("c++17")
    add_files("src/*.cpp")
    add_linkdirs("src/lib")
    add_links("avaspecx64")
    
    set_toolchains("gcc")
    add_packages("argparse")
    add_packages("spdlog")
    