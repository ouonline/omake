#include "dependency.h"
#include "utils.h"
#include <sys/types.h>
#include <dirent.h>
#include <iostream>
#include <cstring> // strerror()
using namespace std;

#include "text-utils/text_utils.h"
using namespace utils;

static void AddFileEndsWith(const string& dirname, const char* suffix,
                            std::unordered_set<string>* file_set) {
    DIR* dirp = opendir(dirname.c_str());
    if (!dirp) {
        cerr << "opendir [" << dirname << "] failed: " << strerror(errno) << endl;
        return;
    }

    struct dirent* dentry;
    const int slen = strlen(suffix);
    while ((dentry = readdir(dirp))) {
        int dlen = strlen(dentry->d_name);
        if (TextEndsWith(dentry->d_name, dlen, suffix, slen)) {
            const string fpath = dirname + "/" + string(dentry->d_name, dlen);
            auto ret_pair = file_set->insert(
                std::move(RemoveDotAndDotDot(fpath)));
            if (!ret_pair.second) {
                cerr << "duplicated source file [" << fpath << "]" << endl;
            }
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

void Dependency::AddFlag(const char* flag) {
    auto ret_pair = m_flags.insert(flag);
    if (!ret_pair.second) {
        cerr << "AddFlag(): duplicated flag [" << flag << "]" << endl;
    }
}

void Dependency::AddSourceFiles(const char* fpath) {
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
        AddFileEndsWith(parent_dir, ".cpp", &m_cpp_sources);
    } else if (strcmp(fname, "*.c") == 0) {
        AddFileEndsWith(parent_dir, ".c", &m_c_sources);
    } else if (strcmp(fname, "*.cc") == 0) {
        AddFileEndsWith(parent_dir, ".cc", &m_cpp_sources);
    } else {
        if (TextEndsWith(fname, flen, ".cpp", 4) ||
            TextEndsWith(fname, flen, ".cc", 3)) {
            auto ret_pair = m_cpp_sources.insert(
                std::move(RemoveDotAndDotDot(fpath)));
            if (!ret_pair.second) {
                cerr << "duplicated source file [" << fpath << "]" << endl;
            }
        } else if (TextEndsWith(fname, flen, ".c", 2)) {
            auto ret_pair = m_c_sources.insert(
                std::move(RemoveDotAndDotDot(fpath)));
            if (!ret_pair.second) {
                cerr << "duplicated source file [" << fpath << "]" << endl;
            }
        }
    }
}

void Dependency::AddLibrary(const char* path, const char* name, int type) {
    // path is null means `name` is sys lib
    string new_path;
    if (path) {
        new_path = RemoveDotAndDotDot(path);
    }

    LibInfo lib(new_path, name, type);
    auto ret_pair = m_libs.insert(std::move(lib));
    if (!ret_pair.second) {
        cerr << "AddLibrary(): duplicated lib [" << name << "]" << endl;
    }
}

void Dependency::AddIncludeDirectory(const char* name) {
    auto ret_pair = m_inc_dirs.insert(std::move(RemoveDotAndDotDot(name)));
    if (!ret_pair.second) {
        cerr << "AddIncludeDirectory(): duplicated include directory ["
             << name << "]" << endl;
    }
}

void Dependency::ForeachFlag(const function<void (const string&)>& f) const {
    for (auto flag : m_flags) {
        f(flag);
    }
}

void Dependency::ForeachCSource(const function<void (const string&)>& f) const {
    for (auto src : m_c_sources) {
        f(src);
    }
}

void Dependency::ForeachCppSource(const function<void (const string&)>& f) const {
    for (auto src : m_cpp_sources) {
        f(src);
    }
}

void Dependency::ForeachIncDir(const function<void (const string&)>& f) const {
    for (auto inc : m_inc_dirs) {
        f(inc);
    }
}

void Dependency::ForeachLibrary(const function<void (const LibInfo&)>& f) const {
    for (auto lib : m_libs) {
        f(lib);
    }
}
