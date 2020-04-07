#include "project.h"
#include "target.h"
#include "misc.h"
#include "text-utils/text_utils.h"
using namespace luacpp;
using namespace std;
using namespace utils;

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

static Project* CreateProject() {
    return new Project();
}

static int l_AddIncludeDirectories(lua_State* l) {
    int argc = lua_gettop(l);
    auto target = (Target**)lua_touserdata(l, 1);
    for (int i = 2; i <= argc; ++i) {
        (*target)->AddIncludeDirectory(lua_tostring(l, i));
    }
    return 0;
}

static int l_AddSysLibraries(lua_State* l) {
    int argc = lua_gettop(l);
    auto target = (Target**)lua_touserdata(l, 1);
    for (int i = 2; i <= argc; ++i) {
        (*target)->AddLibrary(nullptr, lua_tostring(l, i), LIBRARY_TYPE_SHARED);
    }
    return 0;
}

static int l_AddSourceFiles(lua_State* l) {
    int argc = lua_gettop(l);
    auto target = (Target**)lua_touserdata(l, 1);
    for (int i = 2; i <= argc; ++i) {
        (*target)->AddSourceFiles(lua_tostring(l, i));
    }
    return 0;
}

void InitLuaEnv(LuaState* l) {
    l->newfunction(CreateProject, "CreateProject");

    l->set("STATIC", LIBRARY_TYPE_STATIC);
    l->set("SHARED", LIBRARY_TYPE_SHARED);

    l->newclass<Project>("Project")
        .set("CreateBinary", &Project::CreateBinary)
        .set("CreateLibrary", &Project::CreateLibrary);

    l->newclass<Target>("Target")
        .set("AddSourceFiles", l_AddSourceFiles)
        .set("AddLibrary", &Target::AddLibrary)
        .set("AddSysLibraries", l_AddSysLibraries)
        .set("AddIncludeDirectories", l_AddIncludeDirectories);
}
