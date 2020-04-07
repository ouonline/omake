#include "target.h"
#include "misc.h"
#include "text-utils/text_utils.h"
#include "lua-cpp/luacpp.h"
#include <iostream>

#include <sys/types.h>
#include <dirent.h>

using namespace std;
using namespace utils;
using namespace luacpp;

static void FindFileEndsWith(const string& dirname, const char* suffix,
                               std::vector<std::string>* file_list) {
    DIR* dirp = opendir(dirname.c_str());
    if (!dirp) {
        cerr << "opendif[" << dirname << "] failed: " << strerror(errno) << endl;
        return;
    }

    struct dirent* dentry;
    const int slen = strlen(suffix);
    while ((dentry = readdir(dirp))) {
        int dlen = strlen(dentry->d_name);
        if (TextEndsWith(dentry->d_name, dlen, suffix, slen)) {
            file_list->push_back(
                RemoveDotAndDotDot(dirname + "/" + string(dentry->d_name, dlen)));
        }
    }

    closedir(dirp);
}

static int FindParentDirPos(const char* fpath) {
    int plen = strlen(fpath) - 1;
    while (plen >= 0) {
        if (fpath[plen] == '/') {
            return plen;
        }
        --plen;
    }
    return -1;
}

void Target::AddSourceFiles(const char* fpath) {
    string parent_dir;
    const char* fname = nullptr;

    int offset = FindParentDirPos(fpath);
    if (offset >= 0) {
        parent_dir.assign(fpath, offset + 1);
        fname = fpath + offset + 1;
    } else {
        parent_dir = ".";
        fname = fpath;
    }

    int flen = strlen(fname);

    if (strcmp(fname, "*.cpp") == 0) {
        FindFileEndsWith(parent_dir, ".cpp", &m_cpp_sources);
    } else if (strcmp(fname, "*.c") == 0) {
        FindFileEndsWith(parent_dir, ".c", &m_c_sources);
    } else if (strcmp(fname, "*.cc") == 0) {
        FindFileEndsWith(parent_dir, ".cc", &m_cpp_sources);
    } else {
        if (TextEndsWith(fname, flen, ".cpp", 4) ||
            TextEndsWith(fname, flen, ".cc", 3)) {
            m_cpp_sources.push_back(RemoveDotAndDotDot(fpath));
        } else if (TextEndsWith(fname, flen, ".c", 2)) {
            m_c_sources.push_back(RemoveDotAndDotDot(fpath));
        }
    }
}

void Target::AddLibrary(const char* path, const char* name, int type) {
    if ((!(type & LIBRARY_TYPE_SHARED)) && (!(type & LIBRARY_TYPE_STATIC))) {
        cerr << "AddLibrary() MUST specify at least one of `STATIC` and `SHARED`." << endl;
        return;
    }

    string new_path;
    if (path) {
        new_path = RemoveDotAndDotDot(path);
    }

    LibInfo info(new_path, name, type);
    for (auto lib : m_libs) {
        if (lib == info) {
            return;
        }
    }

    m_libs.push_back(std::move(info));
}

void Target::AddIncludeDirectory(const char* name) {
    const string s(name);
    for (auto lib : m_inc_dirs) {
        if (lib == s) {
            return;
        }
    }
    m_inc_dirs.push_back(std::move(RemoveDotAndDotDot(s)));
}

/* -------------------------------------------------------------------------- */

void BinaryTarget::ForeachGeneratedNameAndCommand(
    const function<void (const string&, const string&)>& func) const {
    string cmd;
    if (m_cpp_sources.empty()) {
        cmd = "$(CC) $(CFLAGS) -o $@ $^ $(" + m_name + "_LIBS)";
    } else {
        cmd = "$(CXX) $(CXXFLAGS) -o $@ $^ $(" + m_name + "_LIBS)";
    }
    func(m_name, cmd);
}

/* -------------------------------------------------------------------------- */

LibraryTarget::LibraryTarget(const char* name, int type)
    : Target(name), m_type(type) {}

void LibraryTarget::ForeachGeneratedNameAndCommand(
    const function<void (const string&, const string&)>& func) const {
    if (m_type & LIBRARY_TYPE_STATIC) {
        func("lib" + m_name + ".a", "$(AR) rc $@ $^");
    }
    if (m_type & LIBRARY_TYPE_SHARED) {
        string cmd;
        if (m_cpp_sources.empty()) {
            cmd = "$(CC) -shared -o $@ $^ $(" + m_name + "_LIBS)";
        } else {
            cmd = "$(CXX) -shared -o $@ $^ $(" + m_name + "_LIBS)";
        }
        func("lib" + m_name + ".so", cmd);
    }
}
