project = CreateProject()

dep = project:CreateDependency()
    :AddSourceFiles("*.cpp")
    :AddFlags("-Wall", "-Werror", "-Wextra")
    :AddStaticLibrary("../lua-cpp", "luacpp_static")
    :AddStaticLibrary("../text-utils", "text_utils_static")

project:CreateBinary("omake")
    :AddDependencies(dep)

return project
