#include "project.h"
#include "target.h"
#include "cpputils/text_utils.h"
#include "common.h"
#include <iostream>
using namespace std;
using namespace outils;

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
            path_stack.push_back(string(s, l));
        } else {
            if (l == 1 && s[0] == '.') {
                return true;
            } else if (l == 2 && s[0] == '.' && s[1] == '.') {
                if (path_stack.back() == "..") {
                    path_stack.push_back(string(s, l));
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
                    path_stack.back() = string(s, l);
                } else {
                    path_stack.push_back(string(s, l));
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

int FindParentDirPos(const char* fpath, int plen) {
    for (--plen; plen >= 0; --plen) {
        if (fpath[plen] == '/') {
            return plen;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------- */

static int GenericGetItems(lua_State* l, int expected_argc,
                           const function<void (lua_State* l, int index)>& f) {
    int argc = lua_gettop(l);
    if (argc != expected_argc) {
        return -1;
    }

    if (!lua_istable(l, expected_argc)) {
        f(l, expected_argc);
        return 0;
    }

    lua_pushnil(l);
    while (lua_next(l, -2) != 0) {
        f(l, -1);
        lua_pop(l, 1);
    }

    return 0;
}

static int l_AddFlags(lua_State* l) {
    auto dep = *((Dependency**)lua_touserdata(l, 1));

    int ret = GenericGetItems(l, 2, [&dep] (lua_State* l, int index) {
        dep->AddFlag(lua_tostring(l, index));
    });
    if (ret == -1) {
        cerr << "AddFlags() takes exactly 1 argument: a flag or a table containing flag(s)." << endl;
    }

    lua_pushvalue(l, 1);
    return 1;
}

static int l_AddSourceFiles(lua_State* l) {
    auto dep = *((Dependency**)lua_touserdata(l, 1));

    int ret = GenericGetItems(l, 2, [&dep] (lua_State* l, int index) {
        dep->AddSourceFiles(lua_tostring(l, index));
    });
    if (ret == -1) {
        cerr << "AddSourceFiles() takes exactly 1 argument: a file or a table containing file(s)." << endl;
    }

    lua_pushvalue(l, 1);
    return 1;
}

static int l_AddStaticLibraries(lua_State* l) {
    auto dep = *((Dependency**)lua_touserdata(l, 1));

    int ret = GenericGetItems(l, 3, [&dep] (lua_State* l, int index) {
        dep->AddLibrary(lua_tostring(l, 2), lua_tostring(l, index), OMAKE_TYPE_STATIC);
    });
    if (ret == -1) {
        cerr << "AddStaticLibraries() requires 2 arguments: path and name(s)." << endl;
    }

    lua_pushvalue(l, 1);
    return 1;
}

static int l_AddSharedLibraries(lua_State* l) {
    auto dep = *((Dependency**)lua_touserdata(l, 1));

    int ret = GenericGetItems(l, 3, [&dep] (lua_State* l, int index) {
        dep->AddLibrary(lua_tostring(l, 2), lua_tostring(l, index), OMAKE_TYPE_SHARED);
    });
    if (ret == -1) {
        cerr << "AddSharedLibraries() requires 2 arguments: path and name(s)." << endl;
    }

    lua_pushvalue(l, 1);
    return 1;
}

static int l_AddSysLibraries(lua_State* l) {
    auto dep = *((Dependency**)lua_touserdata(l, 1));

    int ret = GenericGetItems(l, 2, [&dep] (lua_State* l, int index) {
        dep->AddLibrary(nullptr, lua_tostring(l, index), OMAKE_TYPE_SHARED);
    });
    if (ret == -1) {
        cerr << "AddSysLibraries() takes exactly 1 argument: a lib or a table containing lib(s)." << endl;
    }

    lua_pushvalue(l, 1);
    return 1;
}

static int l_AddIncludeDirectories(lua_State* l) {
    auto dep = *((Dependency**)lua_touserdata(l, 1));

    int ret = GenericGetItems(l, 2, [&dep] (lua_State* l, int index) {
        dep->AddIncludeDirectory(lua_tostring(l, index));
    });
    if (ret == -1) {
        cerr << "AddIncludeDirectories() takes exactly 1 argument: a dir or a table containing dir(s)." << endl;
    }

    lua_pushvalue(l, 1);
    return 1;
}

static int l_AddDependencies(lua_State* l) {
    auto target = *((Target**)lua_touserdata(l, 1));

    int ret = GenericGetItems(l, 2, [&target] (lua_State* l, int index) {
        auto d = *(Dependency**)lua_touserdata(l, index);
        target->AddDependency(d);
    });
    if (ret == -1) {
        cerr << "AddDependencies() takes exactly 1 argument: a dep or a table containing dep(s)." << endl;
    }

    lua_pushvalue(l, 1);
    return 1;
}

void InitLuaEnv(LuaState* l) {
    l->RegisterClass<Project>("Project")
        .SetConstructor()
        .Set("CreateBinary", &Project::CreateBinary)
        .Set("CreateStaticLibrary", &Project::CreateStaticLibrary)
        .Set("CreateSharedLibrary", &Project::CreateSharedLibrary)
        .Set("CreateDependency", &Project::CreateDependency);

    l->RegisterClass<Dependency>()
        .Set("AddFlags", l_AddFlags)
        .Set("AddSourceFiles", l_AddSourceFiles)
        .Set("AddStaticLibraries", l_AddStaticLibraries)
        .Set("AddSharedLibraries", l_AddSharedLibraries)
        .Set("AddSysLibraries", l_AddSysLibraries)
        .Set("AddIncludeDirectories", l_AddIncludeDirectories);

    l->RegisterClass<Target>()
        .Set("AddDependencies", l_AddDependencies);
}
