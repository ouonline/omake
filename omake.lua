project = Project()

project:CreateBinary("omake"):AddDependencies(
    project:CreateDependency()
        :AddSourceFiles("*.cpp")
        :AddFlags({"-Wall", "-Werror", "-Wextra"})
        :AddStaticLibraries("../lua-cpp", "luacpp_static")
        :AddStaticLibraries("../cpputils", "cpputils_static"))

return project
