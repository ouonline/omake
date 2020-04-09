#include "project.h"
#include "utils.h"
#include "common.h"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <list>
#include <algorithm> // std::sort
#include <unistd.h> // access()
using namespace std;

#include "lua-cpp/luacpp.h"
using namespace luacpp;

Target* Project::CreateBinary(const char* name) {
    auto ret_pair = m_targets.insert(make_pair(name, nullptr));
    if (!ret_pair.second) {
        cerr << "duplcated target name[" << name << "]" << endl;
        return nullptr;
    }

    ret_pair.first->second = new Target(name, OMAKE_TYPE_BINARY);
    return ret_pair.first->second;
}

Target* Project::CreateStaticLibrary(const char* name) {
    auto ret_pair = m_targets.insert(make_pair(name, nullptr));
    if (!ret_pair.second) {
        cerr << "duplcated target name[" << name << "]" << endl;
        return nullptr;
    }

    ret_pair.first->second = new Target(name, OMAKE_TYPE_STATIC);
    return ret_pair.first->second;
}

Target* Project::CreateSharedLibrary(const char* name) {
    auto ret_pair = m_targets.insert(make_pair(name, nullptr));
    if (!ret_pair.second) {
        cerr << "duplcated target name[" << name << "]" << endl;
        return nullptr;
    }

    ret_pair.first->second = new Target(name, OMAKE_TYPE_SHARED);
    return ret_pair.first->second;
}

Dependency* Project::CreateDependency() {
    static const string dep_prefix = "omake_dep_";

    auto d = new Dependency(dep_prefix + std::to_string(m_dep_counter));
    ++m_dep_counter;
    return d;
}

