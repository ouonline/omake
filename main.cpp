#include "project.h"
#include "target.h"
#include "misc.h"
#include <string>
#include <iostream>
using namespace std;
using namespace luacpp;

int main(void) {
    LuaState l;
    InitLuaEnv(&l);

    string errmsg;
    bool ok = l.dofile("omake.lua", &errmsg, 1, [] (int, const LuaObject& obj) -> bool {
        auto project = obj.touserdata().object<Project>();
        return project->GenerateMakefile("Makefile");
    });
    if (!ok) {
        cerr << "dofile error: " << errmsg << endl;
    }

    return 0;
}
