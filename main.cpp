#include "project.h"
#include "target.h"
#include "utils.h"
#include <string>
#include <iostream>
using namespace std;
using namespace luacpp;

class OMakeHelper final : public LuaFunctionHelper {
public:
    bool BeforeProcess(int nresults) override {
        if (nresults != 1) {
            cerr << "result num != 1" << endl;
            return false;
        }
        return true;
    }

    bool Process(int, const LuaObject& obj) override {
        auto project = obj.ToUserData().Get<Project>();
        return project->GenerateMakefile("Makefile");
    }

    void AfterProcess() override {}
};

int main(void) {
    LuaState l;
    InitLuaEnv(&l);

    string errmsg;
    OMakeHelper helper;
    bool ok = l.DoFile("omake.lua", &errmsg, &helper);
    if (!ok) {
        cerr << "DoFile error: " << errmsg << endl;
    }

    return 0;
}
