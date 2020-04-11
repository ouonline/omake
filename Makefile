# This Makefile is generated by omake: https://github.com/ouonline/omake.git

CXX := g++

ifeq ($(debug), y)
	CXXFLAGS += -g
else
	CXXFLAGS += -O2 -DNDEBUG
endif

TARGET := omake

.PHONY: all clean

all: $(TARGET)

.PHONY: omake_phony_1 omake_phony_0

omake_phony_1:
	$(MAKE) debug=$(debug) libluacpp_static.a -C ../lua-cpp

omake_phony_0:
	$(MAKE) debug=$(debug) libtext_utils_static.a -C ../text-utils

omake_dep_0_INCLUDE := -I../../../lua -I..

utils.cpp.omake_dep_0.o: utils.cpp
	$(CXX) $(CXXFLAGS) -Wall -Werror -Wextra -fPIC $(omake_dep_0_INCLUDE) -c $< -o $@

main.cpp.omake_dep_0.o: main.cpp
	$(CXX) $(CXXFLAGS) -Wall -Werror -Wextra -fPIC $(omake_dep_0_INCLUDE) -c $< -o $@

dependency.cpp.omake_dep_0.o: dependency.cpp
	$(CXX) $(CXXFLAGS) -Wall -Werror -Wextra -fPIC $(omake_dep_0_INCLUDE) -c $< -o $@

target.cpp.omake_dep_0.o: target.cpp
	$(CXX) $(CXXFLAGS) -Wall -Werror -Wextra -fPIC $(omake_dep_0_INCLUDE) -c $< -o $@

project.cpp.omake_dep_0.o: project.cpp
	$(CXX) $(CXXFLAGS) -Wall -Werror -Wextra -fPIC $(omake_dep_0_INCLUDE) -c $< -o $@

omake_OBJS := utils.cpp.omake_dep_0.o main.cpp.omake_dep_0.o dependency.cpp.omake_dep_0.o target.cpp.omake_dep_0.o project.cpp.omake_dep_0.o

omake_LIBS := ../lua-cpp/libluacpp_static.a ../text-utils/libtext_utils_static.a ../../../lua/src/liblua.a -lm -ldl

omake: $(omake_OBJS) | omake_phony_0 omake_phony_1
	$(CXX) $(CXXFLAGS) -fPIC -Wextra -Werror -Wall -o $@ $^ $(omake_LIBS)

clean:
	rm -f $(TARGET) $(omake_OBJS)
