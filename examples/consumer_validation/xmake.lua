set_xmakever("2.8.2")
set_project("randx_consumer_validation")

local package_version = os.getenv("RANDX_PACKAGE_VERSION")
if not package_version then
    raise("RANDX_PACKAGE_VERSION must identify the package version under validation")
end

add_requires("randx " .. package_version)

target("randx_consumer_cpp17")
    set_kind("binary")
    set_languages("cxx17")
    set_encodings("utf-8")
    add_files("consumer.cpp")
    add_defines("RANDX_CONSUMER_STANDARD_17=1")
    add_packages("randx")

target("randx_consumer_cpp23")
    set_kind("binary")
    set_languages("cxx23")
    set_encodings("utf-8")
    add_files("consumer.cpp")
    add_defines("RANDX_CONSUMER_STANDARD_23=1")
    add_packages("randx")
