#include "project.h"
#include "utils.h"
#include "common.h"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <unistd.h> // access()
#include <cstring> // strerror()
using namespace std;

#include "lua-cpp/luacpp.h"
using namespace luacpp;

Target* Project::CreateBinary(const char* name) {
    auto ret_pair = m_targets.insert(make_pair(name, nullptr));
    if (!ret_pair.second) {
        cerr << "duplcated binary name[" << name << "]" << endl;
        return nullptr;
    }

    ret_pair.first->second = new Target(name, OMAKE_TYPE_BINARY);
    return ret_pair.first->second;
}

Target* Project::CreateStaticLibrary(const char* name) {
    auto ret_pair = m_targets.insert(make_pair(name, nullptr));
    if (!ret_pair.second) {
        cerr << "duplcated static library name[" << name << "]" << endl;
        return nullptr;
    }

    ret_pair.first->second = new Target(name, OMAKE_TYPE_STATIC);
    return ret_pair.first->second;
}

Target* Project::CreateSharedLibrary(const char* name) {
    auto ret_pair = m_targets.insert(make_pair(name, nullptr));
    if (!ret_pair.second) {
        cerr << "duplcated shared library name[" << name << "]" << endl;
        return nullptr;
    }

    ret_pair.first->second = new Target(name, OMAKE_TYPE_SHARED);
    return ret_pair.first->second;
}

