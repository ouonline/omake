#include "target.h"
#include "project.h"
#include "misc.h"
#include "text-utils/text_utils.h"
#include "lua-cpp/luacpp.h"
#include <iostream>
#include <cstring>
#include <set>
#include <queue>

#include <sys/types.h>
#include <dirent.h>
#include <limits.h> // PATH_MAX
#include <stdlib.h> // realpath()
#include <unistd.h> // access()

using namespace std;
using namespace utils;
using namespace luacpp;

void Target::AddDynamicLibrary(const char* path, const char* name) {
    char abs_path[PATH_MAX];
    realpath(path, abs_path);

    LibInfo info(path, name, abs_path);
    for (auto lib : m_dynamic_libs) {
        if (lib.abs_path == info.abs_path && lib.name == info.name) {
            return;
         }
    }
    m_dynamic_libs.emplace_back(info);
}

void Target::AddStaticLibrary(const char* path, const char* name) {
    char abs_path[PATH_MAX];
    realpath(path, abs_path);

    LibInfo info(path, name, abs_path);
    for (auto lib : m_static_libs) {
        if (lib.abs_path == info.abs_path && lib.name == info.name) {
            return;
        }
    }
    m_static_libs.emplace_back(info);
}

void Target::AddSystemDynamicLibrary(const char* name) {
    const string s(name);
    for (auto lib : m_sys_libs) {
        if (lib == s) {
            return;
        }
    }
    m_sys_libs.emplace_back(s);
}

static void FindFileEndsWith(const string& dirname, const char* suffix,
                               std::vector<std::string>* file_list) {
    DIR* dirp = opendir(dirname.c_str());
    if (!dirp) {
        return;
    }

    struct dirent* dentry;
    const int slen = strlen(suffix);
    while ((dentry = readdir(dirp))) {
        int dlen = strlen(dentry->d_name);
        if (TextEndsWith(dentry->d_name, dlen, suffix, slen)) {
            file_list->push_back(dirname + "/" + string(dentry->d_name, dlen));
        }
    }

    closedir(dirp);
}

static int FindParentDirPos(const char* fpath) {
    int plen = strlen(fpath) - 1;
    while (plen >= 0) {
        if (fpath[plen] == '/') {
            return plen;
        }
        --plen;
    }
    return -1;
}

void Target::AddSourceFile(const char* fpath) {
    string parent_dir;
    const char* fname = nullptr;

    int offset = FindParentDirPos(fpath);
    if (offset >= 0) {
        parent_dir.assign(fpath, offset + 1);
        fname = fpath + offset + 1;
    } else {
        parent_dir = ".";
        fname = fpath;
    }

    int flen = strlen(fname);

    if (strcmp(fname, "*.cpp") == 0) {
        FindFileEndsWith(parent_dir, ".cpp", &m_cpp_sources);
    } else if (strcmp(fname, "*.c") == 0) {
        FindFileEndsWith(parent_dir, ".c", &m_c_sources);
    } else if (strcmp(fname, "*.cc") == 0) {
        FindFileEndsWith(parent_dir, ".cc", &m_cpp_sources);
    } else {
        if (TextEndsWith(fname, flen, ".cpp", 4) ||
            TextEndsWith(fname, flen, ".cc", 3)) {
            m_cpp_sources.push_back(fpath);
        } else if (TextEndsWith(fname, flen, ".c", 2)) {
            m_c_sources.push_back(fpath);
        }
    }
}

static string GetParentDir(const string& path) {
    if (path == ".." || path == "../") {
        return "../..";
    }
    if (path == "." || path == "./") {
        return "..";
    }

    for (int i = path.size() - 1; i >= 0; --i) {
        if (path[i] == '/') {
            if (i > 0) {
                --i;
            }
            return path.substr(0, i + 1);
        }
    }
    return path;
}

const string Target::GetIncludeClause() const {
    set<string> dedup;

    string content;
    for (auto lib : m_static_libs) {
        auto tmp_path = GetParentDir(lib.path);
        auto ret_pair = dedup.insert(tmp_path);
        if (ret_pair.second) {
            content += " -I" + tmp_path;
        }
    }
    for (auto lib : m_dynamic_libs) {
        auto tmp_path = GetParentDir(lib.path);
        auto ret_pair = dedup.insert(tmp_path);
        if (ret_pair.second) {
            content += " -I" + tmp_path;
        }
    }

    return content;
}

