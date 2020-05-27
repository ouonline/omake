project = CreateProject()

project:CreateBinary("omake"):AddDependencies(
    project:CreateDependency()
        :AddSourceFiles("*.cpp")
        :AddFlags("-Wall", "-Werror", "-Wextra")
        :AddStaticLibrary("../lua-cpp", "luacpp_static")
        :AddStaticLibrary("../cpputils", "cpputils_static"))

return project
