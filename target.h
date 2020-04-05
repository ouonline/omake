#ifndef __TARGET_H__
#define __TARGET_H__

#include <stdint.h>
#include <string>
#include <vector>
#include <functional>

struct LibInfo {
    LibInfo(const char* _path, const char* _name)
        : path(_path), name(_name) {}
    std::string path;
    std::string name;
};

class Target {
public:
    Target(const char* name) : m_name(name) {}
    virtual ~Target() {}

    void AddSourceFile(const char* file);

    void AddDynamicLibrary(const char* path, const char* name);
    void AddStaticLibrary(const char* path, const char* name);
    void AddSystemDynamicLibrary(const char* name);
    void AddIncludeDirectory(const char* path);

    bool Finalize();

    const std::string& GetName() const { return m_name; }

    virtual std::string GetGeneratedName() const = 0;
    virtual std::string GetGeneratedCommand() const = 0;

    const std::string GetIncludeClause() const;
    const std::string GetLibClause() const;

    const std::vector<std::string>& GetCSources() const { return m_c_sources; }
    const std::vector<std::string>& GetCppSources() const { return m_cpp_sources; }

    const std::vector<LibInfo>& GetStaticLibraries() const { return m_static_libs; }
    const std::vector<LibInfo>& GetDynamicLibraries() const { return m_dynamic_libs; }
    const std::vector<std::string>& GetSystemDynamicLibraries() const { return m_sys_libs; }
    const std::vector<std::string>& GetIncludeDirectories() const { return m_inc_dirs; }

protected:
    const std::string m_name;
    std::vector<std::string> m_c_sources;
    std::vector<std::string> m_cpp_sources;
    std::vector<LibInfo> m_dynamic_libs;
    std::vector<LibInfo> m_static_libs;
    std::vector<std::string> m_sys_libs;
    std::vector<std::string> m_inc_dirs;
};

class BinaryTarget final : public Target {
public:
    BinaryTarget(const char* name) : Target(name) {}

    std::string GetGeneratedName() const override;
    std::string GetGeneratedCommand() const override;
};

class LibraryTarget final : public Target {
public:
    LibraryTarget(const char* name) : Target(name) {}

    std::string GetGeneratedName() const override;
    std::string GetGeneratedCommand() const override;
};

#endif
