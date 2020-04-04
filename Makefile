# This Makefile is generated by omake: https://github.com/ouonline/omake.git

CXX := g++
AR := ar

ifeq ($(debug), y)
	CXXFLAGS := -g
else
	CXXFLAGS := -O2 -DNDEBUG
endif
CXXFLAGS := $(CXXFLAGS) -Wall -Werror -Wextra -fPIC

TARGET := omake

.PHONY: all clean

all: $(TARGET)

omake_OBJS := ./project.cpp.omake.o ./target.cpp.omake.o ./misc.cpp.omake.o ./main.cpp.omake.o

omake_INCLUDE := -I.. -I$(HOME)/workspace/lua

omake_LIBS := ../lua-cpp/libluacpp.a ../text-utils/libtext_utils.a $(HOME)/workspace/lua/src/liblua.a -ldl -lm

./project.cpp.omake.o: ./project.cpp
	$(CXX) $(CXXFLAGS) $(omake_INCLUDE) -c $< -o $@

./target.cpp.omake.o: ./target.cpp
	$(CXX) $(CXXFLAGS) $(omake_INCLUDE) -c $< -o $@

./misc.cpp.omake.o: ./misc.cpp
	$(CXX) $(CXXFLAGS) $(omake_INCLUDE) -c $< -o $@

./main.cpp.omake.o: ./main.cpp
	$(CXX) $(CXXFLAGS) $(omake_INCLUDE) -c $< -o $@

.PHONY: omake-pre-process

$(omake_OBJS): | omake-pre-process

omake-pre-process:
	$(MAKE) debug=$(debug) -C ../lua-cpp
	$(MAKE) debug=$(debug) -C ../text-utils
	$(MAKE) debug=$(debug) -C $(HOME)/workspace/lua/src

omake: $(omake_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(omake_LIBS)

clean:
	rm -f $(TARGET) $(omake_OBJS)
