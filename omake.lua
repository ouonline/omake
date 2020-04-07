project = CreateProject()

omake = project:CreateBinary("omake")
omake:AddSourceFiles("*.cpp")
omake:AddLibrary("../lua-cpp", "luacpp", STATIC)
omake:AddLibrary("../text-utils", "text_utils", STATIC)
omake:AddSysLibraries("dl", "m")

return project
