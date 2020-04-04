#include "project.h"
#include "target.h"
#include "misc.h"
using namespace luacpp;

static Project* CreateProject() {
    return new Project();
}

static int l_AddSystemDynamicLibraries(lua_State* l) {
    int argc = lua_gettop(l);
    auto target = (Target**)lua_touserdata(l, 1);
    for (int i = 2; i <= argc; ++i) {
        (*target)->AddSystemDynamicLibrary(lua_tostring(l, i));
    }
    return 0;
}

void InitLuaEnv(LuaState* l) {
    l->newfunction(CreateProject, "CreateProject");

    l->newclass<Project>("Project")
        .set("CreateBinary", &Project::CreateBinary)
        .set("CreateLibrary", &Project::CreateLibrary);

    l->newclass<Target>("Target")
        .set("AddSourceFile", &Target::AddSourceFile)
        .set("AddStaticLibrary", &Target::AddStaticLibrary)
        .set("AddDynamicLibrary", &Target::AddDynamicLibrary)
        .set("AddSystemDynamicLibraries", l_AddSystemDynamicLibraries);
}