Dependency* Project::CreateDependency() {
    const string dep_prefix = "omake_dep_";

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
    set<string> inc_dirs;
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

class ProjectHelper final : public LuaFunctionHelper {
public:
    ProjectHelper(const function<bool (int)>& before_proc,
                  const function<bool (int, const LuaObject&)>& proc)
        : m_before_proc(before_proc), m_proc(proc) {}
    bool BeforeProcess(int nresults) override {
        return m_before_proc(nresults);
    }
    bool Process(int i, const LuaObject& obj) override {
        return m_proc(i, obj);
    }
    void AfterProcess() override {}

private:
    function<bool (int)> m_before_proc;
    function<bool (int, const LuaObject&)> m_proc;
};

static bool ProcessOMakeProject(const string& dir, ProjectHelper* helper) {
#define MAX_PATH_LEN 4096
    LuaState l;
    InitLuaEnv(&l);

    char cur_path[MAX_PATH_LEN];
    if (!getcwd(cur_path, MAX_PATH_LEN)) {
        cerr << "path too long" << endl;
        return false;
    }

    if (chdir(dir.c_str()) != 0) {
        cerr << "chdir to [" << dir << "] failed: "
             << strerror(errno) << endl;
        return false;
    }

    string errmsg;
    bool ok = l.DoFile("omake.lua", &errmsg, helper);
    if (!ok) {
        cerr << "Preprocessing dependency [" << dir << "/omake.lua] failed: "
             << errmsg << endl;
    }

    if (chdir(cur_path) != 0) {
        cerr << "chdir to [" << cur_path << "] failed: "
             << strerror(errno) << endl;
        return false;
    }

    return ok;
#undef MAX_PATH_LEN
}

static void GenerateDepTree(const Target* target,
                            unordered_map<LibInfo, DepTreeNode, LibInfoHash>* dep_tree) {
    list<DepTreeNode*> q;

    auto handle_lib = [&q, &dep_tree] (const LibInfo& lib, bool is_third_party) -> DepTreeNode* {
        auto ret_pair = dep_tree->insert(make_pair(lib, DepTreeNode(lib)));
        auto node = &ret_pair.first->second;

        if (ret_pair.second) {
            if ((!IsSysLib(lib)) && (!is_third_party)) {
                q.push_back(node);
            }
        }

        return node;
    };

    target->ForEachDependency([&handle_lib] (const Dependency* dep) {
        dep->ForEachLibrary([&handle_lib] (const LibInfo& lib) {
            handle_lib(lib, IsThirdPartyLib(lib));
        });
    });

    while (!q.empty()) {
        auto parent = q.front();
        q.pop_front();

        const string omake_file = parent->lib.path + "/omake.lua";

        auto before_proc = [&omake_file] (int nresults) -> bool {
            if (nresults != 1) {
                cerr << "omake [" << omake_file << "] result num != 1" << endl;
                return false;
            }
            return true;
        };

        auto proc = [&parent, &handle_lib] (int, const LuaObject& obj) -> bool {
            auto project = obj.ToUserData().Get<Project>();
            auto target = project->FindTarget(parent->lib.name);
            if (!target) {
                return true;
            }

            target->ForEachDependency([&parent, &handle_lib] (const Dependency* dep) {
                dep->ForEachLibrary([&parent, &handle_lib] (const LibInfo& lib) {
                    string new_path;
                    bool is_third_party = IsThirdPartyLib(lib);
                    if ((!lib.path.empty()) && lib.path[0] != '/') {
                        new_path = RemoveDotAndDotDot(parent->lib.path + "/" + lib.path);
                    } else {
                        new_path = lib.path;
                    }

                    auto node = handle_lib(LibInfo(new_path, lib.name, lib.type), is_third_party);
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
        };

        ProjectHelper helper(before_proc, proc);
        ProcessOMakeProject(parent->lib.path, &helper);
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
            auto ret_pair = node2in->insert(make_pair(&ref->second, 0));
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
            auto ret_pair = node2in->insert(make_pair(dep, 0));
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
    unordered_set<string> link_path_dedup;

    for (auto dep : dep_list) {
        const LibInfo& lib = dep->lib;
        if (lib.type == OMAKE_TYPE_STATIC) {
            content += " " + lib.path + "/lib" + lib.name + ".a";
        } else if (lib.type == OMAKE_TYPE_SHARED) {
            if (!IsSysLib(lib)) {
                auto ret_pair = link_path_dedup.insert(lib.path);
                if (ret_pair.second) {
                    content += " -L" + lib.path;
                }
            }
            content += " -l" + lib.name;
        }
    }

    return content;
}

static string GenerateObjects(const unordered_set<string>& obj_of_target) {
    string content;
    for (auto obj : obj_of_target) {
        content += " " + obj;
    }
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
                make_pair(lib, label_prefix + std::to_string(node2label->size())));
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

static string GenerateObjectName(const string& src, const string& dep_name,
                                 size_t seq) {
    const int offset = FindParentDirPos(src.data(), src.size()) + 1;
    const string base_name = src.substr(offset);

    if (base_name.size() != src.size()) { // `src` is not in current dir
        return dep_name + "." + std::to_string(seq) + "." + base_name + ".o";
    }

    return dep_name + "." + base_name + ".o";
}

static string GenerateObjBuildInfo(const Target* target,
                                   const unordered_map<LibInfo, DepTreeNode, LibInfoHash>& dep_tree,
                                   unordered_set<string>* obj_of_target,
                                   unordered_set<string>* obj_dedup) {
    string content;

    target->ForEachDependency([&] (const Dependency* dep) {
        const string& dep_name = dep->GetName();
        const string dep_inc_str = GenerateDepInc(dep, dep_tree);

        string flags;
        dep->ForEachFlag([&flags] (const string& flag) {
            flags += " " + flag;
        });

        const string flag_var_name = dep->GetName() + "_FLAGS";
        const string inc_var_name = dep->GetName() + "_INCS";

        string local_content;
        dep->ForEachCSource([&] (const string& src) {
            const string obj = GenerateObjectName(src, dep_name, obj_of_target->size());
            obj_of_target->insert(obj);
            auto ret_pair = obj_dedup->insert(obj);
            if (ret_pair.second) {
                local_content += obj + ": " + src + "\n" +
                    "\t$(CC) $(CFLAGS)";
                if (!flags.empty()) {
                    local_content += " $(" + flag_var_name + ")";
                }
                if (!dep_inc_str.empty()) {
                    local_content += " $(" + inc_var_name + ")";
                }
                local_content += " -c $< -o $@\n\n";
            }
        });

        dep->ForEachCppSource([&] (const string& src) {
            const string obj = GenerateObjectName(src, dep_name, obj_of_target->size());
            obj_of_target->insert(obj);
            auto ret_pair = obj_dedup->insert(obj);
            if (ret_pair.second) {
                local_content += obj + ": " + src + "\n" +
                    "\t$(CXX) $(CXXFLAGS)";
                if (!flags.empty()) {
                    local_content += " $(" + flag_var_name + ")";
                }
                if (!dep_inc_str.empty()) {
                    local_content += " $(" + inc_var_name + ")";
                }
                local_content += " -c $< -o $@\n\n";
            }
        });

        if (!local_content.empty()) {
            if (!dep_inc_str.empty()) {
                content += inc_var_name + " :=" + dep_inc_str + "\n\n";
            }
            if (!flags.empty()) {
                content += flag_var_name + " :=" + flags + "\n\n";
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
    string content;
    unordered_set<string> dedup;

    target->ForEachDependency([&content, &dedup] (const Dependency* dep) {
        content += " $(" + dep->GetName() + "_FLAGS)";
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
        const string lib_var_name = target->GetName() + "_LIBS";
        const string obj_var_name = target->GetName() + "_OBJS";

        unordered_map<const DepTreeNode*, int> node2in;
        CalcInDegree(target, dep_tree, &node2in);

        content += GeneratePhonyBuildInfo(target, dep_tree, node2in, &node2label);

        unordered_set<string> obj_of_target;
        content += GenerateObjBuildInfo(target, dep_tree, &obj_of_target, &obj_dedup);

        content += obj_var_name + " :=" + GenerateObjects(obj_of_target) + "\n\n";

        string target_dep_libs;
        if (target->GetType() == OMAKE_TYPE_BINARY ||
            target->GetType() == OMAKE_TYPE_SHARED) {
            target_dep_libs = GenerateTargetDepLibs(target, dep_tree, &node2in);
            if (!target_dep_libs.empty()) {
                content += lib_var_name + " :=" + target_dep_libs + "\n\n";
            }
        }

        content += GetGeneratedName(target) + ": $(" + obj_var_name + ")";

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
                        cmd += " $(" + lib_var_name + ")";
                    }
                } else if (target->HasCSource()) {
                    cmd = "$(CC) $(CFLAGS)" + extra_flags +
                        " -o $@ $^";
                    if (!target_dep_libs.empty()) {
                        cmd += " $(" + lib_var_name + ")";
                    }
                }
            } else if (target->GetType() == OMAKE_TYPE_SHARED) {
                if (target->HasCppSource()) {
                    cmd = "$(CXX) $(CXXFLAGS)" + extra_flags +
                        " -shared -o $@ $^";
                    if (!target_dep_libs.empty()) {
                        cmd += " $(" + lib_var_name + ")";
                    }
                } else if (target->HasCSource()) {
                    cmd = "$(CC) $(CFLAGS)" + extra_flags +
                        " -shared -o $@ $^";
                    if (!target_dep_libs.empty()) {
                        cmd += " $(" + lib_var_name + ")";
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
