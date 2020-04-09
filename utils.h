#ifndef __OMAKE_UTILS_H__
#define __OMAKE_UTILS_H__

#include "lua-cpp/luacpp.h"
#include <string>

void InitLuaEnv(luacpp::LuaState* l);
std::string RemoveDotAndDotDot(const std::string& path);

#endif
