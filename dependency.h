#ifndef __OMAKE_DEPENDENCY_H__
#define __OMAKE_DEPENDENCY_H__

#include <string>
#include <functional>
#include <unordered_set>

struct LibInfo {
    int type;
    std::string path; // null path means sys lib
    std::string name;

    LibInfo(const std::string& _path, const std::string& _name, int _type)
        : type(_type), path(_path), name(_name) {}

    bool operator==(const LibInfo& info) const {
        return (name == info.name && path == info.path && type == info.type);
    }
};

struct LibInfoHash final {
    unsigned long operator () (const LibInfo& info) const {
        return std::hash<std::string>()(info.path) +
            std::hash<std::string>()(info.name) +
            info.type;
    }
};

class Dependency final {
public:
    Dependency(const std::string& name) : m_name(name) {}

    void AddFlag(const char* flag);
    void AddSourceFiles(const char* file);
    void AddLibrary(const char* path, const char* name, int type);
    void AddIncludeDirectory(const char* path);

    const std::string& GetName() const { return m_name; }
    bool HasCSource() const { return (!m_c_sources.empty()); }
    bool HasCppSource() const { return (!m_cpp_sources.empty()); }

    void ForeachFlag(const std::function<void (const std::string&)>&) const;
    void ForeachCSource(const std::function<void (const std::string&)>&) const;
    void ForeachCppSource(const std::function<void (const std::string&)>&) const;
    void ForeachIncDir(const std::function<void (const std::string&)>&) const;
    void ForeachLibrary(const std::function<void (const LibInfo&)>&) const;

private:
    std::string m_name;
    std::unordered_set<std::string> m_flags;
    std::unordered_set<std::string> m_c_sources;
    std::unordered_set<std::string> m_cpp_sources;
    std::unordered_set<std::string> m_inc_dirs;
    std::unordered_set<LibInfo, LibInfoHash> m_libs;
};

#endif
