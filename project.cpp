#include "project.h"
#include "utils.h"
#include "common.h"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <list>
#include <unistd.h> // access()
using namespace std;

#include "lua-cpp/luacpp.h"
using namespace luacpp;

Target* Project::CreateBinary(const char* name) {
    auto ret_pair = m_targets.insert(std::move(make_pair(name, nullptr)));
    if (!ret_pair.second) {
        cerr << "duplcated target name[" << name << "]" << endl;
        return nullptr;
    }

    ret_pair.first->second = new Target(name, OMAKE_TYPE_BINARY);
    return ret_pair.first->second;
}

Target* Project::CreateStaticLibrary(const char* name) {
    auto ret_pair = m_targets.insert(std::move(make_pair(name, nullptr)));
    if (!ret_pair.second) {
        cerr << "duplcated target name[" << name << "]" << endl;
        return nullptr;
    }

    ret_pair.first->second = new Target(name, OMAKE_TYPE_STATIC);
    return ret_pair.first->second;
}

Target* Project::CreateSharedLibrary(const char* name) {
    auto ret_pair = m_targets.insert(std::move(make_pair(name, nullptr)));
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

static inline bool IsLocalLib(const LibInfo& lib) {
    return (lib.path == ".");
}

struct DepTreeNode {
    DepTreeNode(const string& _path, const string& _name, int _lib_type)
        : lib(_path, _name, _lib_type) {}
    DepTreeNode(const LibInfo& _lib) : lib(_lib) {}

    LibInfo lib;
    unordered_set<string> inc_dirs;
    vector<const DepTreeNode*> deps; // keep order of insertion
};

static bool InsertDepNode(const DepTreeNode* dep, vector<const DepTreeNode*>* dep_list) {
    for (auto ddd : *dep_list) {
        if (ddd == dep) {
            return false;
        }
    }

    dep_list->push_back(dep);
    return true;
}

static void GenerateDepTree(const Target* target,
                            unordered_map<LibInfo, DepTreeNode, LibInfoHash>* dep_tree) {
    list<DepTreeNode*> q;

    auto handle_lib = [&q, &dep_tree] (const LibInfo& lib) -> DepTreeNode* {
        auto ret_pair = dep_tree->insert(
            std::move(make_pair(lib, DepTreeNode(lib))));
        auto node = &ret_pair.first->second;

        if (ret_pair.second) {
            if (!IsSysLib(lib) && !IsThirdPartyLib(lib)) {
                q.push_back(node);
            }
        }

        return node;
    };

    target->ForEachDependency([&handle_lib] (const Dependency* dep) {
        dep->ForEachLibrary([&handle_lib] (const LibInfo& lib) {
            handle_lib(lib);
        });
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

            target->ForEachDependency([&parent, &handle_lib] (const Dependency* dep) {
                dep->ForEachLibrary([&parent, &handle_lib] (const LibInfo& lib) {
                    string new_path;
                    if ((!lib.path.empty()) && lib.path[0] != '/') {
                        new_path = RemoveDotAndDotDot(parent->lib.path + "/" + lib.path);
                    } else {
                        new_path = lib.path;
                    }

                    auto node = handle_lib(LibInfo(new_path, lib.name, lib.type));
                    InsertDepNode(node, &parent->deps);
                });

                dep->ForEachIncDir([&parent] (const string& inc) {
                    if (inc[0] == '/') {
                        parent->inc_dirs.insert(inc);
                    } else {
                        parent->inc_dirs.insert(
                            RemoveDotAndDotDot(parent->lib.path + "/" + inc));
                    }
                });
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
    if (path == "..") {
        return "../..";
    }
    if (path == ".") {
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

static void CalcInDegree(const Target* target,
                         const unordered_map<LibInfo, DepTreeNode, LibInfoHash>& dep_tree,
                         unordered_map<const DepTreeNode*, int>* node2in) {
    list<const DepTreeNode*> q;

    target->ForEachDependency([&dep_tree, &q, &node2in] (const Dependency* dep) {
        dep->ForEachLibrary([&dep_tree, &q, &node2in] (const LibInfo& lib) {
            auto ref = dep_tree.find(lib);
            auto ret_pair = node2in->insert(std::move(make_pair(&ref->second, 0)));
            ++ret_pair.first->second;
            if (ret_pair.second) {
                q.push_back(&ref->second);
            }
        });
    });

    while (!q.empty()) {
        auto parent = q.front();
        q.pop_front();

        for (auto dep : parent->deps) {
            auto ret_pair = node2in->insert(std::move(make_pair(dep, 0)));
            ++ret_pair.first->second;
            if (ret_pair.second) {
                q.push_back(dep);
            }
        }
    }
}

static void TopologicalSort(const Target* target,
                            const unordered_map<LibInfo, DepTreeNode, LibInfoHash>& dep_tree,
                            unordered_map<const DepTreeNode*, int>* node2in,
                            vector<const DepTreeNode*>* res) {
    list<const DepTreeNode*> q;

    target->ForEachDependency([&dep_tree, &q, &node2in] (const Dependency* dep) {
        dep->ForEachLibrary([&dep_tree, &q, &node2in] (const LibInfo& lib) {
            auto dep_ref = dep_tree.find(lib);
            auto ref = node2in->find(&dep_ref->second);
            --ref->second;
            if (ref->second == 0) {
                q.push_back(ref->first);
            }
        });
    });

    while (!q.empty()) {
        auto parent = q.front();
        q.pop_front();
        res->push_back(parent);

        for (auto dep : parent->deps) {
            auto ref = node2in->find(dep);
            --ref->second;
            if (ref->second == 0) {
                q.push_back(dep);
            }
        }
    }
}

static string GenerateTargetDepLibs(const Target* target,
                                    const unordered_map<LibInfo, DepTreeNode, LibInfoHash>& dep_tree,
                                    unordered_map<const DepTreeNode*, int>* node2in) {
    vector<const DepTreeNode*> dep_list;
    TopologicalSort(target, dep_tree, node2in, &dep_list);

    string content;
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

    target->ForEachDependency([&content] (const Dependency* dep) {
        const string& dep_name = dep->GetName();

        dep->ForEachCSource([&content, &dep_name] (const string& src) {
            content += " " + src + "." + dep_name + ".o";
        });

        dep->ForEachCppSource([&content, &dep_name] (const string& src) {
            content += " " + src + "." + dep_name + ".o";
        });
    });

    return content;
}

static string GeneratePhonyBuildInfo(const Target* target,
                                     const unordered_map<LibInfo, DepTreeNode, LibInfoHash>& dep_tree,
                                     const unordered_map<const DepTreeNode*, int>& node2in,
                                     unordered_map<LibInfo, string, LibInfoHash>* node2label) {
    const string label_prefix = "omake_phony_";

    string content;
    vector<const DepTreeNode*> node_list;

    target->ForEachDependency([&dep_tree, &node2in, &node_list] (const Dependency* dep) {
        dep->ForEachLibrary([&dep_tree, &node2in, &node_list] (const LibInfo& lib) {
            auto dep_ref = dep_tree.find(lib);
            auto ref = node2in.find(&dep_ref->second);
            if (ref != node2in.end() && ref->second == 1) {
                node_list.push_back(ref->first);
            }
        });
    });

    for (auto iter : node_list) {
        const LibInfo& lib = iter->lib;
        if ((!IsLocalLib(lib)) && (!IsSysLib(lib)) && (!IsThirdPartyLib(lib))) {
            auto ret_pair = node2label->insert(
                std::move(make_pair(lib, label_prefix + std::to_string(node2label->size()))));
            if (ret_pair.second) {
                const string target_name = (lib.type == OMAKE_TYPE_STATIC)
                    ? ("lib" + lib.name + ".a")
                    : ("lib" + lib.name + ".so");
                content += ".PHONY: " + ret_pair.first->second + "\n" +
                    ret_pair.first->second + ":\n" +
                    "\t$(MAKE) debug=$(debug) " + target_name +
                    " -C " + lib.path + "\n\n";
            }
        }
    }

    return content;
}

static string GenerateDepInc(const Dependency* dep,
                             const unordered_map<LibInfo, DepTreeNode, LibInfoHash>& dep_tree) {
    list<const DepTreeNode*> q;
    unordered_set<const DepTreeNode*> lib_dedup;
    unordered_set<string> inc_dedup;

    dep->ForEachIncDir([&inc_dedup] (const string& inc) {
        inc_dedup.insert(inc);
    });

    dep->ForEachLibrary([&lib_dedup, &q, &dep_tree] (const LibInfo& lib) {
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

        inc_dedup.insert(std::move(GetParentDir(parent->lib.path)));
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

    target->ForEachDependency([&dep_tree, &obj_dedup, &content] (const Dependency* dep) {
        const string& dep_name = dep->GetName();
        const string dep_inc_str = GenerateDepInc(dep, dep_tree);

        string flags;
        dep->ForEachFlag([&flags] (const string& flag) {
            flags += " " + flag;
        });

        string local_content;
        dep->ForEachCSource([&obj_dedup, &local_content, &dep_name, &flags, &dep_inc_str] (const string& src) {
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

        dep->ForEachCppSource([&obj_dedup, &local_content, &dep_name, &flags, &dep_inc_str] (const string& src) {
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
    });

    return content;
}

static string GenerateTargetDepLabels(const Target* target,
                                      const unordered_map<LibInfo, string, LibInfoHash>& node2label) {
    unordered_set<string> label_dedup;

    target->ForEachDependency([&label_dedup, &node2label] (const Dependency* dep) {
        dep->ForEachLibrary([&label_dedup, &node2label] (const LibInfo& lib) {
            auto ref = node2label.find(lib);
            if (ref != node2label.end()) {
                label_dedup.insert(ref->second);
            } else if (IsLocalLib(lib)) {
                const int type = lib.type;
                string local_lib_name;
                if (type == OMAKE_TYPE_STATIC) {
                    local_lib_name = "lib" + lib.name + ".a";
                } else {
                    local_lib_name = "lib" + lib.name + ".so";
                }
                label_dedup.insert(std::move(local_lib_name));
            }
        });
    });

    string content;
    for (auto label : label_dedup) {
        content += " " + label;
    }
    return content;
}

static string GetGeneratedName(const Target* target) {
    const int type = target->GetType();
    if (type == OMAKE_TYPE_BINARY) {
        return target->GetName();
    } else if (type == OMAKE_TYPE_STATIC) {
        return "lib" + target->GetName() + ".a";
    } else if (type == OMAKE_TYPE_SHARED) {
        return "lib" + target->GetName() + ".so";
    }

    return string();
}

static string CollectFlagsForTarget(const Target* target) {
    unordered_set<string> dedup;

    target->ForEachDependency([&dedup] (const Dependency* dep) {
        dep->ForEachFlag([&dedup] (const string& flag) {
            dedup.insert(flag);
        });
    });

    string content;
    for (auto flag : dedup) {
        content += " " + flag;
    }
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
        content += " " + GetGeneratedName(iter.second);
    }
    content += "\n\n";

    content += ".PHONY: all clean distclean\n"
        "\n"
        "all: $(TARGET)\n"
        "\n";

    // pre process for dependencies
    unordered_map<LibInfo, DepTreeNode, LibInfoHash> dep_tree;
    for (auto iter : m_targets) {
        GenerateDepTree(iter.second, &dep_tree);
    }

    unordered_set<string> obj_dedup;
    unordered_map<LibInfo, string, LibInfoHash> node2label;

    for (auto iter : m_targets) {
        auto target = iter.second;

        unordered_map<const DepTreeNode*, int> node2in;
        CalcInDegree(target, dep_tree, &node2in);

        content += GeneratePhonyBuildInfo(target, dep_tree, node2in, &node2label);
        content += GenerateObjBuildInfo(target, dep_tree, &obj_dedup);

        content += target->GetName() + "_OBJS :=" + GenerateObjects(target) + "\n\n";

        string target_dep_libs;
        if (target->GetType() == OMAKE_TYPE_BINARY ||
            target->GetType() == OMAKE_TYPE_SHARED) {
            target_dep_libs = GenerateTargetDepLibs(target, dep_tree, &node2in);
            if (!target_dep_libs.empty()) {
                content += target->GetName() + "_LIBS :=" + target_dep_libs + "\n\n";
            }
        }

        content += GetGeneratedName(target) + ": $(" + target->GetName() + "_OBJS)";

        const string dep_label_str = GenerateTargetDepLabels(target, node2label);
        if (!dep_label_str.empty()) {
            content += " |" + dep_label_str;
        }

        string cmd;
        if (target->GetType() == OMAKE_TYPE_STATIC) {
            cmd = "$(AR) rc $@ $^";
        } else {
            const string extra_flags = CollectFlagsForTarget(target);

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
    content += "\n\n";

    content += "distclean:\n"
        "\t$(MAKE) clean\n";
    for (auto dep : node2label) {
        content += "\t$(MAKE) distclean -C " + dep.first.path + "\n";
    }

    return WriteFile(fname, content);
}