const string Target::GetLibClause() const {
    string content;
    for (auto lib : m_static_libs) {
        content += " " + lib.path + "/lib" + lib.name + ".a";
    }
    for (auto lib : m_dynamic_libs) {
        content += " -L" + lib.path + " -l" + lib.name;
    }
    for (auto lib : m_sys_libs) {
        content += " -l" + lib;
    }
    return content;
}

bool Target::Finalize() {
    queue<LibInfo> q;
    set<pair<string, string>> user_libs_dedup;

    for (auto lib : m_static_libs) {
        auto ret_pair = user_libs_dedup.insert(make_pair(lib.abs_path, lib.name));
        if (ret_pair.second) {
            q.push(lib);
        }
    }

    for (auto lib : m_dynamic_libs) {
        auto ret_pair = user_libs_dedup.insert(make_pair(lib.abs_path, lib.name));
        if (ret_pair.second) {
            q.push(lib);
        }
    }

    while (!q.empty()) {
        auto lib = q.front();
        q.pop();

        const string omake_file = lib.path + "/omake.lua";
        if (access(omake_file.c_str(), F_OK) != 0) {
            continue;
        }

        LuaState l;
        InitLuaEnv(&l);

        string errmsg;
        bool ok = l.dofile(omake_file.c_str(), &errmsg, 1, [this, &lib, &q, &user_libs_dedup] (int, const LuaObject& obj) -> bool {
            auto project = obj.touserdata().object<Project>();
            auto target = project->GetTarget(lib.name);
            if (!target) {
                return true;
            }

            for (auto it : target->GetStaticLibraries()) {
                auto ret_pair = user_libs_dedup.insert(make_pair(it.abs_path, it.name));
                if (ret_pair.second) {
                    auto tmp_lib_info = it;
                    if (it.path[0] == '/') {
                        AddStaticLibrary(it.path.c_str(), it.name.c_str());
                    } else {
                        tmp_lib_info.path = lib.path + "/" + it.path;
                        AddStaticLibrary(tmp_lib_info.path.c_str(), it.name.c_str());
                    }
                    q.push(tmp_lib_info);
                }
            }

            for (auto it : target->GetDynamicLibraries()) {
                auto ret_pair = user_libs_dedup.insert(make_pair(it.abs_path, it.name));
                if (ret_pair.second) {
                    auto tmp_lib_info = it;
                    if (it.path[0] == '/') {
                        AddDynamicLibrary(it.path.c_str(), it.name.c_str());
                    } else {
                        tmp_lib_info.path = lib.path + "/" + it.path;
                        AddDynamicLibrary(tmp_lib_info.path.c_str(), it.name.c_str());
                    }
                    q.push(tmp_lib_info);
                }
            }

            for (auto it : target->GetSystemDynamicLibraries()) {
                AddSystemDynamicLibrary(it.c_str());
            }

            return true;
        });
        if (!ok) {
            cerr << "Preprocessing dependency[" << omake_file << "] failed: "
                 << errmsg << endl;
            return false;
        }
    }

    return true;
}

/* -------------------------------------------------------------------------- */

void BinaryTarget::ForeachTargetAndCommand(
    const function<void (const string& target,
                         const string& command)>& f) const {
    string cmd;
    if (m_cpp_sources.empty()) {
        cmd = "$(CC) $(CFLAGS) -o $@ $^ $(" + m_name + "_LIBS)";
    } else {
        cmd = "$(CXX) $(CXXFLAGS) -o $@ $^ $(" + m_name + "_LIBS)";
    }

    f(m_name, cmd);
}

/* -------------------------------------------------------------------------- */

void LibraryTarget::ForeachTargetAndCommand(
    const function<void (const string& target,
                         const string& command)>& f) const {
    f("lib" + m_name + ".a", "$(AR) rc $@ $^");
    if (m_cpp_sources.empty()) {
        f("lib" + m_name + ".so", "$(CC) -shared -o $@ $^ $(" + m_name + "_LIBS)");
    } else {
        f("lib" + m_name + ".so", "$(CXX) -shared -o $@ $^ $(" + m_name + "_LIBS)");
    }
}
