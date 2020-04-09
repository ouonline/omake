#ifndef __OMAKE_PROJECT_H__
#define __OMAKE_PROJECT_H__

#include "target.h"
#include <vector>
#include <unordered_map>

class Project final {
public:
    Project() : m_dep_counter(0) {}

    Target* CreateBinary(const char* name);
    Target* CreateStaticLibrary(const char* name);
    Target* CreateSharedLibrary(const char* name);
    Dependency* CreateDependency();
    Target* FindTarget(const std::string& name) const;
    bool GenerateMakefile(const std::string& fname);

private:
    unsigned long m_dep_counter;
    std::unordered_map<std::string, Target*> m_targets;
};

#endif
