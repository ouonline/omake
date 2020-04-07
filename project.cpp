#include "project.h"
#include "misc.h"
#include <iostream>
#include <fstream>
#include <set>
#include <unordered_map>
#include <list>
#include <algorithm> // std::sort
#include <unistd.h> // access()
using namespace std;

#include "lua-cpp/luacpp.h"
using namespace luacpp;

Target* Project::CreateBinary(const char* name) {
    auto t = new BinaryTarget(name);
    m_targets.push_back(t);
    return t;
}

Target* Project::CreateLibrary(const char* name, int type) {
    if ((!(type & LIBRARY_TYPE_SHARED)) && (!(type & LIBRARY_TYPE_STATIC))) {
        cerr << "CreateLibrary() MUST specify at least one of `STATIC` and `SHARED`." << endl;
        return nullptr;
    }

    auto t = new LibraryTarget(name, type);
    m_targets.push_back(t);
    return t;
}

Target* Project::GetTarget(const string& name) const {
    for (auto target : m_targets) {
        if (name == target->GetName()) {
            return target;
        }
    }
    return nullptr;
}

static bool WriteFile(const string& fname, const string& content) {
    ofstream ofs;
    ofs.open(fname, ios_base::out | ios_base::trunc);
    if (!ofs.is_open()) {
        return false;
    }

    ofs.write(content.data(), content.size());
    ofs.close();
    return true;
}

struct DepTreeNode {
    DepTreeNode(const string& _path, const string& _name, int _lib_type)
        : info(_path, _name, _lib_type), level(0) {}
    LibInfo info;
    int level;
    string label;
    list<DepTreeNode*> dep_list;
    vector<string> inc_dirs;
};

struct LibInfoHash final {
    uint64_t operator () (const LibInfo& info) const {
        return std::hash<string>()(info.path) +
            std::hash<string>()(info.name) +
            info.type;
    }
};

static inline bool HasOMake(const string& dirname) {
    return (access((dirname + "/omake.lua").c_str(), F_OK) == 0);
}

