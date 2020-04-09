#include "target.h"
#include "common.h"
#include <iostream>
using namespace std;

Target::Target(const char* name, int type)
    : m_type(type), m_default_dep(name) {}

void Target::AddDependency(Dependency* dep) {
    auto ret_pair = m_deps.insert(dep);
    if (!ret_pair.second) {
        cerr << "AddDependency(): duplicated dependency" << endl;
    }
}

void Target::ForeachDependency(const function<bool (const Dependency*)>& f) const {
    if (!f(&m_default_dep)) {
        return;
    }

    for (auto dep : m_deps) {
        if (!f(dep)) {
            return;
        }
    }
}

const string& Target::GetName() const {
    return m_default_dep.GetName();
}

string Target::GetGeneratedName() const {
    if (m_type == OMAKE_TYPE_BINARY) {
        return m_default_dep.GetName();
    } else if (m_type == OMAKE_TYPE_STATIC) {
        return "lib" + m_default_dep.GetName() + ".a";
    } else if (m_type == OMAKE_TYPE_SHARED) {
        return "lib" + m_default_dep.GetName() + ".so";
    }

    return string();
}

bool Target::HasCSource() const  {
    if (m_default_dep.HasCSource()) {
        return true;
    }

    for (auto dep : m_deps) {
        if (dep->HasCSource()) {
            return true;
        }
    }

    return false;
}

bool Target::HasCppSource() const  {
    if (m_default_dep.HasCppSource()) {
        return true;
    }

    for (auto dep : m_deps) {
        if (dep->HasCppSource()) {
            return true;
        }
    }

    return false;
}
