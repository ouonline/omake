#ifndef __OMAKE_TARGET_H__
#define __OMAKE_TARGET_H__

#include "dependency.h"

class Target final {
public:
    Target(const char* name, int type)
        : m_type(type), m_name(name) {}

    int GetType() const { return m_type; }
    const std::string& GetName() const { return m_name; }

    bool HasCSource() const;
    bool HasCppSource() const;

    void AddDependency(const Dependency*);
    void ForEachDependency(const std::function<void (const Dependency*)>&) const;

private:
    const int m_type;
    const std::string m_name;
    std::unordered_set<const Dependency*> m_deps;

private:
    Target(const Target&);
    Target& operator=(const Target&);
};

#endif