Target* Project::FindTarget(const string& name) const {
    auto ref = m_targets.find(name);
    if (ref == m_targets.end()) {
        return nullptr;
    }

    return ref->second;
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

static inline bool IsSysLib(const LibInfo& lib) {
    return lib.path.empty();
}

static inline bool IsThirdPartyLib(const LibInfo& lib) {
    return (access((lib.path + "/omake.lua").c_str(), F_OK) != 0);
}

struct DepTreeNode {
    DepTreeNode(const string& _path, const string& _name, int _lib_type)
        : level(0), lib(_path, _name, _lib_type) {}
    DepTreeNode(const LibInfo& _lib) : level(0), lib(_lib) {}

    int level;
    LibInfo lib;
    unordered_set<string> inc_dirs;
    unordered_set<DepTreeNode*> deps;
};

static void GenerateDepTree(const Target* target,
                            unordered_map<LibInfo, DepTreeNode, LibInfoHash>* dep_tree) {
    list<DepTreeNode*> q;

    auto handle_lib = [&q, &dep_tree] (const LibInfo& lib, int parent_level) -> DepTreeNode* {
        auto ret_pair = dep_tree->insert(
            std::move(make_pair(lib, DepTreeNode(lib))));
        auto node = &ret_pair.first->second;

        if (ret_pair.second) {
            if (!IsSysLib(lib) && !IsThirdPartyLib(lib)) {
                q.push_back(node);
            }
        }

        int level = parent_level + 1;
        if (level > node->level) {
            node->level = level;
        }

        return node;
    };

    target->ForeachDependency([&handle_lib] (const Dependency* dep) -> bool {
        dep->ForeachLibrary([&handle_lib] (const LibInfo& lib) {
            handle_lib(lib, 0);
        });
        return true;
    });

    while (!q.empty()) {
        auto parent = q.front();
        q.pop_front();

        LuaState l;
        InitLuaEnv(&l);

        string errmsg;
        const string omake_file = parent->lib.path + "/omake.lua";
        bool ok = l.dofile(omake_file.c_str(), &errmsg, 1, [&handle_lib, &parent] (int, const LuaObject& obj) -> bool {
            auto project = obj.touserdata().object<Project>();
            auto target = project->FindTarget(parent->lib.name);
            if (!target) {
                return true;
            }

            target->ForeachDependency([&parent, &handle_lib] (const Dependency* dep) -> bool {
                dep->ForeachLibrary([&parent, &handle_lib] (const LibInfo& lib) {
                    string new_path;
                    if ((!lib.path.empty()) && lib.path[0] != '/') {
                        new_path = RemoveDotAndDotDot(parent->lib.path + "/" + lib.path);
                    } else {
                        new_path = lib.path;
                    }

                    auto node = handle_lib(LibInfo(new_path, lib.name, lib.type),
                                           parent->level);
                    parent->deps.insert(node);
                });

                dep->ForeachIncDir([&parent] (const string& inc) {
                    if (inc[0] == '/') {
                        parent->inc_dirs.insert(inc);
                    } else {
                        parent->inc_dirs.insert(
                            RemoveDotAndDotDot(parent->lib.path + "/" + inc));
                    }
                });

                return true;
            });

            return true;
        });
        if (!ok) {
            cerr << "Preprocessing dependency [" << omake_file << "] failed: "
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

static string GenerateTargetDepLibs(const Target* target,
                                    const unordered_map<LibInfo, DepTreeNode, LibInfoHash>& dep_tree) {
    string content;
    list<const DepTreeNode*> q;
    unordered_set<const DepTreeNode*> lib_dedup;

    target->ForeachDependency([&dep_tree, &lib_dedup, &q] (const Dependency* dep) -> bool {
        dep->ForeachLibrary([&dep_tree, &lib_dedup, &q] (const LibInfo& lib) {
            auto ref = dep_tree.find(lib);

            if (IsSysLib(lib)) {
                lib_dedup.insert(&ref->second);
                return;
            }

            auto ret_pair = lib_dedup.insert(&ref->second);
            if (ret_pair.second) {
                q.push_back(&ref->second);
            }
        });

        return true;
    });

    while (!q.empty()) {
        auto parent = q.front();
        q.pop_front();

        for (auto dep : parent->deps) {
            if (IsSysLib(dep->lib)) {
                lib_dedup.insert(dep);
                continue;
            }

            auto ret_pair = lib_dedup.insert(dep);
            if (ret_pair.second) {
                q.push_back(dep);
            }
        }
    }

    // sort dep_list according to level asc
    vector<const DepTreeNode*> dep_list;
    dep_list.insert(dep_list.end(), lib_dedup.begin(), lib_dedup.end());
    std::sort(dep_list.begin(), dep_list.end(),
              [] (const DepTreeNode* a, const DepTreeNode* b) -> bool {
                  if (a->lib.path.empty()) {
                      return false;
                  }
                  if (b->lib.path.empty()) { // sys lib should be at the end of dep_list list
                      return true;
                  }

                  return (a->level < b->level);
              });

    for (auto dep : dep_list) {
        const LibInfo& lib = dep->lib;
        if (lib.type == OMAKE_TYPE_STATIC) {
            content += " " + lib.path + "/lib" + lib.name + ".a";
        } else if (lib.type == OMAKE_TYPE_SHARED) {
            if (!IsSysLib(lib)) {
                content += " -L" + lib.path;
            }
            content += " -l" + lib.name;
        }
    }

    return content;
}

static string GenerateObjects(const Target* target) {
    string content;

    target->ForeachDependency([&content] (const Dependency* dep) -> bool {
        const string& dep_name = dep->GetName();

        dep->ForeachCSource([&content, &dep_name] (const string& src) {
            content += " " + src + "." + dep_name + ".o";
        });

        dep->ForeachCppSource([&content, &dep_name] (const string& src) {
            content += " " + src + "." + dep_name + ".o";
        });

        return true;
    });

    return content;
}

static void GenerateDepLabel(const unordered_map<LibInfo, DepTreeNode, LibInfoHash>& dep_tree,
                             unordered_map<const DepTreeNode*, string>* node2label) {
    const string label_prefix = "omake_phony_";

    for (auto it = dep_tree.begin(); it != dep_tree.end(); ++it) {
        if (!IsThirdPartyLib(it->first) && it->second.level == 1) {
            node2label->insert(
                make_pair(&it->second,
                          label_prefix + std::to_string(node2label->size())));
        }
    }
}

static string GenerateDepBuildInfo(const unordered_map<const DepTreeNode*, string>& node2label) {
    string content;

    if (!node2label.empty()) {
        content += ".PHONY:";
        for (auto iter : node2label) {
            if (iter.first->level == 1) {
                content += " " + iter.second;
            }
        }
        content += "\n\n";
    }

    for (auto iter : node2label) {
        if (iter.first->level != 1) {
            continue;
        }

        content += iter.second + ":\n";

        const string target_name =
            (iter.first->lib.type == OMAKE_TYPE_STATIC)
            ? ("lib" + iter.first->lib.name + ".a")
            : ("lib" + iter.first->lib.name + ".so");
        content += "\t$(MAKE) debug=$(debug) " + target_name +
            " -C " + iter.first->lib.path + "\n\n";
    }

    return content;
}

static string GenerateDepInc(const Dependency* dep,
                             const unordered_map<LibInfo, DepTreeNode, LibInfoHash>& dep_tree) {
    list<const DepTreeNode*> q;
    unordered_set<const DepTreeNode*> lib_dedup;
    unordered_set<string> inc_dedup;

    dep->ForeachIncDir([&inc_dedup] (const string& inc) {
        inc_dedup.insert(inc);
    });

    dep->ForeachLibrary([&lib_dedup, &q, &dep_tree] (const LibInfo& lib) {
        if (IsSysLib(lib)) {
            return;
        }

        auto ref = dep_tree.find(lib);
        auto ret_pair = lib_dedup.insert(&ref->second);
        if (ret_pair.second) {
            q.push_back(&ref->second);
        }
    });

    while (!q.empty()) {
        auto parent = q.front();
        q.pop_front();

        inc_dedup.insert(GetParentDir(parent->lib.path));
        for (auto inc : parent->inc_dirs) {
            inc_dedup.insert(inc);
        }

        for (auto dep : parent->deps) {
            if (IsSysLib(dep->lib)) {
                continue;
            }

            auto ret_pair = lib_dedup.insert(dep);
            if (ret_pair.second) {
                q.push_back(dep);
            }
        }
    }

    string content;
    for (auto inc : inc_dedup) {
        content += " -I" + inc;
    }
    return content;
}

static string GenerateObjBuildInfo(const Target* target,
                                   const unordered_map<LibInfo, DepTreeNode, LibInfoHash>& dep_tree,
                                   unordered_set<string>* obj_dedup) {
    string content;

    target->ForeachDependency([&dep_tree, &obj_dedup, &content] (const Dependency* dep) -> bool {
        const string& dep_name = dep->GetName();
        const string dep_inc_str = GenerateDepInc(dep, dep_tree);

        string flags;
        dep->ForeachFlag([&flags] (const string& flag) {
            flags += " " + flag;
        });

        string local_content;
        dep->ForeachCSource([&obj_dedup, &local_content, &dep_name, &flags, &dep_inc_str] (const string& src) {
            const string obj = src + "." + dep_name + ".o";
            auto ret_pair = obj_dedup->insert(obj);
            if (ret_pair.second) {
                local_content += obj + ": " + src + "\n" +
                    "\t$(CC) $(CFLAGS)" + flags;
                if (!dep_inc_str.empty()) {
                    local_content += " $(" + dep_name + "_INCLUDE)";
                }
                local_content += " -c $< -o $@\n\n";
            }
        });

        dep->ForeachCppSource([&obj_dedup, &local_content, &dep_name, &flags, &dep_inc_str] (const string& src) {
            const string obj = src + "." + dep_name + ".o";
            auto ret_pair = obj_dedup->insert(obj);
            if (ret_pair.second) {
                local_content += obj + ": " + src + "\n" +
                    "\t$(CXX) $(CXXFLAGS)" + flags;
                if (!dep_inc_str.empty()) {
                    local_content += " $(" + dep_name + "_INCLUDE)";
                }
                local_content += " -c $< -o $@\n\n";
            }

        });

        if (!local_content.empty()) {
            if (!dep_inc_str.empty()) {
                content += dep_name + "_INCLUDE :=" + dep_inc_str + "\n\n";
            }
            content += local_content;
        }

        return true;
    });

    return content;
}

static string GenerateTargetDepLabels(const Target* target,
                                      const unordered_map<LibInfo, DepTreeNode, LibInfoHash>& dep_tree,
                                      const unordered_map<const DepTreeNode*, string>& node2label) {
    string content;

    target->ForeachDependency([&dep_tree, &content, &node2label] (const Dependency* dep) -> bool {
        dep->ForeachLibrary([&dep_tree, &content, &node2label] (const LibInfo& lib) {
            auto dep_ref = dep_tree.find(lib);
            auto ref = node2label.find(&dep_ref->second);
            if (ref != node2label.end() && dep_ref->second.level == 1) {
                content += " " + ref->second;
            }
        });
        return true;
    });

    return content;
}

bool Project::GenerateMakefile(const string& fname) {
    string content = "# This Makefile is generated by omake: https://github.com/ouonline/omake.git\n\n";

    bool has_c = false, has_cpp = false;
    for (auto iter : m_targets) {
        if (iter.second->HasCSource()) {
            has_c = true;
        }
        if (iter.second->HasCppSource()) {
            has_cpp = true;
        }
        if (has_c && has_cpp) {
            break;
        }
    }

    if (has_c) {
        content += "CC := gcc\n"
            "\n"
            "ifeq ($(debug), y)\n"
            "\tCFLAGS += -g\n"
            "else\n"
            "\tCFLAGS += -O2 -DNDEBUG\n"
            "endif\n"
            "\n";
    }
    if (has_cpp) {
        content += "CXX := g++\n"
            "\n"
            "ifeq ($(debug), y)\n"
            "\tCXXFLAGS += -g\n"
            "else\n"
            "\tCXXFLAGS += -O2 -DNDEBUG\n"
            "endif\n"
            "\n";
    }

    for (auto iter : m_targets) {
        if (iter.second->GetType() == OMAKE_TYPE_STATIC) {
            content += "AR := ar\n\n";
            break;
        }
    }

    content += "TARGET :=";
    for (auto iter : m_targets) {
        content += " " + iter.second->GetGeneratedName();
    }
    content += "\n\n";

    content += ".PHONY: all clean\n"
        "\n"
        "all: $(TARGET)\n"
        "\n";

    // pre process for dependencies
    unordered_map<LibInfo, DepTreeNode, LibInfoHash> dep_tree;
    for (auto iter : m_targets) {
        GenerateDepTree(iter.second, &dep_tree);
    }

    unordered_map<const DepTreeNode*, string> node2label;
    GenerateDepLabel(dep_tree, &node2label);

    content += GenerateDepBuildInfo(node2label);

    unordered_set<string> obj_dedup;
    for (auto iter : m_targets) {
        auto target = iter.second;

        content += GenerateObjBuildInfo(target, dep_tree, &obj_dedup);

        content += target->GetName() + "_OBJS :=" + GenerateObjects(target) + "\n\n";


        string target_dep_libs;
        if (target->GetType() == OMAKE_TYPE_BINARY ||
            target->GetType() == OMAKE_TYPE_SHARED) {
            target_dep_libs = GenerateTargetDepLibs(target, dep_tree);
            if (!target_dep_libs.empty()) {
                content += target->GetName() + "_LIBS :=" + target_dep_libs + "\n\n";
            }
        }

        content += target->GetGeneratedName() + ": $(" + target->GetName() + "_OBJS)";

        const string dep_label_str = GenerateTargetDepLabels(target, dep_tree, node2label);
        if (!dep_label_str.empty()) {
            content += " |" + dep_label_str;
        }

        string cmd;
        if (target->GetType() == OMAKE_TYPE_STATIC) {
            cmd = "$(AR) rc $@ $^";
        } else {
            string extra_flags;
            target->GetDefaultDependency()->ForeachFlag([&extra_flags] (const string& flag) {
                extra_flags += " " + flag;
            });

            if (target->GetType() == OMAKE_TYPE_BINARY) {
                if (target->HasCppSource()) {
                    cmd = "$(CXX) $(CXXFLAGS)" + extra_flags +
                        " -o $@ $^";
                    if (!target_dep_libs.empty()) {
                        cmd += " $(" + target->GetName() + "_LIBS)";
                    }
                } else if (target->HasCSource()) {
                    cmd = "$(CC) $(CFLAGS)" + extra_flags +
                        " -o $@ $^";
                    if (!target_dep_libs.empty()) {
                        cmd += " $(" + target->GetName() + "_LIBS)";
                    }
                }
            } else if (target->GetType() == OMAKE_TYPE_SHARED) {
                if (target->HasCppSource()) {
                    cmd = "$(CXX) $(CXXFLAGS)" + extra_flags +
                        " -shared -o $@ $^";
                    if (!target_dep_libs.empty()) {
                        cmd += " $(" + target->GetName() + "_LIBS)";
                    }
                } else if (target->HasCSource()) {
                    cmd = "$(CC) $(CFLAGS)" + extra_flags +
                        " -shared -o $@ $^";
                    if (!target_dep_libs.empty()) {
                        cmd += " $(" + target->GetName() + "_LIBS)";
                    }
                }
            }
        }

        content += "\n\t" + cmd + "\n\n";
    }

    content += "clean:\n"
        "\trm -f $(TARGET)";
    for (auto iter : m_targets) {
        content += " $(" + iter.second->GetName() + "_OBJS)";
    }
    content += "\n";

    return WriteFile(fname, content);
}
