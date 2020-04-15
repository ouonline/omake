#include "project.h"
#include "target.h"
#include "text-utils/text_utils.h"
#include "common.h"
#include <iostream>
using namespace std;
using namespace utils;

#include "utils.h"
using namespace luacpp;

string RemoveDotAndDotDot(const string& path) {
    vector<string> path_stack;
    if (path[0] == '/') {
        path_stack.push_back("/");
    }

    TextSplit(path.data(), path.size(), "/", 1, [&path_stack] (const char* s, unsigned int l) -> bool {
        if (l == 0) {
            return true;
        }

        if (path_stack.empty()) {
            path_stack.push_back(std::move(string(s, l)));
        } else {
            if (l == 1 && s[0] == '.') {
                return true;
            } else if (l == 2 && s[0] == '.' && s[1] == '.') {
                if (path_stack.back() == "..") {
                    path_stack.push_back(std::move(string(s, l)));
                } else if (path_stack.back() == ".") {
                    path_stack.back() = string(s, l);
                } else if (path_stack.size() == 1 && path_stack[0] == "/") {
                    return true;
                } else {
                    path_stack.pop_back();
                }
                return true;
            } else {
                if (path_stack.back() == ".") {
                    path_stack.back() = std::move(string(s, l));
                } else {
                    path_stack.push_back(std::move(string(s, l)));
                }
            }
        }
        return true;
    });

    if (path_stack.empty()) {
        return string();
    }

    string new_path;
    if (path_stack[0] != "/") {
        new_path = path_stack[0];
    }
    for (size_t i = 1; i < path_stack.size(); ++i) {
        new_path.append("/" + path_stack[i]);
    }
    return new_path;
}

string GetBaseName(const string& path) {
    const char* begin = path.data();
    const char* end = path.data() + path.size();
    const char* cursor = end - 1;

    for (cursor = end - 1; cursor >= begin; --cursor) {
        if (*cursor == '/') {
            return string(cursor + 1, end - cursor - 1);
        }
    }

    return path;
}

/* -------------------------------------------------------------------------- */

static Project* CreateProject() {
    return new Project();
}

static int l_AddFlags(lua_State* l) {
    int argc = lua_gettop(l);
    auto dep = *((Dependency**)lua_touserdata(l, 1));
    for (int i = argc; i >= 2; --i) {
        dep->AddFlag(lua_tostring(l, i));
    }

    return 0;
}

static int l_AddSourceFiles(lua_State* l) {
    int argc = lua_gettop(l);
    auto dep = *((Dependency**)lua_touserdata(l, 1));
    for (int i = argc; i >= 2; --i) {
        dep->AddSourceFiles(lua_tostring(l, i));
    }

    return 0;
}

static int l_AddStaticLibrary(lua_State* l) {
    int argc = lua_gettop(l);
    if (argc != 3) {
        cerr << "AddStaticLibrary() requires 2 arguments: `path` and `name`." << endl;
        return 0;
    }

    auto dep = *((Dependency**)lua_touserdata(l, 1));
    dep->AddLibrary(lua_tostring(l, 2), // path
                    lua_tostring(l, 3), // name
                    OMAKE_TYPE_STATIC);

    return 0;
}

static int l_AddSharedLibrary(lua_State* l) {
    int argc = lua_gettop(l);
    if (argc != 3) {
        cerr << "AddSharedLibrary() requires 2 arguments: `path` and `name`." << endl;
        return 0;
    }

    auto dep = *((Dependency**)lua_touserdata(l, 1));
    dep->AddLibrary(lua_tostring(l, 2), // path
                    lua_tostring(l, 3), // name
                    OMAKE_TYPE_SHARED);

    return 0;
}

static int l_AddSysLibraries(lua_State* l) {
    int argc = lua_gettop(l);
    auto dep = *((Dependency**)lua_touserdata(l, 1));
    for (int i = argc; i >= 2; --i) {
        dep->AddLibrary(nullptr, // path
                        lua_tostring(l, i), // name
                        OMAKE_TYPE_SHARED);
    }

    return 0;
}

static int l_AddIncludeDirectories(lua_State* l) {
    int argc = lua_gettop(l);
    auto dep = *((Dependency**)lua_touserdata(l, 1));
    for (int i = argc; i >= 2; --i) {
        dep->AddIncludeDirectory(lua_tostring(l, i));
    }

    return 0;
}

static int l_TargetAddDependencies(lua_State* l) {
    int argc = lua_gettop(l);
    auto target = *((Target**)lua_touserdata(l, 1));
    for (int i = argc; i >= 2; --i) {
        auto d = *(Dependency**)lua_touserdata(l, i);
        target->AddDependency(d);
    }

    return 0;
}

void InitLuaEnv(LuaState* l) {
    l->newfunction(CreateProject, "CreateProject");

    l->newclass<Project>("Project")
        .set("CreateBinary", &Project::CreateBinary)
        .set("CreateStaticLibrary", &Project::CreateStaticLibrary)
        .set("CreateSharedLibrary", &Project::CreateSharedLibrary)
        .set("CreateDependency", &Project::CreateDependency);

    l->newclass<Dependency>("Dependency")
        .set("AddFlags", l_AddFlags)
        .set("AddSourceFiles", l_AddSourceFiles)
        .set("AddStaticLibrary", l_AddStaticLibrary)
        .set("AddSharedLibrary", l_AddSharedLibrary)
        .set("AddSysLibraries", l_AddSysLibraries)
        .set("AddIncludeDirectories", l_AddIncludeDirectories);

    l->newclass<Target>("Target")
        .set("AddDependencies", l_TargetAddDependencies);
}
