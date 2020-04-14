#include "project.h"
#include "target.h"
#include "utils.h"
#include <string>
#include <iostream>
using namespace std;
using namespace luacpp;

int main(void) {
    LuaState l;
    InitLuaEnv(&l);

    string errmsg;
    bool ok = l.dofile("omake.lua", &errmsg,
                       [] (int nresults) -> bool {
                           if (nresults != 1) {
                               cerr << "result num != 1" << endl;
                               return false;
                           }
                           return true;
                       },
                       [] (int, const LuaObject& obj) -> bool {
                           auto project = obj.touserdata().object<Project>();
                           return project->GenerateMakefile("Makefile");
                       });
    if (!ok) {
        cerr << "dofile error: " << errmsg << endl;
    }

    return 0;
}
