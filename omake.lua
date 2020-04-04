project = CreateProject()

omake = project:CreateBinary("omake")
omake:AddSourceFile("*.cpp")
omake:AddStaticLibrary("../lua-cpp", "luacpp")
omake:AddStaticLibrary("../text-utils", "text_utils")
omake:AddSystemDynamicLibraries("dl", "m")

return project
