#include "target.h"
#include "common.h"
#include <iostream>
#include <algorithm>
using namespace std;

void Target::AddDependency(const Dependency* dep) {
    auto it = std::find(m_deps.begin(), m_deps.end(), dep);
    if (it == m_deps.end()) {
        m_deps.push_back(dep);
    } else {
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
