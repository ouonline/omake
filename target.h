#ifndef __TARGET_H__
#define __TARGET_H__

#include <stdint.h>
#include <string>
#include <vector>
#include <functional>

#define LIBRARY_TYPE_STATIC 1
#define LIBRARY_TYPE_SHARED 2

struct LibInfo {
    int type;
    std::string path;
    std::string name;

    LibInfo(const std::string& _path, const std::string& _name, int _type)
        : type(_type), path(_path), name(_name) {}

    bool operator==(const LibInfo& info) const {
        return (name == info.name && path == info.path && type == info.type);
    }
};

class Target {
public:
    Target(const char* name) : m_name(name) {}
    virtual ~Target() {}

    void AddSourceFiles(const char* file);
    void AddLibrary(const char* path, const char* name, int type);
    void AddIncludeDirectory(const char* path);

    const std::string& GetName() const { return m_name; }

    virtual void ForeachGeneratedNameAndCommand(
        const std::function<void (const std::string&, const std::string&)>& func) const = 0;

    const std::vector<std::string>& GetCSources() const { return m_c_sources; }
    const std::vector<std::string>& GetCppSources() const { return m_cpp_sources; }
    const std::vector<LibInfo>& GetLibraries() const { return m_libs; }
    const std::vector<std::string>& GetIncludeDirectories() const { return m_inc_dirs; }

protected:
    const std::string m_name;
    std::vector<std::string> m_c_sources;
    std::vector<std::string> m_cpp_sources;
    std::vector<LibInfo> m_libs;
    std::vector<std::string> m_inc_dirs;
};

class BinaryTarget final : public Target {
public:
    BinaryTarget(const char* name) : Target(name) {}

    void ForeachGeneratedNameAndCommand(
        const std::function<void (const std::string&, const std::string&)>& func) const override;
};

class LibraryTarget final : public Target {
public:
    LibraryTarget(const char* name, int type);

    void ForeachGeneratedNameAndCommand(
        const std::function<void (const std::string&, const std::string&)>& func) const override;

private:
    int m_type;
};

#endif