static void GenerateDepTree(const Target* target,
                            unordered_map<LibInfo, DepTreeNode, LibInfoHash>* dep_tree) {
    const vector<LibInfo>& libs = target->GetLibraries();
    if (libs.empty()) {
        return;
    }

    list<DepTreeNode*> q;
    const string dep_prefix = "__omake_dep__";

    for (auto lib : libs) {
        DepTreeNode node(lib.path, lib.name, lib.type);
        auto ret_pair = dep_tree->insert(make_pair(node.info, node));
        if (ret_pair.second) {
            if (HasOMake(lib.path)) {
                ret_pair.first->second.label = dep_prefix + std::to_string(dep_tree->size());
                q.push_back(&(ret_pair.first->second));
            }
        }
    }

    while (!q.empty()) {
        auto dep = q.front();
        q.pop_front();

        // stop parsing dep_list if `omake.lua` is not found
        const string omake_file = dep->info.path + "/omake.lua";

        LuaState l;
        InitLuaEnv(&l);

        string errmsg;
        bool ok = l.dofile(omake_file.c_str(), &errmsg, 1, [&dep, &q, &dep_prefix, &dep_tree] (int, const LuaObject& obj) -> bool {
            auto project = obj.touserdata().object<Project>();
            auto target = project->GetTarget(dep->info.name);
            if (!target) {
                return true;
            }

            for (auto it : target->GetLibraries()) {
                string new_path;
                if ((!it.path.empty()) && it.path[0] != '/') {
                    new_path = RemoveDotAndDotDot(dep->info.path + "/" + it.path);
                } else {
                    new_path = it.path;
                }

                const DepTreeNode dep_node(new_path, it.name, it.type);
                auto ret_pair = dep_tree->insert(make_pair(dep_node.info, dep_node));
                if (ret_pair.second) {
                    ret_pair.first->second.level = dep->level + 1;
                    if (HasOMake(dep_node.info.path)) {
                        ret_pair.first->second.label = dep_prefix + std::to_string(dep_tree->size());
                        q.push_back(&(ret_pair.first->second));
                    }
                } else {
                    int level = dep->level + 1;
                    if (level > ret_pair.first->second.level) { // deeper level
                        ret_pair.first->second.level = level;
                    }
                }
                dep->dep_list.push_back(&(ret_pair.first->second));
            }

            for (auto dirpath : target->GetIncludeDirectories()) {
                if (dirpath[0] == '/') {
                    dep->inc_dirs.push_back(dirpath);
                } else {
                    dep->inc_dirs.push_back(RemoveDotAndDotDot(dep->info.path + "/" + dirpath));
                }
            }

            return true;
        });
        if (!ok) {
            cerr << "Preprocessing dependency[" << omake_file << "] failed: "
                 << errmsg << endl;
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

static bool DepExists(const vector<const DepTreeNode*>& dep_list, const LibInfo& info) {
    for (auto it = dep_list.begin(); it != dep_list.end(); ++it) {
        const DepTreeNode* node = *it;
        if (node->info == info) {
            return true;
        }
    }

    return false;
}

static void GenerateIncAndLibs(const Target* target,
                               const unordered_map<LibInfo, DepTreeNode, LibInfoHash>& dep_tree,
                               string* inc_clause, string* lib_clause) {
    set<string> inc_dedup;

    for (auto inc : target->GetIncludeDirectories()) {
        inc_dedup.insert(inc);
    }

    list<const DepTreeNode*> q;
    vector<const DepTreeNode*> dep_list;

    for (auto lib : target->GetLibraries()) {
        auto ref = dep_tree.find(lib);

        if (!lib.path.empty()) {
            inc_dedup.insert(GetParentDir(lib.path));
        }

        for (auto inc : ref->second.inc_dirs) {
            if (inc[0] != '/') {
                inc_dedup.insert(RemoveDotAndDotDot(lib.path + "/" + inc));
            }
        }

        if (!DepExists(dep_list, lib)) {
            dep_list.push_back(&ref->second);
            q.push_back(&ref->second);
        }
    }

    while (!q.empty()) {
        auto dep = q.front();
        q.pop_front();

        for (auto sub_dep : dep->dep_list) {
            if (!sub_dep->info.path.empty()) {
                inc_dedup.insert(GetParentDir(sub_dep->info.path));
            }

            for (auto inc : sub_dep->inc_dirs) {
                if (inc[0] != '/') {
                    inc_dedup.insert(RemoveDotAndDotDot(sub_dep->info.path + "/" + inc));
                }
            }

            if (!DepExists(dep_list, sub_dep->info)) {
                dep_list.push_back(sub_dep);
                q.push_back(sub_dep);
            }
        }
    }

    // sort dep_list according to level desc
    std::sort(dep_list.begin(), dep_list.end(),
              [] (const DepTreeNode* a, const DepTreeNode* b) -> bool {
                  if (a->info.path.empty()) {
                      return false;
                  }
                  if (b->info.path.empty()) { // sys lib should be at the end of dep_list list
                      return true;
                  }

                  return (a->level < b->level);
              });

    for (auto inc : inc_dedup) {
        inc_clause->append(" -I" + inc);
    }

    for (auto dep : dep_list) {
        const LibInfo& lib = dep->info;
        if (lib.type == LIBRARY_TYPE_STATIC) {
            lib_clause->append(" " + lib.path + "/lib" + lib.name + ".a");
        } else {
            if (!lib.path.empty()) {
                lib_clause->append(" -L" + lib.path);
            }
            lib_clause->append(" -l" + lib.name);
        }
    }
}

static string GenerateDepBuildInfo(const unordered_map<LibInfo, DepTreeNode, LibInfoHash>& dep_tree) {
    string content;
    if (dep_tree.empty()) {
        return content;
    }

    vector<const DepTreeNode*> dep_targets;
    for (auto it = dep_tree.begin(); it != dep_tree.end(); ++it) {
        if ((!it->second.label.empty()) && it->second.level == 0) {
            dep_targets.push_back(&it->second);
        }
    }

    if (!dep_targets.empty()) {
        content += ".PHONY:";
        for (auto dep : dep_targets) {
            content += " " + dep->label;
        }
        content += "\n\n";
    }

    for (auto target : dep_targets) {
        content += target->label + ":\n";

        const string target_name =
            (target->info.type == LIBRARY_TYPE_STATIC)
            ? ("lib" + target->info.name + ".a")
            : ("lib" + target->info.name + ".so");
        content += "\t$(MAKE) debug=$(debug) " + target_name + " -C " + target->info.path + "\n\n";
    }

    return content;
}

bool Project::GenerateMakefile(const string& fname) {
    string content = "# This Makefile is generated by omake: https://github.com/ouonline/omake.git\n\n";

    bool has_c = false, has_cpp = false;
    for (auto target : m_targets) {
        if (!target->GetCppSources().empty()) {
            has_cpp = true;
        }
        if (!target->GetCSources().empty()) {
            has_c = true;
        }
    }

    content += "AR := ar\n";
    if (has_c) {
        content += "CC := gcc\n"
            "\n"
            "ifeq ($(debug), y)\n"
            "\tCFLAGS := -g\n"
            "else\n"
            "\tCFLAGS := -O2 -DNDEBUG\n"
            "endif\n"
            "CFLAGS := $(CFLAGS) -Wall -Werror -Wextra -fPIC\n"
            "\n";
    }
    if (has_cpp) {
        content += "CXX := g++\n"
            "\n"
            "ifeq ($(debug), y)\n"
            "\tCXXFLAGS := -g\n"
            "else\n"
            "\tCXXFLAGS := -O2 -DNDEBUG\n"
            "endif\n"
            "CXXFLAGS := $(CXXFLAGS) -Wall -Werror -Wextra -fPIC\n"
            "\n";
    }

    content += "TARGET :=";
    for (auto target : m_targets) {
        target->ForeachGeneratedNameAndCommand(
            [&content] (const string& name, const string&) {
                content += " " + name;
            });
    }
    content += "\n\n";

    content += ".PHONY: all clean\n"
        "\n"
        "all: $(TARGET)\n"
        "\n";

    // pre process for dependencies
    unordered_map<LibInfo, DepTreeNode, LibInfoHash> dep_tree;
    for (auto target : m_targets) {
        GenerateDepTree(target, &dep_tree);
    }

    content += GenerateDepBuildInfo(dep_tree);

    for (auto target : m_targets) {
        vector<pair<string, string>> target_cpp_obj_files;
        vector<pair<string, string>> target_c_obj_files;

        const string obj_name = target->GetName() + "_OBJS";
        content += obj_name + " :=";
        for (auto src : target->GetCppSources()) {
            const string obj_name = src + "." + target->GetName() + ".o";
            target_cpp_obj_files.push_back(make_pair(obj_name, src));
            content += " " + obj_name;
        }
        for (auto src : target->GetCSources()) {
            const string obj_name = src + "." + target->GetName() + ".o";
            target_c_obj_files.push_back(make_pair(obj_name, src));
            content += " " + obj_name;
        }
        content += "\n\n";

        string inc_clause, lib_clause;
        GenerateIncAndLibs(target, dep_tree, &inc_clause, &lib_clause);
        content += target->GetName() + "_INCLUDE :=" + inc_clause +
            "\n\n" +
            target->GetName() + "_LIBS :=" + lib_clause +
            "\n\n";

        for (auto obj : target_cpp_obj_files) {
            content += obj.first + ": " + obj.second + "\n" +
                "\t$(CXX) $(CXXFLAGS) $(" + target->GetName() + "_INCLUDE) -c $< -o $@\n\n";
        }

        for (auto obj : target_c_obj_files) {
            content += obj.first + ": " + obj.second + "\n" +
                "\t$(CC) $(CFLAGS) $(" + target->GetName() + "_INCLUDE) -c $< -o $@\n\n";
        }

        target->ForeachGeneratedNameAndCommand([&target, &dep_tree, &content] (const string& name, const string& cmd) {
            content += name + ": $(" + target->GetName() + "_OBJS)";

            vector<string> dep_labels;
            for (auto lib : target->GetLibraries()) {
                auto ref = dep_tree.find(lib);
                if (!ref->second.label.empty() && ref->second.level == 0) {
                    dep_labels.push_back(ref->second.label);
                }
            }

            if (!dep_labels.empty()) {
                content += " |";
                for (auto label : dep_labels) {
                    content += " " + label;
                }
            }

            content += "\n"
                "\t" + cmd + "\n\n";
        });
    }

    content += "clean:\n"
        "\trm -f $(TARGET)";
    for (auto target : m_targets) {
        content += " $(" + target->GetName() + "_OBJS)";
    }
    content += "\n";

    return WriteFile(fname, content);
}
