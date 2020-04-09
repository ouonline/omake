project = CreateProject()

omake = project:CreateBinary("omake")
omake:AddSourceFiles("*.cpp")
omake:AddFlags("-Wall", "-Werror", "-Wextra", "-fPIC")
omake:AddStaticLibrary("../lua-cpp", "luacpp_static")
omake:AddStaticLibrary("../text-utils", "text_utils_static")
omake:AddSysLibraries("dl", "m")

return project
