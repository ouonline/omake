#include "target.h"
#include "common.h"
#include <iostream>
using namespace std;

void Target::AddDependency(const Dependency* dep) {
    auto ret_pair = m_deps.insert(dep);
    if (!ret_pair.second) {
        cerr << "AddDependency(): duplicated dependency ["
             << dep->GetName() << "]" << endl;
    }
}

void Target::ForEachDependency(const function<void (const Dependency*)>& f) const {
    for (auto dep : m_deps) {
        f(dep);
    }
}

bool Target::HasCSource() const {
    for (auto dep : m_deps) {
        if (dep->HasCSource()) {
            return true;
        }
    }

    return false;
}

bool Target::HasCppSource() const {
    for (auto dep : m_deps) {
        if (dep->HasCppSource()) {
            return true;
        }
    }

    return false;
}
