#ifndef __PROJECT_H__
#define __PROJECT_H__

#include "target.h"

class Project final {
public:
    Target* CreateBinary(const char* name);
    Target* CreateLibrary(const char* name);
    Target* GetTarget(const std::string& name) const;
    bool GenerateMakefile(const std::string& fname);

private:
    std::string m_dep_root_dir;
    std::vector<Target*> m_targets;
};

#endif
