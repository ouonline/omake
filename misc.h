#ifndef __MISC_H__
#define __MISC_H__

#include "lua-cpp/luacpp.h"
#include <string>

void InitLuaEnv(luacpp::LuaState* l);
std::string RemoveDotAndDotDot(const std::string& path);

#endif
