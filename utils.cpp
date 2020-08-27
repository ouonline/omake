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

static int GenericGetItems(lua_State* l, const function<void (lua_State* l, int index)>& f) {
    int argc = lua_gettop(l);
    if (argc != 2) {
        return -1;
    }

    if (!lua_istable(l, 2)) {
        f(l, 2);
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
    // push flags in written order
    vector<string> flags;
    int ret = GenericGetItems(l, [&flags] (lua_State* l, int index) {
        flags.push_back(lua_tostring(l, index));
    });

    if (ret == -1) {
        cerr << "AddFlags() takes exactly 1 argument: a flag or a table containing flag(s)." << endl;
    } else {
        auto dep = *((Dependency**)lua_touserdata(l, 1));
        for (auto it = flags.rbegin(); it != flags.rend(); ++it) {
            dep->AddFlag(it->c_str());
        }
    }

    lua_pushvalue(l, 1);
    return 1;
}

static int l_AddSourceFiles(lua_State* l) {
    // push files in written order
    vector<string> files;
    int ret = GenericGetItems(l, [&files] (lua_State* l, int index) {
        files.push_back(lua_tostring(l, index));
    });

    if (ret == -1) {
        cerr << "AddSourceFiles() takes exactly 1 argument: a file or a table containing file(s)." << endl;
    } else {
        auto dep = *((Dependency**)lua_touserdata(l, 1));
        for (auto it = files.rbegin(); it != files.rend(); ++it) {
            dep->AddSourceFiles(it->c_str());
        }
    }

    lua_pushvalue(l, 1);
    return 1;
}

static int l_AddStaticLibrary(lua_State* l) {
    int argc = lua_gettop(l);
    if (argc != 3) {
        cerr << "AddStaticLibrary() requires 2 arguments: `path` and `name`." << endl;
        lua_pushvalue(l, 1);
        return 1;
    }

    auto dep = *((Dependency**)lua_touserdata(l, 1));
    dep->AddLibrary(lua_tostring(l, 2), // path
                    lua_tostring(l, 3), // name
                    OMAKE_TYPE_STATIC);

    lua_pushvalue(l, 1);
    return 1;
}

static int l_AddSharedLibrary(lua_State* l) {
    int argc = lua_gettop(l);
    if (argc != 3) {
        cerr << "AddSharedLibrary() requires 2 arguments: `path` and `name`." << endl;
        lua_pushvalue(l, 1);
        return 1;
    }

    auto dep = *((Dependency**)lua_touserdata(l, 1));
    dep->AddLibrary(lua_tostring(l, 2), // path
                    lua_tostring(l, 3), // name
                    OMAKE_TYPE_SHARED);

    lua_pushvalue(l, 1);
    return 1;
}

static int l_AddSysLibraries(lua_State* l) {
    // push libs in written order
    vector<string> libs;
    int ret = GenericGetItems(l, [&libs] (lua_State* l, int index) {
        libs.push_back(lua_tostring(l, index));
    });

    if (ret == -1) {
        cerr << "AddSysLibraries() takes exactly 1 argument: a lib or a table containing lib(s)." << endl;
    } else {
        auto dep = *((Dependency**)lua_touserdata(l, 1));
        for (auto it = libs.rbegin(); it != libs.rend(); ++it) {
            dep->AddLibrary(nullptr, it->c_str(), OMAKE_TYPE_SHARED);
        }
    }

    lua_pushvalue(l, 1);
    return 1;
}

static int l_AddIncludeDirectories(lua_State* l) {
    // push dirs in written order
    vector<string> dirs;
    int ret = GenericGetItems(l, [&dirs] (lua_State* l, int index) {
        dirs.push_back(lua_tostring(l, index));
    });

    if (ret == -1) {
        cerr << "AddIncludeDirectories() takes exactly 1 argument: a dir or a table containing dir(s)." << endl;
    } else {
        auto dep = *((Dependency**)lua_touserdata(l, 1));
        for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) {
            dep->AddIncludeDirectory(it->c_str());
        }
    }

    lua_pushvalue(l, 1);
    return 1;
}

static int l_AddDependencies(lua_State* l) {
    auto target = *((Target**)lua_touserdata(l, 1));
    int ret = GenericGetItems(l, [&target] (lua_State* l, int index) {
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
        .Set("AddStaticLibrary", l_AddStaticLibrary)
        .Set("AddSharedLibrary", l_AddSharedLibrary)
        .Set("AddSysLibraries", l_AddSysLibraries)
        .Set("AddIncludeDirectories", l_AddIncludeDirectories);

    l->RegisterClass<Target>()
        .Set("AddDependencies", l_AddDependencies);
}
