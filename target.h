#ifndef __OMAKE_TARGET_H__
#define __OMAKE_TARGET_H__

#include "dependency.h"

class Target final {
public:
    Target(const char* name, int type);

    void AddDependency(Dependency*);

    int GetType() const { return m_type; }
    const std::string& GetName() const;
    std::string GetGeneratedName() const;

    bool HasCSource() const;
    bool HasCppSource() const;

    Dependency* GetDefaultDependency() { return &m_default_dep; }
    const Dependency* GetDefaultDependency() const { return &m_default_dep; }

    void ForeachDependency(const std::function<bool (const Dependency*)>&) const;

private:
    const int m_type;
    Dependency m_default_dep;
    std::unordered_set<Dependency*> m_deps;

private:
    Target(const Target&);
    Target& operator=(const Target&);
};

#endif
