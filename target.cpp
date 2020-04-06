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
#include <unistd.h> // access()

using namespace std;
using namespace utils;
using namespace luacpp;

static string RemoveDotAndDotDot(const string& path) {
    vector<string> path_stack;

    TextSplit(path.data(), path.size(), "/", 1, [&path_stack] (const char* s, unsigned int l) -> bool {
        if (l == 0) {
            return true;
        }
        if (path_stack.empty()) {
            path_stack.emplace_back(string(s, l));
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
                path_stack.emplace_back(string(s, l));
            }
        }
        return true;
    });

    if (path_stack.empty()) {
        return string();
    }

    string new_path = path_stack[0];
    for (size_t i = 1; i < path_stack.size(); ++i) {
        new_path.append("/" + path_stack[i]);
    }
    return new_path;
}

void Target::AddDynamicLibrary(const char* path, const char* name) {
    LibInfo info(RemoveDotAndDotDot(path).c_str(), name);
    for (auto lib : m_dynamic_libs) {
        if (lib.path == info.path && lib.name == info.name) {
            return;
         }
    }
    m_dynamic_libs.emplace_back(info);
}

void Target::AddStaticLibrary(const char* path, const char* name) {
    LibInfo info(RemoveDotAndDotDot(path).c_str(), name);
    for (auto lib : m_static_libs) {
        if (lib.path == info.path && lib.name == info.name) {
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
    m_sys_libs.emplace_back(RemoveDotAndDotDot(s));
}

void Target::AddIncludeDirectory(const char* name) {
    const string s(name);
    for (auto lib : m_inc_dirs) {
        if (lib == s) {
            return;
        }
    }
    m_inc_dirs.emplace_back(RemoveDotAndDotDot(s));
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
    string content;
    set<string> dedup;

    for (auto inc : m_inc_dirs) {
        auto ret_pair = dedup.insert(inc);
        if (ret_pair.second) {
            content += " -I" + inc;
        }
    }

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

    for (size_t i = 0; i < m_static_libs.size(); ++i) {
        const LibInfo& lib = m_static_libs[i];
        content += " " + lib.path + "/lib" + lib.name + ".a";
        if (i < m_static_libs.size() - 1) {
            content += " \\\n";
        }
    }

    for (size_t i = 0; i < m_dynamic_libs.size(); ++i) {
        const LibInfo& lib = m_dynamic_libs[i];
        content += " -L" + lib.path + " -l" + lib.name;
        if (i < m_dynamic_libs.size() - 1) {
            content += " \\\n";
        }
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
        auto ret_pair = user_libs_dedup.insert(make_pair(lib.path, lib.name));
        if (ret_pair.second) {
            q.push(lib);
        }
    }

    for (auto lib : m_dynamic_libs) {
        auto ret_pair = user_libs_dedup.insert(make_pair(lib.path, lib.name));
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
                auto ret_pair = user_libs_dedup.insert(make_pair(it.path, it.name));
                if (ret_pair.second) {
                    auto tmp_lib_info = it;
                    if (it.path[0] == '/') {
                        AddStaticLibrary(it.path.c_str(), it.name.c_str());
                    } else {
                        tmp_lib_info.path = RemoveDotAndDotDot(lib.path + "/" + it.path);
                        AddStaticLibrary(tmp_lib_info.path.c_str(), it.name.c_str());
                    }
                    q.push(tmp_lib_info);
                }
            }

            for (auto it : target->GetDynamicLibraries()) {
                auto ret_pair = user_libs_dedup.insert(make_pair(it.path, it.name));
                if (ret_pair.second) {
                    auto tmp_lib_info = it;
                    if (it.path[0] == '/') {
                        AddDynamicLibrary(it.path.c_str(), it.name.c_str());
                    } else {
                        tmp_lib_info.path = RemoveDotAndDotDot(lib.path + "/" + it.path);
                        AddDynamicLibrary(tmp_lib_info.path.c_str(), it.name.c_str());
                    }
                    q.push(tmp_lib_info);
                }
            }

            for (auto it : target->GetSystemDynamicLibraries()) {
                AddSystemDynamicLibrary(it.c_str());
            }

            for (auto dirpath : target->GetIncludeDirectories()) {
                if (dirpath[0] == '/') {
                    AddIncludeDirectory(dirpath.c_str());
                } else {
                    AddIncludeDirectory((lib.path + "/" + dirpath).c_str());
                }
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

void BinaryTarget::ForeachGeneratedNameAndCommand(
    const function<void (const string&, const string&)>& func) const {
    string cmd;
    if (m_cpp_sources.empty()) {
        cmd = "$(CC) $(CFLAGS) -o $@ $^ $(" + m_name + "_LIBS)";
    } else {
        cmd = "$(CXX) $(CXXFLAGS) -o $@ $^ $(" + m_name + "_LIBS)";
    }
    func(m_name, cmd);
}

/* -------------------------------------------------------------------------- */

LibraryTarget::LibraryTarget(const char* name, int type)
    : Target(name), m_type(type) {}

void LibraryTarget::ForeachGeneratedNameAndCommand(
    const function<void (const string&, const string&)>& func) const {
    if (m_type & LIBRARY_TYPE_STATIC) {
        func("lib" + m_name + ".a", "$(AR) rc $@ $^");
    }
    if (m_type & LIBRARY_TYPE_SHARED) {
        string cmd;
        if (m_cpp_sources.empty()) {
            cmd = "$(CC) -shared -o $@ $^ $(" + m_name + "_LIBS)";
        } else {
            cmd = "$(CXX) -shared -o $@ $^ $(" + m_name + "_LIBS)";
        }
        func("lib" + m_name + ".so", cmd);
    }
}
