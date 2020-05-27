#include "dependency.h"
#include "utils.h"
#include <sys/types.h>
#include <dirent.h>
#include <iostream>
#include <cstring> // strerror()
using namespace std;

#include "cpputils/text_utils.h"
using namespace outils;

static void AddFileEndsWith(const string& dirname, const char* suffix,
                            std::unordered_set<string>* file_set) {
    DIR* dirp = opendir(dirname.c_str());
    if (!dirp) {
        cerr << "Dependency opendir [" << dirname << "] failed: "
             << strerror(errno) << endl;
        return;
    }

    struct dirent* dentry;
    const int slen = strlen(suffix);
    while ((dentry = readdir(dirp))) {
        int dlen = strlen(dentry->d_name);
        if (TextEndsWith(dentry->d_name, dlen, suffix, slen)) {
            const string fpath = dirname + "/" + string(dentry->d_name, dlen);
            auto ret_pair = file_set->insert(RemoveDotAndDotDot(fpath));
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
            auto ret_pair = m_cpp_sources.insert(RemoveDotAndDotDot(fpath));
            if (!ret_pair.second) {
                cerr << "duplicated source file [" << fpath << "]" << endl;
            }
        } else if (TextEndsWith(fname, flen, ".c", 2)) {
            auto ret_pair = m_c_sources.insert(RemoveDotAndDotDot(fpath));
            if (!ret_pair.second) {
                cerr << "duplicated source file [" << fpath << "]" << endl;
            }
        }
    }
}

static bool EmplaceLibInfo(LibInfo&& lib, vector<LibInfo>* libs) {
    for (auto iter = libs->begin(); iter != libs->end(); ++iter) {
        if (*iter == lib) {
            return false;
        }
    }

    libs->push_back(lib);
    return true;
}

void Dependency::AddLibrary(const char* path, const char* name, int type) {
    // path is null means `name` is sys lib
    string new_path;
    if (path) {
        const unsigned int plen = strlen(path);
        unsigned int chars_removed = TextTrim(path, plen, '/');
        new_path = RemoveDotAndDotDot(string(path, plen - chars_removed));
    }

    LibInfo lib(new_path, name, type);
    if (!EmplaceLibInfo(std::move(lib), &m_libs)) {
        cerr << "AddLibrary(): duplicated lib [" << name << "]" << endl;
    }
}

void Dependency::AddIncludeDirectory(const char* name) {
    const unsigned int namelen = strlen(name);
    unsigned int chars_removed = TextTrim(name, namelen, '/');
    auto ret_pair = m_inc_dirs.insert(
        RemoveDotAndDotDot(string(name, namelen - chars_removed)));
    if (!ret_pair.second) {
        cerr << "AddIncludeDirectory(): duplicated include directory ["
             << name << "]" << endl;
    }
}

void Dependency::ForEachFlag(const function<void (const string&)>& f) const {
    for (auto flag : m_flags) {
        f(flag);
    }
}

void Dependency::ForEachCSource(const function<void (const string&)>& f) const {
    for (auto src : m_c_sources) {
        f(src);
    }
}

void Dependency::ForEachCppSource(const function<void (const string&)>& f) const {
    for (auto src : m_cpp_sources) {
        f(src);
    }
}

void Dependency::ForEachIncDir(const function<void (const string&)>& f) const {
    for (auto inc : m_inc_dirs) {
        f(inc);
    }
}

void Dependency::ForEachLibrary(const function<void (const LibInfo&)>& f) const {
    for (auto lib : m_libs) {
        f(lib);
    }
}
