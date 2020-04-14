project = CreateProject()

dep = project:CreateDependency()
dep:AddSourceFiles("*.cpp")
dep:AddFlags("-Wall", "-Werror", "-Wextra")
dep:AddStaticLibrary("../lua-cpp", "luacpp_static")
dep:AddStaticLibrary("../text-utils", "text_utils_static")
dep:AddSysLibraries("dl", "m")

omake = project:CreateBinary("omake")
omake:AddDependencies(dep)

return project
