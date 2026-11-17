/* SPDX-License-Identifier: MIT OR Unlicense
   cco - v0.2.0 - C11 edition

   A single-header C11 build runtime.  Hand-written projects include this
   header directly; the `cco.c` seed in this directory provides `new` / `init`.

   Quick start:

       #define CCO_IMPLEMENTATION
       #include "cco.h"

       int main(int argc, char **argv) {
           CCO_GO_REBUILD_URSELF(argc, argv);
           Cco_Workspace ws;
           cco_workspace_init(&ws, argc, argv);
           Cco_Target *app = cco_executable(&ws, "app");
           cco_target_add_source(app, "src/main.c");
           return cco_workspace_run(&ws);
       }

   This is the M2 C prototype: target graph, content fingerprints, depfiles,
   state cache, compile/link, run/test, compdb, clean, manual/managed layouts.
   The C and C++ implementations are separate.
*/

#ifndef CCO_H
#define CCO_H

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CCO_VERSION "0.2.0"

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} Cco_Str_Array;

typedef struct Cco_Target Cco_Target;

typedef struct {
    Cco_Target **items;
    size_t count;
    size_t capacity;
} Cco_Target_Array;

typedef enum {
    CCO_EXECUTABLE,
    CCO_LIBRARY,
    CCO_SHARED_LIBRARY,
    CCO_TEST
} Cco_Target_Kind;

struct Cco_Target {
    char *name;
    Cco_Target_Kind kind;
    Cco_Str_Array sources;
    Cco_Str_Array includes;
    Cco_Str_Array defines;
    Cco_Str_Array cflags;
    Cco_Str_Array linkflags;
    Cco_Target_Array links;
    char *output;
    char *standard;
};

typedef struct {
    int argc;
    char **argv;
    Cco_Target_Array targets;
    char *root;
    char *build_root;
    char *cc;
    int msvc;
    bool verbose;
    bool dry_run;
    bool explain;
    char *cc_override;
} Cco_Workspace;

void cco_workspace_init(Cco_Workspace *ws, int argc, char **argv);
void cco_workspace_free(Cco_Workspace *ws);
void cco_workspace_managed_layout(Cco_Workspace *ws);
void cco_workspace_set_build_root(Cco_Workspace *ws, const char *dir);
int cco_workspace_run(Cco_Workspace *ws);

Cco_Target *cco_executable(Cco_Workspace *ws, const char *name);
Cco_Target *cco_library(Cco_Workspace *ws, const char *name);
Cco_Target *cco_shared_library(Cco_Workspace *ws, const char *name);
Cco_Target *cco_test(Cco_Workspace *ws, const char *name);

void cco_target_add_source(Cco_Target *t, const char *path);
void cco_target_add_include(Cco_Target *t, const char *dir);
void cco_target_add_define(Cco_Target *t, const char *define);
void cco_target_add_cflag(Cco_Target *t, const char *flag);
void cco_target_add_linkflag(Cco_Target *t, const char *flag);
void cco_target_link(Cco_Target *t, Cco_Target *dep);
void cco_target_output_to(Cco_Target *t, const char *path);
void cco_target_standard(Cco_Target *t, const char *standard);

Cco_Str_Array cco_glob(const char *pattern);

const char *cco_header_file_path(void);
int cco_go_rebuild_urself(int argc, char **argv, const char *source_path, const char *header_path);

#ifndef CCO_HEADER_FILE
#  define CCO_HEADER_FILE cco_header_file_path()
#endif

#define CCO_GO_REBUILD_URSELF(argc, argv) \
    cco_go_rebuild_urself((argc), (argv), __FILE__, CCO_HEADER_FILE)


#ifdef CCO_IMPLEMENTATION

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#  include <direct.h>
#  include <io.h>
#  include <process.h>
#  include <windows.h>
#  define CCO_MKDIR(path) _mkdir(path)
#  define CCO_RMDIR(path) _rmdir(path)
#  define CCO_UNLINK(path) _unlink(path)
#  define CCO_GETCWD(buf, size) _getcwd((buf), (size))
#  define CCO_CHDIR(path) _chdir(path)
#else
#  include <dirent.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <mach-o/dyld.h>
#  endif
#  define CCO_MKDIR(path) mkdir((path), 0777)
#  define CCO_RMDIR(path) rmdir(path)
#  define CCO_UNLINK(path) unlink(path)
#  define CCO_GETCWD(buf, size) getcwd((buf), (size))
#  define CCO_CHDIR(path) chdir(path)
#endif

static void cco_oom(void) {
    fprintf(stderr, "cco: out of memory\n");
    exit(1);
}

static void *cco_xmalloc(size_t size) {
    void *p = malloc(size ? size : 1);
    if (!p) cco_oom();
    return p;
}

static void *cco_xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size ? size : 1);
    if (!p) cco_oom();
    return p;
}

static char *cco_xstrdup(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s);
    char *p = (char *)cco_xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

static char *cco_xstrndup(const char *s, size_t n) {
    char *p = (char *)cco_xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static void cco_str_array_reserve(Cco_Str_Array *a, size_t need) {
    if (a->capacity >= need) return;
    size_t cap = a->capacity ? a->capacity : 8;
    while (cap < need) cap *= 2;
    a->items = (char **)cco_xrealloc(a->items, cap * sizeof(char *));
    a->capacity = cap;
}

void cco_str_array_push(Cco_Str_Array *a, const char *value) {
    cco_str_array_reserve(a, a->count + 1);
    a->items[a->count++] = cco_xstrdup(value ? value : "");
}

static void cco_str_array_push_owned(Cco_Str_Array *a, char *value) {
    cco_str_array_reserve(a, a->count + 1);
    a->items[a->count++] = value;
}

static bool cco_str_array_contains(const Cco_Str_Array *a, const char *value) {
    for (size_t i = 0; i < a->count; ++i) {
        if (strcmp(a->items[i], value) == 0) return true;
    }
    return false;
}

static void cco_str_array_free(Cco_Str_Array *a) {
    for (size_t i = 0; i < a->count; ++i) free(a->items[i]);
    free(a->items);
    a->items = NULL;
    a->count = 0;
    a->capacity = 0;
}

void cco_target_array_push(Cco_Target_Array *a, Cco_Target *value) {
    if (a->capacity < a->count + 1) {
        size_t cap = a->capacity ? a->capacity * 2 : 8;
        a->items = (Cco_Target **)cco_xrealloc(a->items, cap * sizeof(Cco_Target *));
        a->capacity = cap;
    }
    a->items[a->count++] = value;
}

static void cco_target_array_free(Cco_Target_Array *a) {
    free(a->items);
    a->items = NULL;
    a->count = 0;
    a->capacity = 0;
}

typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} Cco_SB;

static void cco_sb_reserve(Cco_SB *sb, size_t need) {
    if (sb->capacity >= need) return;
    size_t cap = sb->capacity ? sb->capacity : 128;
    while (cap < need) cap *= 2;
    sb->data = (char *)cco_xrealloc(sb->data, cap);
    sb->capacity = cap;
}

static void cco_sb_append_n(Cco_SB *sb, const char *s, size_t n) {
    cco_sb_reserve(sb, sb->len + n + 1);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

static void cco_sb_append(Cco_SB *sb, const char *s) {
    cco_sb_append_n(sb, s, strlen(s));
}

static void cco_sb_append_ch(Cco_SB *sb, char c) {
    cco_sb_reserve(sb, sb->len + 2);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

static const char *cco_sb_cstr(Cco_SB *sb) {
    if (!sb->data) cco_sb_reserve(sb, 1);
    sb->data[sb->len] = '\0';
    return sb->data;
}

static void cco_sb_free(Cco_SB *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->capacity = 0;
}

static bool cco_path_is_absolute(const char *p) {
    if (!p || !*p) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
#ifdef _WIN32
    if (isalpha((unsigned char)p[0]) && p[1] == ':' && (p[2] == '/' || p[2] == '\\')) return true;
#endif
    return false;
}

static char *cco_path_normalize(const char *path) {
    if (!path || !*path) return cco_xstrdup(".");
    bool absolute = cco_path_is_absolute(path);
    Cco_Str_Array parts = {0};
    const char *p = path;
    while (*p) {
        while (*p == '/' || *p == '\\') ++p;
        const char *start = p;
        while (*p && *p != '/' && *p != '\\') ++p;
        size_t n = (size_t)(p - start);
        if (n == 0) continue;
        if (n == 1 && start[0] == '.') continue;
        if (n == 2 && start[0] == '.' && start[1] == '.') {
            if (parts.count > 0 && strcmp(parts.items[parts.count - 1], "..") != 0) {
                free(parts.items[--parts.count]);
            } else if (!absolute) {
                cco_str_array_push(&parts, "..");
            }
            continue;
        }
        char *component = cco_xstrndup(start, n);
        cco_str_array_push_owned(&parts, component);
    }

    Cco_SB sb = {0};
    if (absolute) cco_sb_append_ch(&sb, '/');
    for (size_t i = 0; i < parts.count; ++i) {
        if (i) cco_sb_append_ch(&sb, '/');
        cco_sb_append(&sb, parts.items[i]);
    }
    if (sb.len == 0) cco_sb_append(&sb, absolute ? "" : ".");
    char *out = cco_xstrdup(cco_sb_cstr(&sb));
    cco_sb_free(&sb);
    cco_str_array_free(&parts);
    return out;
}

static char *cco_path_join(const char *a, const char *b) {
    if (!a || !*a) return cco_xstrdup(b ? b : "");
    if (!b || !*b) return cco_xstrdup(a);
    if (cco_path_is_absolute(b)) return cco_xstrdup(b);
    Cco_SB sb = {0};
    cco_sb_append(&sb, a);
    size_t n = sb.len;
    if (n > 0 && sb.data[n - 1] != '/' && sb.data[n - 1] != '\\') cco_sb_append_ch(&sb, '/');
    cco_sb_append(&sb, b);
    char *out = cco_path_normalize(cco_sb_cstr(&sb));
    cco_sb_free(&sb);
    return out;
}

static char *cco_path_absolute(const char *path) {
    if (cco_path_is_absolute(path)) return cco_path_normalize(path);
    char buf[4096];
    if (!CCO_GETCWD(buf, sizeof(buf))) return cco_path_normalize(path);
    return cco_path_join(buf, path);
}

static char *cco_executable_path(const char *argv0) {
#ifdef _WIN32
    char buf[4096];
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        buf[n] = '\0';
        return cco_path_absolute(buf);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    if (size > 0) {
        char *buf = (char *)cco_xmalloc(size + 1);
        if (_NSGetExecutablePath(buf, &size) == 0) {
            char *out = cco_path_absolute(buf);
            free(buf);
            return out;
        }
        free(buf);
    }
#elif defined(__linux__)
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return cco_path_absolute(buf);
    }
#endif
    return cco_path_absolute(argv0);
}

static char *cco_path_parent(const char *path) {
    char *norm = cco_path_normalize(path);
    char *slash = strrchr(norm, '/');
#ifdef _WIN32
    char *bslash = strrchr(norm, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    if (!slash) {
        free(norm);
        return cco_xstrdup(".");
    }
    if (slash == norm) {
        slash[1] = '\0';
        return norm;
    }
    *slash = '\0';
    return norm;
}

static const char *cco_path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bslash = strrchr(path, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    return slash ? slash + 1 : path;
}

static bool cco_path_is_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
#ifdef _WIN32
    return (st.st_mode & _S_IFREG) != 0;
#else
    return S_ISREG(st.st_mode);
#endif
}

static bool cco_path_is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
#ifdef _WIN32
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

static double cco_path_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1.0;
    return (double)st.st_mtime;
}

static bool cco_path_mkdir_p(const char *path) {
    char *norm = cco_path_normalize(path);
    if (strcmp(norm, ".") == 0 || strcmp(norm, "/") == 0) {
        free(norm);
        return true;
    }
    size_t len = strlen(norm);
    for (size_t i = 1; i <= len; ++i) {
        if (i == len || norm[i] == '/') {
            char saved = norm[i];
            norm[i] = '\0';
            if (norm[0] && CCO_MKDIR(norm) != 0 && errno != EEXIST) {
                if (!cco_path_is_dir(norm)) {
                    free(norm);
                    return false;
                }
            }
            norm[i] = saved;
        }
    }
    free(norm);
    return true;
}

static bool cco_path_remove_tree(const char *path) {
    if (!cco_path_is_dir(path)) return CCO_UNLINK(path) == 0 || !cco_path_is_file(path);
#ifdef _WIN32
    char *pattern = cco_path_join(path, "*");
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(pattern, &data);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        char *child = cco_path_join(path, data.cFileName);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) cco_path_remove_tree(child);
        else CCO_UNLINK(child);
        free(child);
    } while (FindNextFileA(h, &data));
    FindClose(h);
    return CCO_RMDIR(path) == 0;
#else
    DIR *dir = opendir(path);
    if (!dir) return false;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char *child = cco_path_join(path, entry->d_name);
        struct stat st;
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) cco_path_remove_tree(child);
        else CCO_UNLINK(child);
        free(child);
    }
    closedir(dir);
    return CCO_RMDIR(path) == 0;
#endif
}

static char *cco_path_relative(const char *root, const char *path) {
    char *abs_root = cco_path_absolute(root);
    char *abs_path = cco_path_absolute(path);
    size_t n = strlen(abs_root);
    if (strncmp(abs_path, abs_root, n) == 0 && abs_path[n] == '/') {
        char *out = cco_xstrdup(abs_path + n + 1);
        free(abs_root);
        free(abs_path);
        return out;
    }
    char *out = cco_xstrdup(cco_path_basename(abs_path));
    free(abs_root);
    free(abs_path);
    return out;
}

static bool cco_wildcard_match(const char *pattern, const char *text) {
    const char *star = NULL;
    const char *mark = NULL;
    while (*text) {
        if (*pattern == '?' || (*pattern && *pattern == *text)) {
            ++pattern;
            ++text;
        } else if (*pattern == '*') {
            star = pattern++;
            mark = text;
        } else if (star) {
            pattern = star + 1;
            text = ++mark;
        } else {
            return false;
        }
    }
    while (*pattern == '*') ++pattern;
    return *pattern == '\0';
}

static bool cco_match_components(char **pattern, size_t pcount, size_t pi, char **path,
                                 size_t scount, size_t si) {
    if (pi == pcount) return si == scount;
    if (strcmp(pattern[pi], "**") == 0) {
        if (cco_match_components(pattern, pcount, pi + 1, path, scount, si)) return true;
        if (si < scount) return cco_match_components(pattern, pcount, pi, path, scount, si + 1);
        return false;
    }
    if (si == scount) return false;
    if (!cco_wildcard_match(pattern[pi], path[si])) return false;
    return cco_match_components(pattern, pcount, pi + 1, path, scount, si + 1);
}

static bool cco_excluded_dir(const char *name) {
    return strcmp(name, ".git") == 0 || strcmp(name, ".hg") == 0 ||
           strcmp(name, ".svn") == 0 || strcmp(name, ".cco") == 0;
}

static void cco_glob_walk(const char *dir, const char *rel, char **pat, size_t pcount,
                          Cco_Str_Array *out
#ifdef _WIN32
                          , bool unused
#endif
) {
#ifdef _WIN32
    (void)unused;
    char *pattern = cco_path_join(dir, "*");
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(pattern, &data);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        char *child = cco_path_join(dir, data.cFileName);
        char *child_rel = *rel ? cco_path_join(rel, data.cFileName) : cco_xstrdup(data.cFileName);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!cco_excluded_dir(data.cFileName)) cco_glob_walk(child, child_rel, pat, pcount, out, false);
        } else {
            Cco_Str_Array parts = {0};
            const char *p = child_rel;
            while (*p) {
                while (*p == '/' || *p == '\\') ++p;
                const char *start = p;
                while (*p && *p != '/' && *p != '\\') ++p;
                if (p > start) cco_str_array_push_owned(&parts, cco_xstrndup(start, (size_t)(p - start)));
            }
            if (cco_match_components(pat, pcount, 0, parts.items, parts.count, 0)) {
                cco_str_array_push(out, child_rel);
            }
            cco_str_array_free(&parts);
        }
        free(child);
        free(child_rel);
    } while (FindNextFileA(h, &data));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char *child = cco_path_join(dir, entry->d_name);
        char *child_rel = *rel ? cco_path_join(rel, entry->d_name) : cco_xstrdup(entry->d_name);
        struct stat st;
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (!cco_excluded_dir(entry->d_name)) cco_glob_walk(child, child_rel, pat, pcount, out);
        } else if (cco_path_is_file(child)) {
            Cco_Str_Array parts = {0};
            const char *p = child_rel;
            while (*p) {
                while (*p == '/' || *p == '\\') ++p;
                const char *start = p;
                while (*p && *p != '/' && *p != '\\') ++p;
                if (p > start) cco_str_array_push_owned(&parts, cco_xstrndup(start, (size_t)(p - start)));
            }
            if (cco_match_components(pat, pcount, 0, parts.items, parts.count, 0)) {
                cco_str_array_push(out, child_rel);
            }
            cco_str_array_free(&parts);
        }
        free(child);
        free(child_rel);
    }
    closedir(d);
#endif
}

static int cco_cmp_str(const void *a, const void *b) {
    const char *const *sa = (const char *const *)a;
    const char *const *sb = (const char *const *)b;
    return strcmp(*sa, *sb);
}

Cco_Str_Array cco_glob(const char *pattern) {
    Cco_Str_Array out = {0};
    if (!pattern || !*pattern) return out;
    char *norm = cco_path_normalize(pattern);
    if (!strchr(norm, '*') && !strchr(norm, '?')) {
        if (cco_path_is_file(norm)) cco_str_array_push(&out, norm);
        free(norm);
        return out;
    }

    Cco_Str_Array pat = {0};
    const char *p = norm;
    while (*p) {
        while (*p == '/' || *p == '\\') ++p;
        const char *start = p;
        while (*p && *p != '/' && *p != '\\') ++p;
        if (p > start) cco_str_array_push_owned(&pat, cco_xstrndup(start, (size_t)(p - start)));
    }
    cco_glob_walk(".", "", pat.items, pat.count, &out
#ifdef _WIN32
                  , false
#endif
    );
    qsort(out.items, out.count, sizeof(char *), cco_cmp_str);
    cco_str_array_free(&pat);
    free(norm);
    return out;
}

static int cco_run_program(Cco_Str_Array *args, const char *cwd, bool echo) {
    if (args->count == 0) return -1;
    if (echo) {
        fflush(stdout);
        fflush(stderr);
        fprintf(stdout, ">");
        for (size_t i = 0; i < args->count; ++i) fprintf(stdout, " %s", args->items[i]);
        fprintf(stdout, "\n");
        fflush(stdout);
    }
#ifdef _WIN32
    Cco_SB cmd = {0};
    for (size_t i = 0; i < args->count; ++i) {
        if (i) cco_sb_append_ch(&cmd, ' ');
        cco_sb_append_ch(&cmd, '"');
        for (const char *p = args->items[i]; *p; ++p) {
            if (*p == '"') cco_sb_append_ch(&cmd, '\\');
            cco_sb_append_ch(&cmd, *p);
        }
        cco_sb_append_ch(&cmd, '"');
    }
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    char *cmdline = cco_xstrdup(cco_sb_cstr(&cmd));
    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, cwd, &si, &pi);
    cco_sb_free(&cmd);
    if (!ok) {
        free(cmdline);
        fprintf(stderr, "cco: cannot start `%s`\n", args->items[0]);
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    free(cmdline);
    return (int)code;
#else
    char **argv = (char **)cco_xmalloc((args->count + 1) * sizeof(char *));
    for (size_t i = 0; i < args->count; ++i) argv[i] = args->items[i];
    argv[args->count] = NULL;
    pid_t pid = fork();
    if (pid < 0) {
        free(argv);
        fprintf(stderr, "cco: fork failed: %s\n", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        if (cwd && *cwd && CCO_CHDIR(cwd) != 0) _exit(127);
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            free(argv);
            return -1;
        }
    }
    free(argv);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

const char *cco_header_file_path(void) { return __FILE__; }

static char *cco_safe_component(const char *name) {
    char *out = cco_xstrdup(name && *name ? name : "target");
    for (char *p = out; *p; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' || *p == '?' || *p == '"' ||
            *p == '<' || *p == '>' || *p == '|') {
            *p = '_';
        }
    }
    return out;
}

static char *cco_obj_ext(const Cco_Workspace *ws) { return cco_xstrdup(ws->msvc ? ".obj" : ".o"); }

static char *cco_exe_ext(const Cco_Workspace *ws) {
#ifdef _WIN32
    (void)ws;
    return cco_xstrdup(".exe");
#else
    (void)ws;
    return cco_xstrdup("");
#endif
}

static char *cco_shared_ext(const Cco_Workspace *ws) {
#ifdef __APPLE__
    (void)ws;
    return cco_xstrdup(".dylib");
#elif defined(_WIN32)
    (void)ws;
    return cco_xstrdup(".dll");
#else
    (void)ws;
    return cco_xstrdup(".so");
#endif
}

static char *cco_build_base(const Cco_Workspace *ws) {
    if (!ws->build_root || !*ws->build_root) return cco_xstrdup(ws->root);
    return cco_path_absolute(ws->build_root);
}

static char *cco_object_path(const Cco_Workspace *ws, const Cco_Target *t, const char *src) {
    char *base = cco_build_base(ws);
    char *obj_dir = cco_path_join(base, ws->build_root && *ws->build_root ? "obj" : "obj");
    char *safe = cco_safe_component(t->name);
    char *target_dir = cco_path_join(obj_dir, safe);
    char *rel = cco_path_relative(ws->root, src);
    char *rel_norm = cco_path_normalize(rel);
    char *joined = cco_path_join(target_dir, rel_norm);
    char *ext = cco_obj_ext(ws);
    Cco_SB sb = {0};
    cco_sb_append(&sb, joined);
    cco_sb_append(&sb, ext);
    char *out = cco_xstrdup(cco_sb_cstr(&sb));
    cco_sb_free(&sb);
    free(ext);
    free(joined);
    free(rel_norm);
    free(rel);
    free(target_dir);
    free(safe);
    free(obj_dir);
    free(base);
    return out;
}

static char *cco_target_output(const Cco_Workspace *ws, const Cco_Target *t) {
    if (t->output && *t->output) return cco_path_absolute(t->output);
    char *base = cco_build_base(ws);
    char *bin = cco_path_join(base, "bin");
    char *safe = cco_safe_component(t->name);
    char *ext = cco_exe_ext(ws);
    Cco_SB sb = {0};
    cco_sb_append(&sb, bin);
    cco_sb_append_ch(&sb, '/');
    cco_sb_append(&sb, safe);
    cco_sb_append(&sb, ext);
    char *out = cco_xstrdup(cco_sb_cstr(&sb));
    cco_sb_free(&sb);
    free(ext);
    free(safe);
    free(bin);
    free(base);
    return out;
}

static char *cco_shared_output_path(const Cco_Workspace *ws, const Cco_Target *t) {
    if (t->output && *t->output) return cco_path_absolute(t->output);
    char *base = cco_build_base(ws);
    char *bin = cco_path_join(base, "bin");
    char *safe = cco_safe_component(t->name);
    char *ext = cco_shared_ext(ws);
    Cco_SB sb = {0};
    cco_sb_append(&sb, bin);
    cco_sb_append_ch(&sb, '/');
    cco_sb_append(&sb, "lib");
    cco_sb_append(&sb, safe);
    cco_sb_append(&sb, ext);
    char *out = cco_xstrdup(cco_sb_cstr(&sb));
    cco_sb_free(&sb);
    free(ext);
    free(safe);
    free(bin);
    free(base);
    return out;
}

static Cco_Target *cco_target_new(Cco_Workspace *ws, const char *name, Cco_Target_Kind kind) {
    Cco_Target *t = (Cco_Target *)cco_xmalloc(sizeof(*t));
    memset(t, 0, sizeof(*t));
    t->name = cco_xstrdup(name);
    t->kind = kind;
    t->standard = cco_xstrdup("c11");
    cco_target_array_push(&ws->targets, t);
    return t;
}

static void cco_target_free(Cco_Target *t) {
    if (!t) return;
    free(t->name);
    free(t->output);
    free(t->standard);
    cco_str_array_free(&t->sources);
    cco_str_array_free(&t->includes);
    cco_str_array_free(&t->defines);
    cco_str_array_free(&t->cflags);
    cco_str_array_free(&t->linkflags);
    cco_target_array_free(&t->links);
    free(t);
}

void cco_workspace_init(Cco_Workspace *ws, int argc, char **argv) {
    memset(ws, 0, sizeof(*ws));
    ws->argc = argc;
    ws->argv = argv;
    char buf[4096];
    if (CCO_GETCWD(buf, sizeof(buf))) ws->root = cco_path_normalize(buf);
    else ws->root = cco_xstrdup(".");
#ifdef _MSC_VER
    ws->cc = cco_xstrdup("cl");
    ws->msvc = 1;
#else
    ws->cc = cco_xstrdup("cc");
    ws->msvc = 0;
#endif
    const char *env = getenv("CC");
    if (env && *env) {
        free(ws->cc);
        ws->cc = cco_xstrdup(env);
    }
}

void cco_workspace_free(Cco_Workspace *ws) {
    for (size_t i = 0; i < ws->targets.count; ++i) cco_target_free(ws->targets.items[i]);
    cco_target_array_free(&ws->targets);
    free(ws->root);
    free(ws->build_root);
    free(ws->cc);
    free(ws->cc_override);
    memset(ws, 0, sizeof(*ws));
}

void cco_workspace_managed_layout(Cco_Workspace *ws) {
    free(ws->build_root);
    ws->build_root = cco_xstrdup(".cco");
}

void cco_workspace_set_build_root(Cco_Workspace *ws, const char *dir) {
    free(ws->build_root);
    ws->build_root = cco_xstrdup(dir);
}

Cco_Target *cco_executable(Cco_Workspace *ws, const char *name) {
    return cco_target_new(ws, name, CCO_EXECUTABLE);
}
Cco_Target *cco_library(Cco_Workspace *ws, const char *name) {
    return cco_target_new(ws, name, CCO_LIBRARY);
}
Cco_Target *cco_shared_library(Cco_Workspace *ws, const char *name) {
    return cco_target_new(ws, name, CCO_SHARED_LIBRARY);
}
Cco_Target *cco_test(Cco_Workspace *ws, const char *name) {
    return cco_target_new(ws, name, CCO_TEST);
}

void cco_target_add_source(Cco_Target *t, const char *path) { cco_str_array_push(&t->sources, path); }
void cco_target_add_include(Cco_Target *t, const char *dir) { cco_str_array_push(&t->includes, dir); }
void cco_target_add_define(Cco_Target *t, const char *define) { cco_str_array_push(&t->defines, define); }
void cco_target_add_cflag(Cco_Target *t, const char *flag) { cco_str_array_push(&t->cflags, flag); }
void cco_target_add_linkflag(Cco_Target *t, const char *flag) { cco_str_array_push(&t->linkflags, flag); }
void cco_target_link(Cco_Target *t, Cco_Target *dep) { cco_target_array_push(&t->links, dep); }
void cco_target_output_to(Cco_Target *t, const char *path) {
    free(t->output);
    t->output = cco_xstrdup(path);
}
void cco_target_standard(Cco_Target *t, const char *standard) {
    free(t->standard);
    t->standard = cco_xstrdup(standard);
}

typedef struct {
    char *path;
    uintmax_t size;
    int64_t mtime_ns;
    char *hash;
} Cco_FileRecord;

typedef struct {
    char *id;
    char *fingerprint;
    Cco_Str_Array deps;
    Cco_Str_Array outputs;
} Cco_ActionRecord;

typedef struct {
    int schema;
    char *toolchain;
    Cco_FileRecord *files;
    size_t file_count;
    size_t file_capacity;
    Cco_ActionRecord *actions;
    size_t action_count;
    size_t action_capacity;
} Cco_State;

typedef struct {
    Cco_Workspace *ws;
    const char *root;
    bool verbose;
    bool dry_run;
    bool explain;
    Cco_State *state;
    const char *toolchain_fp;
} Cco_Ctx;

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    unsigned char buffer[64];
    size_t buflen;
} Cco_Sha256;

static uint32_t cco_rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

static void cco_sha_init(Cco_Sha256 *s) {
    s->state[0] = 0x6a09e667u;
    s->state[1] = 0xbb67ae85u;
    s->state[2] = 0x3c6ef372u;
    s->state[3] = 0xa54ff53au;
    s->state[4] = 0x510e527fu;
    s->state[5] = 0x9b05688cu;
    s->state[6] = 0x1f83d9abu;
    s->state[7] = 0x5be0cd19u;
    s->bitlen = 0;
    s->buflen = 0;
}

static void cco_sha_transform(Cco_Sha256 *s, const unsigned char *chunk) {
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
        0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
        0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)chunk[i * 4] << 24) | ((uint32_t)chunk[i * 4 + 1] << 16) |
               ((uint32_t)chunk[i * 4 + 2] << 8) | (uint32_t)chunk[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = cco_rotr(w[i - 15], 7) ^ cco_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = cco_rotr(w[i - 2], 17) ^ cco_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->state[0], b = s->state[1], c = s->state[2], d = s->state[3];
    uint32_t e = s->state[4], f = s->state[5], g = s->state[6], h = s->state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = cco_rotr(e, 6) ^ cco_rotr(e, 11) ^ cco_rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = cco_rotr(a, 2) ^ cco_rotr(a, 13) ^ cco_rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    s->state[0] += a;
    s->state[1] += b;
    s->state[2] += c;
    s->state[3] += d;
    s->state[4] += e;
    s->state[5] += f;
    s->state[6] += g;
    s->state[7] += h;
}

static void cco_sha_update(Cco_Sha256 *s, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    s->bitlen += (uint64_t)len * 8u;
    for (size_t i = 0; i < len; ++i) {
        s->buffer[s->buflen++] = p[i];
        if (s->buflen == 64) {
            cco_sha_transform(s, s->buffer);
            s->buflen = 0;
        }
    }
}

static char *cco_sha_hex(Cco_Sha256 *s) {
    uint64_t bits = s->bitlen;
    s->buffer[s->buflen++] = 0x80;
    if (s->buflen > 56) {
        while (s->buflen < 64) s->buffer[s->buflen++] = 0;
        cco_sha_transform(s, s->buffer);
        s->buflen = 0;
    }
    while (s->buflen < 56) s->buffer[s->buflen++] = 0;
    for (int i = 0; i < 8; ++i) s->buffer[56 + i] = (unsigned char)((bits >> (56 - i * 8)) & 0xff);
    cco_sha_transform(s, s->buffer);
    char *out = (char *)cco_xmalloc(65);
    static const char digits[] = "0123456789abcdef";
    size_t pos = 0;
    for (int i = 0; i < 8; ++i) {
        for (int shift = 28; shift >= 0; shift -= 4) out[pos++] = digits[(s->state[i] >> shift) & 0x0f];
    }
    out[pos] = '\0';
    return out;
}

static char *cco_hex_encode(const char *input) {
    static const char digits[] = "0123456789abcdef";
    size_t n = strlen(input);
    char *out = (char *)cco_xmalloc(n * 2 + 1);
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)input[i];
        out[i * 2] = digits[c >> 4];
        out[i * 2 + 1] = digits[c & 0x0f];
    }
    out[n * 2] = '\0';
    return out;
}

static int cco_hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char *cco_hex_decode(const char *input) {
    size_t n = strlen(input);
    if (n % 2 != 0) return cco_xstrdup("");
    char *out = (char *)cco_xmalloc(n / 2 + 1);
    for (size_t i = 0; i < n; i += 2) {
        int hi = cco_hex_nibble(input[i]);
        int lo = cco_hex_nibble(input[i + 1]);
        if (hi < 0 || lo < 0) {
            free(out);
            return cco_xstrdup("");
        }
        out[i / 2] = (char)((hi << 4) | lo);
    }
    out[n / 2] = '\0';
    return out;
}

static char *cco_make_fingerprint(const Cco_Str_Array *parts) {
    Cco_Sha256 s;
    cco_sha_init(&s);
    for (size_t i = 0; i < parts->count; ++i) {
        uint64_t len = (uint64_t)strlen(parts->items[i]);
        cco_sha_update(&s, &len, sizeof(len));
        cco_sha_update(&s, parts->items[i], (size_t)len);
    }
    return cco_sha_hex(&s);
}

static bool cco_stat_file(const char *path, uintmax_t *size, int64_t *mtime_ns) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    *size = (uintmax_t)st.st_size;
#if defined(_WIN32)
    *mtime_ns = (int64_t)st.st_mtime * 1000000000ll;
#elif defined(__APPLE__)
    *mtime_ns = (int64_t)st.st_mtimespec.tv_sec * 1000000000ll + (int64_t)st.st_mtimespec.tv_nsec;
#else
    *mtime_ns = (int64_t)st.st_mtim.tv_sec * 1000000000ll + (int64_t)st.st_mtim.tv_nsec;
#endif
    return true;
}

static char *cco_canonical_path(const char *path) {
    char *abs = cco_path_absolute(path);
    char *norm = cco_path_normalize(abs);
    free(abs);
    return norm;
}

static Cco_FileRecord *cco_state_find_file(Cco_State *state, const char *path) {
    for (size_t i = 0; i < state->file_count; ++i) {
        if (strcmp(state->files[i].path, path) == 0) return &state->files[i];
    }
    return NULL;
}

static Cco_FileRecord *cco_state_add_file(Cco_State *state, const char *path) {
    Cco_FileRecord *existing = cco_state_find_file(state, path);
    if (existing) return existing;
    if (state->file_capacity < state->file_count + 1) {
        size_t cap = state->file_capacity ? state->file_capacity * 2 : 16;
        state->files = (Cco_FileRecord *)cco_xrealloc(state->files, cap * sizeof(*state->files));
        state->file_capacity = cap;
    }
    Cco_FileRecord *rec = &state->files[state->file_count++];
    memset(rec, 0, sizeof(*rec));
    rec->path = cco_xstrdup(path);
    return rec;
}

static Cco_ActionRecord *cco_state_find_action(Cco_State *state, const char *id) {
    for (size_t i = 0; i < state->action_count; ++i) {
        if (strcmp(state->actions[i].id, id) == 0) return &state->actions[i];
    }
    return NULL;
}

static Cco_ActionRecord *cco_state_add_action(Cco_State *state, const char *id) {
    Cco_ActionRecord *existing = cco_state_find_action(state, id);
    if (existing) return existing;
    if (state->action_capacity < state->action_count + 1) {
        size_t cap = state->action_capacity ? state->action_capacity * 2 : 16;
        state->actions = (Cco_ActionRecord *)cco_xrealloc(state->actions, cap * sizeof(*state->actions));
        state->action_capacity = cap;
    }
    Cco_ActionRecord *rec = &state->actions[state->action_count++];
    memset(rec, 0, sizeof(*rec));
    rec->id = cco_xstrdup(id);
    return rec;
}

static void cco_state_clear(Cco_State *state) {
    for (size_t i = 0; i < state->file_count; ++i) {
        free(state->files[i].path);
        free(state->files[i].hash);
    }
    free(state->files);
    for (size_t i = 0; i < state->action_count; ++i) {
        free(state->actions[i].id);
        free(state->actions[i].fingerprint);
        cco_str_array_free(&state->actions[i].deps);
        cco_str_array_free(&state->actions[i].outputs);
    }
    free(state->actions);
    free(state->toolchain);
    memset(state, 0, sizeof(*state));
}

static bool cco_ensure_parent(const char *path);

static bool cco_state_load(Cco_State *state, const char *path) {
    cco_state_clear(state);
    state->schema = 1;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char line[8192];
    Cco_ActionRecord *current = NULL;
    if (!fgets(line, sizeof(line), f) || strncmp(line, "CCOSTATE 1", 10) != 0) {
        fclose(f);
        cco_state_clear(state);
        return false;
    }
    while (fgets(line, sizeof(line), f)) {
        char *tag = strtok(line, " \t\r\n");
        if (!tag) continue;
        if (strcmp(tag, "T") == 0) {
            char *value = strtok(NULL, " \t\r\n");
            if (value) { free(state->toolchain); state->toolchain = cco_hex_decode(value); }
        } else if (strcmp(tag, "F") == 0) {
            char *size_s = strtok(NULL, " \t\r\n");
            char *mtime_s = strtok(NULL, " \t\r\n");
            char *hash = strtok(NULL, " \t\r\n");
            char *path_hex = strtok(NULL, " \t\r\n");
            if (!size_s || !mtime_s || !hash || !path_hex) continue;
            char *decoded = cco_hex_decode(path_hex);
            Cco_FileRecord *rec = cco_state_add_file(state, decoded);
            rec->size = (uintmax_t)strtoull(size_s, NULL, 10);
            rec->mtime_ns = (int64_t)strtoll(mtime_s, NULL, 10);
            free(rec->hash);
            rec->hash = strcmp(hash, "-") == 0 ? cco_xstrdup("") : cco_xstrdup(hash);
            free(decoded);
        } else if (strcmp(tag, "A") == 0) {
            char *id_hex = strtok(NULL, " \t\r\n");
            char *fp = strtok(NULL, " \t\r\n");
            if (!id_hex || !fp) continue;
            char *id = cco_hex_decode(id_hex);
            current = cco_state_add_action(state, id);
            free(id);
            free(current->fingerprint);
            current->fingerprint = cco_xstrdup(fp);
        } else if (strcmp(tag, "D") == 0 || strcmp(tag, "O") == 0) {
            char *path_hex = strtok(NULL, " \t\r\n");
            if (!path_hex || !current) continue;
            char *decoded = cco_hex_decode(path_hex);
            if (tag[0] == 'D') cco_str_array_push(&current->deps, decoded);
            else cco_str_array_push(&current->outputs, decoded);
            free(decoded);
        }
    }
    fclose(f);
    return true;
}

static bool cco_state_save(const Cco_State *state, const char *path) {
    if (!cco_ensure_parent(path)) return false;
    Cco_SB tmp = {0};
    cco_sb_append(&tmp, path);
    cco_sb_append(&tmp, ".tmp");
    FILE *f = fopen(cco_sb_cstr(&tmp), "wb");
    if (!f) { cco_sb_free(&tmp); return false; }
    fprintf(f, "CCOSTATE %d\n", state->schema);
    char *toolchain_hex = cco_hex_encode(state->toolchain ? state->toolchain : "");
    fprintf(f, "T %s\n", toolchain_hex);
    free(toolchain_hex);
    for (size_t i = 0; i < state->file_count; ++i) {
        char *path_hex = cco_hex_encode(state->files[i].path);
        fprintf(f, "F %llu %lld %s %s\n", (unsigned long long)state->files[i].size,
                (long long)state->files[i].mtime_ns,
                state->files[i].hash && *state->files[i].hash ? state->files[i].hash : "-", path_hex);
        free(path_hex);
    }
    for (size_t i = 0; i < state->action_count; ++i) {
        char *id_hex = cco_hex_encode(state->actions[i].id);
        fprintf(f, "A %s %s\n", id_hex, state->actions[i].fingerprint ? state->actions[i].fingerprint : "");
        free(id_hex);
        for (size_t j = 0; j < state->actions[i].deps.count; ++j) {
            char *dep_hex = cco_hex_encode(state->actions[i].deps.items[j]);
            fprintf(f, "D %s\n", dep_hex);
            free(dep_hex);
        }
        for (size_t j = 0; j < state->actions[i].outputs.count; ++j) {
            char *out_hex = cco_hex_encode(state->actions[i].outputs.items[j]);
            fprintf(f, "O %s\n", out_hex);
            free(out_hex);
        }
    }
    bool ok = ferror(f) == 0;
    fclose(f);
    if (ok) {
        remove(path);
        ok = rename(cco_sb_cstr(&tmp), path) == 0;
    }
    if (!ok) remove(cco_sb_cstr(&tmp));
    cco_sb_free(&tmp);
    return ok;
}

static char *cco_state_path(const Cco_Workspace *ws) {
    if (ws->build_root && *ws->build_root) {
        char *base = cco_build_base(ws);
        char *p = cco_path_join(base, "state.txt");
        free(base);
        return p;
    }
    char *obj = cco_path_join(ws->root, "obj");
    char *p = cco_path_join(obj, "state.txt");
    free(obj);
    return p;
}

static char *cco_shell_quote(const char *s) {
#ifdef _WIN32
    Cco_SB sb = {0};
    cco_sb_append_ch(&sb, '"');
    for (const char *p = s; *p; ++p) {
        if (*p == '"') cco_sb_append_ch(&sb, '\\');
        cco_sb_append_ch(&sb, *p);
    }
    cco_sb_append_ch(&sb, '"');
    char *out = cco_xstrdup(cco_sb_cstr(&sb));
    cco_sb_free(&sb);
    return out;
#else
    Cco_SB sb = {0};
    cco_sb_append_ch(&sb, '\'');
    for (const char *p = s; *p; ++p) {
        if (*p == '\'') cco_sb_append(&sb, "'\\''");
        else cco_sb_append_ch(&sb, *p);
    }
    cco_sb_append_ch(&sb, '\'');
    char *out = cco_xstrdup(cco_sb_cstr(&sb));
    cco_sb_free(&sb);
    return out;
#endif
}

static char *cco_capture_output(const char *program, const char *argument) {
    char *q1 = cco_shell_quote(program);
    char *q2 = cco_shell_quote(argument);
    Cco_SB cmd = {0};
    cco_sb_append(&cmd, q1);
    cco_sb_append_ch(&cmd, ' ');
    cco_sb_append(&cmd, q2);
#ifdef _WIN32
    FILE *pipe = _popen(cco_sb_cstr(&cmd), "r");
#else
    FILE *pipe = popen(cco_sb_cstr(&cmd), "r");
#endif
    free(q1);
    free(q2);
    cco_sb_free(&cmd);
    if (!pipe) return cco_xstrdup("");
    Cco_SB out = {0};
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        cco_sb_append(&out, buf);
        if (out.len > 1024 * 1024) break;
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    char *result = cco_xstrdup(cco_sb_cstr(&out));
    cco_sb_free(&out);
    return result;
}

static char *cco_toolchain_fingerprint(const Cco_Workspace *ws) {
    Cco_Str_Array parts = {0};
    cco_str_array_push(&parts, "toolchain");
    cco_str_array_push(&parts, ws->cc);
    cco_str_array_push(&parts, ws->msvc ? "msvc" : "gnu");
    char *path = cco_path_absolute(ws->cc);
    uintmax_t size = 0;
    int64_t mtime = 0;
    if (cco_stat_file(path, &size, &mtime)) {
        cco_str_array_push(&parts, path);
        char buf[64];
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)size);
        cco_str_array_push(&parts, buf);
        snprintf(buf, sizeof(buf), "%lld", (long long)mtime);
        cco_str_array_push(&parts, buf);
    }
    free(path);
    char *version = cco_capture_output(ws->cc, ws->msvc ? "/Bv" : "--version");
    cco_str_array_push(&parts, version);
    free(version);
    char *fp = cco_make_fingerprint(&parts);
    cco_str_array_free(&parts);
    return fp;
}

static char *cco_file_hash(Cco_Ctx *ctx, const char *path) {
    uintmax_t size = 0;
    int64_t mtime = 0;
    if (!cco_stat_file(path, &size, &mtime)) return cco_xstrdup("");
    char *key = cco_canonical_path(path);
    if (ctx->state) {
        Cco_FileRecord *rec = cco_state_find_file(ctx->state, key);
        if (rec && rec->size == size && rec->mtime_ns == mtime && rec->hash && *rec->hash) {
            char *hash = cco_xstrdup(rec->hash);
            free(key);
            return hash;
        }
    }
    FILE *f = fopen(path, "rb");
    if (!f) { free(key); return cco_xstrdup(""); }
    Cco_Sha256 sha;
    cco_sha_init(&sha);
    char *buf = (char *)cco_xmalloc(1024 * 1024);
    size_t n;
    while ((n = fread(buf, 1, 1024 * 1024, f)) > 0) cco_sha_update(&sha, buf, n);
    free(buf);
    bool error = ferror(f) != 0;
    fclose(f);
    if (error) { free(key); return cco_xstrdup(""); }
    char *hash = cco_sha_hex(&sha);
    if (ctx->state) {
        Cco_FileRecord *rec = cco_state_add_file(ctx->state, key);
        rec->size = size;
        rec->mtime_ns = mtime;
        free(rec->hash);
        rec->hash = cco_xstrdup(hash);
    }
    free(key);
    return hash;
}

static bool cco_target_seen(const Cco_Target_Array *seen, const Cco_Target *t) {
    for (size_t i = 0; i < seen->count; ++i) {
        if (seen->items[i] == t) return true;
    }
    return false;
}

static void cco_add_unique(Cco_Str_Array *a, const char *value) {
    if (!cco_str_array_contains(a, value)) cco_str_array_push(a, value);
}

static void cco_collect_flags(const Cco_Target *t, Cco_Str_Array *includes, Cco_Str_Array *defines,
                              Cco_Str_Array *linkflags, Cco_Target_Array *seen) {
    if (cco_target_seen(seen, t)) return;
    cco_target_array_push(seen, (Cco_Target *)t);
    for (size_t i = 0; i < t->includes.count; ++i) cco_add_unique(includes, t->includes.items[i]);
    for (size_t i = 0; i < t->defines.count; ++i) cco_add_unique(defines, t->defines.items[i]);
    for (size_t i = 0; i < t->linkflags.count; ++i) cco_add_unique(linkflags, t->linkflags.items[i]);
    for (size_t i = 0; i < t->links.count; ++i) {
        cco_collect_flags(t->links.items[i], includes, defines, linkflags, seen);
    }
}

static void cco_collect_objects(const Cco_Workspace *ws, const Cco_Target *t, Cco_Str_Array *objects,
                                Cco_Target_Array *seen) {
    if (cco_target_seen(seen, t)) return;
    cco_target_array_push(seen, (Cco_Target *)t);
    for (size_t i = 0; i < t->sources.count; ++i) {
        char *obj = cco_object_path(ws, t, t->sources.items[i]);
        cco_str_array_push_owned(objects, obj);
    }
    for (size_t i = 0; i < t->links.count; ++i) {
        const Cco_Target *dep = t->links.items[i];
        if (dep->kind == CCO_SHARED_LIBRARY) continue;
        cco_collect_objects(ws, dep, objects, seen);
    }
}

static void cco_collect_shared_flags(const Cco_Workspace *ws, const Cco_Target *t,
                                     Cco_Str_Array *flags, Cco_Target_Array *seen) {
    if (cco_target_seen(seen, t)) return;
    cco_target_array_push(seen, (Cco_Target *)t);
    for (size_t i = 0; i < t->links.count; ++i) {
        const Cco_Target *dep = t->links.items[i];
        if (dep->kind == CCO_SHARED_LIBRARY) {
            char *out = cco_shared_output_path(ws, dep);
            char *dir = cco_path_parent(out);
            Cco_SB lib = {0};
            cco_sb_append(&lib, "-L");
            cco_sb_append(&lib, dir);
            cco_add_unique(flags, cco_sb_cstr(&lib));
            cco_sb_free(&lib);
            char *safe = cco_safe_component(dep->name);
            Cco_SB name = {0};
            cco_sb_append(&name, "-l");
            cco_sb_append(&name, safe);
            cco_add_unique(flags, cco_sb_cstr(&name));
            cco_sb_free(&name);
            free(safe);
#if defined(__APPLE__)
            cco_add_unique(flags, "-Wl,-rpath,@loader_path");
#elif !defined(_WIN32)
            cco_add_unique(flags, "-Wl,-rpath,$ORIGIN");
#endif
            free(dir);
            free(out);
        }
        cco_collect_shared_flags(ws, dep, flags, seen);
    }
}

static Cco_Str_Array cco_compile_command(const Cco_Workspace *ws, const Cco_Target *t,
                                         const char *src, const char *obj,
                                         const Cco_Str_Array *includes,
                                         const Cco_Str_Array *defines,
                                         const char *depfile) {
    Cco_Str_Array cmd = {0};
    cco_str_array_push(&cmd, ws->cc);
    if (ws->msvc) {
        cco_str_array_push(&cmd, "/nologo");
        Cco_SB std = {0};
        cco_sb_append(&std, "/std:");
        cco_sb_append(&std, t->standard);
        cco_str_array_push(&cmd, cco_sb_cstr(&std));
        cco_sb_free(&std);
        cco_str_array_push(&cmd, "/EHsc");
        cco_str_array_push(&cmd, "/utf-8");
        for (size_t i = 0; i < includes->count; ++i) {
            Cco_SB sb = {0};
            cco_sb_append(&sb, "/I");
            cco_sb_append(&sb, includes->items[i]);
            cco_str_array_push(&cmd, cco_sb_cstr(&sb));
            cco_sb_free(&sb);
        }
        for (size_t i = 0; i < defines->count; ++i) {
            Cco_SB sb = {0};
            cco_sb_append(&sb, "/D");
            cco_sb_append(&sb, defines->items[i]);
            cco_str_array_push(&cmd, cco_sb_cstr(&sb));
            cco_sb_free(&sb);
        }
        for (size_t i = 0; i < t->cflags.count; ++i) cco_str_array_push(&cmd, t->cflags.items[i]);
        if (depfile && *depfile) {
            cco_str_array_push(&cmd, "/sourceDependencies");
            cco_str_array_push(&cmd, depfile);
        }
        cco_str_array_push(&cmd, "/c");
        cco_str_array_push(&cmd, src);
        Cco_SB out = {0};
        cco_sb_append(&out, "/Fo");
        cco_sb_append(&out, obj);
        cco_str_array_push(&cmd, cco_sb_cstr(&out));
        cco_sb_free(&out);
    } else {
        Cco_SB std = {0};
        cco_sb_append(&std, "-std=");
        cco_sb_append(&std, t->standard);
        cco_str_array_push(&cmd, cco_sb_cstr(&std));
        cco_sb_free(&std);
        for (size_t i = 0; i < includes->count; ++i) {
            Cco_SB sb = {0};
            cco_sb_append(&sb, "-I");
            cco_sb_append(&sb, includes->items[i]);
            cco_str_array_push(&cmd, cco_sb_cstr(&sb));
            cco_sb_free(&sb);
        }
        for (size_t i = 0; i < defines->count; ++i) {
            Cco_SB sb = {0};
            cco_sb_append(&sb, "-D");
            cco_sb_append(&sb, defines->items[i]);
            cco_str_array_push(&cmd, cco_sb_cstr(&sb));
            cco_sb_free(&sb);
        }
        if (t->kind == CCO_SHARED_LIBRARY) cco_str_array_push(&cmd, "-fPIC");
        for (size_t i = 0; i < t->cflags.count; ++i) cco_str_array_push(&cmd, t->cflags.items[i]);
        if (depfile && *depfile) {
            cco_str_array_push(&cmd, "-MD");
            cco_str_array_push(&cmd, "-MF");
            cco_str_array_push(&cmd, depfile);
            cco_str_array_push(&cmd, "-MT");
            cco_str_array_push(&cmd, obj);
        }
        cco_str_array_push(&cmd, "-c");
        cco_str_array_push(&cmd, src);
        cco_str_array_push(&cmd, "-o");
        cco_str_array_push(&cmd, obj);
    }
    return cmd;
}

static Cco_Str_Array cco_link_command(const Cco_Workspace *ws, const Cco_Str_Array *objects,
                                      const char *out, const Cco_Str_Array *linkflags,
                                      bool shared) {
    Cco_Str_Array cmd = {0};
    cco_str_array_push(&cmd, ws->cc);
    if (ws->msvc) cco_str_array_push(&cmd, "/nologo");
    for (size_t i = 0; i < objects->count; ++i) cco_str_array_push(&cmd, objects->items[i]);
    if (ws->msvc) {
        if (shared) cco_str_array_push(&cmd, "/LD");
        Cco_SB fe = {0};
        cco_sb_append(&fe, "/Fe:");
        cco_sb_append(&fe, out);
        cco_str_array_push(&cmd, cco_sb_cstr(&fe));
        cco_sb_free(&fe);
    } else {
        if (shared) {
#ifdef __APPLE__
            cco_str_array_push(&cmd, "-dynamiclib");
#else
            cco_str_array_push(&cmd, "-shared");
#endif
        }
        cco_str_array_push(&cmd, "-o");
        cco_str_array_push(&cmd, out);
    }
    for (size_t i = 0; i < linkflags->count; ++i) cco_str_array_push(&cmd, linkflags->items[i]);
    return cmd;
}

static bool cco_ensure_parent(const char *path) {
    char *parent = cco_path_parent(path);
    bool ok = cco_path_mkdir_p(parent);
    free(parent);
    return ok;
}

static char *cco_depfile_for(const Cco_Workspace *ws, const char *obj) {
    Cco_SB sb = {0};
    cco_sb_append(&sb, obj);
    cco_sb_append(&sb, ws->msvc ? ".d.json" : ".d");
    char *out = cco_xstrdup(cco_sb_cstr(&sb));
    cco_sb_free(&sb);
    return out;
}

static bool cco_read_text_file(const char *path, char **out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return false; }
    rewind(f);
    char *buf = (char *)cco_xmalloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    *out = buf;
    return true;
}

static char *cco_json_unescape(const char *input, size_t n) {
    Cco_SB sb = {0};
    for (size_t i = 0; i < n; ++i) {
        char c = input[i];
        if (c != '\\' || i + 1 >= n) {
            cco_sb_append_ch(&sb, c);
            continue;
        }
        char next = input[++i];
        switch (next) {
        case '"': cco_sb_append_ch(&sb, '"'); break;
        case '\\': cco_sb_append_ch(&sb, '\\'); break;
        case '/': cco_sb_append_ch(&sb, '/'); break;
        case 'b': cco_sb_append_ch(&sb, '\b'); break;
        case 'f': cco_sb_append_ch(&sb, '\f'); break;
        case 'n': cco_sb_append_ch(&sb, '\n'); break;
        case 'r': cco_sb_append_ch(&sb, '\r'); break;
        case 't': cco_sb_append_ch(&sb, '\t'); break;
        default: cco_sb_append_ch(&sb, next); break;
        }
    }
    char *out = cco_xstrdup(cco_sb_cstr(&sb));
    cco_sb_free(&sb);
    return out;
}

static void cco_dep_add(Cco_Str_Array *deps, const char *value, const char *root) {
    char *path = value[0] == '/' ? cco_xstrdup(value) : cco_path_join(root, value);
    char *canonical = cco_canonical_path(path);
    if (!cco_str_array_contains(deps, canonical)) cco_str_array_push(deps, canonical);
    free(canonical);
    free(path);
}

static Cco_Str_Array cco_parse_depfile(const char *depfile, const char *root) {
    Cco_Str_Array deps = {0};
    char *text = NULL;
    if (!cco_read_text_file(depfile, &text)) return deps;
    if (strstr(depfile, ".json")) {
        const char *keys[2] = {"\"Path\"", "\"Source\""};
        for (int k = 0; k < 2; ++k) {
            const char *p = text;
            while ((p = strstr(p, keys[k])) != NULL) {
                p += strlen(keys[k]);
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
                if (*p != ':') continue;
                ++p;
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
                if (*p != '"') continue;
                const char *begin = ++p;
                while (*p && *p != '"') {
                    if (*p == '\\' && p[1]) ++p;
                    ++p;
                }
                if (*p != '"') break;
                char *value = cco_json_unescape(begin, (size_t)(p - begin));
                cco_dep_add(&deps, value, root);
                free(value);
                ++p;
            }
        }
        free(text);
        qsort(deps.items, deps.count, sizeof(char *), cco_cmp_str);
        return deps;
    }

    Cco_SB joined = {0};
    for (size_t i = 0; text[i]; ++i) {
        if (text[i] == '\\' && text[i + 1] == '\n') {
            cco_sb_append_ch(&joined, ' ');
            ++i;
        } else if (text[i] == '\\' && text[i + 1] == '\r' && text[i + 2] == '\n') {
            cco_sb_append_ch(&joined, ' ');
            i += 2;
        } else {
            cco_sb_append_ch(&joined, text[i]);
        }
    }
    free(text);
    char *line = joined.data ? joined.data : (char *)"";
    char *colon = strstr(line, ": ");
    if (!colon) colon = strstr(line, ":\t");
    if (!colon) {
        cco_sb_free(&joined);
        return deps;
    }
    char *p = colon + 1;
    Cco_SB token = {0};
    bool escaped = false;
    for (;;) {
        char c = *p;
        if (escaped) {
            cco_sb_append_ch(&token, c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '\0' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (token.len > 0) {
                cco_dep_add(&deps, cco_sb_cstr(&token), root);
                token.len = 0;
                if (token.data) token.data[0] = '\0';
            }
            if (c == '\0') break;
        } else {
            cco_sb_append_ch(&token, c);
        }
        ++p;
    }
    cco_sb_free(&token);
    cco_sb_free(&joined);
    qsort(deps.items, deps.count, sizeof(char *), cco_cmp_str);
    return deps;
}

static char *cco_compile_fingerprint(Cco_Ctx *ctx, const Cco_Target *t, const char *src,
                                     const char *obj, const Cco_Str_Array *cmd,
                                     const Cco_Str_Array *deps) {
    Cco_Str_Array parts = {0};
    cco_str_array_push(&parts, "compile");
    cco_str_array_push(&parts, ctx->toolchain_fp ? ctx->toolchain_fp : "");
    cco_str_array_push(&parts, ctx->root);
    cco_str_array_push(&parts, t->name);
    char *canon_src = cco_canonical_path(src);
    char *canon_obj = cco_canonical_path(obj);
    cco_str_array_push(&parts, canon_src);
    cco_str_array_push(&parts, canon_obj);
    free(canon_obj);
    free(canon_src);
    char *src_hash = cco_file_hash(ctx, src);
    cco_str_array_push(&parts, *src_hash ? src_hash : "<missing>");
    free(src_hash);
    for (size_t i = 0; i < cmd->count; ++i) cco_str_array_push(&parts, cmd->items[i]);
    for (size_t i = 0; i < deps->count; ++i) {
        cco_str_array_push(&parts, deps->items[i]);
        char *hash = cco_file_hash(ctx, deps->items[i]);
        cco_str_array_push(&parts, *hash ? hash : "<missing>");
        free(hash);
    }
    char *fp = cco_make_fingerprint(&parts);
    cco_str_array_free(&parts);
    return fp;
}

static void cco_collect_shared_outputs(const Cco_Workspace *ws, const Cco_Target *t,
                                       Cco_Str_Array *out, Cco_Target_Array *seen) {
    if (cco_target_seen(seen, t)) return;
    cco_target_array_push(seen, (Cco_Target *)t);
    for (size_t i = 0; i < t->links.count; ++i) {
        const Cco_Target *dep = t->links.items[i];
        if (dep->kind == CCO_SHARED_LIBRARY) {
            char *path = cco_shared_output_path(ws, dep);
            cco_str_array_push_owned(out, path);
        }
        cco_collect_shared_outputs(ws, dep, out, seen);
    }
}

static char *cco_link_fingerprint(Cco_Ctx *ctx, const Cco_Target *t, const char *out,
                                  const Cco_Str_Array *objects, const Cco_Str_Array *shared,
                                  const Cco_Str_Array *cmd) {
    Cco_Str_Array parts = {0};
    cco_str_array_push(&parts, "link");
    cco_str_array_push(&parts, ctx->toolchain_fp ? ctx->toolchain_fp : "");
    cco_str_array_push(&parts, ctx->root);
    cco_str_array_push(&parts, t->name);
    cco_str_array_push(&parts, out);
    for (size_t i = 0; i < cmd->count; ++i) cco_str_array_push(&parts, cmd->items[i]);
    for (size_t i = 0; i < objects->count; ++i) {
        cco_str_array_push(&parts, objects->items[i]);
        char *hash = cco_file_hash(ctx, objects->items[i]);
        cco_str_array_push(&parts, *hash ? hash : "<missing>");
        free(hash);
    }
    for (size_t i = 0; i < shared->count; ++i) {
        cco_str_array_push(&parts, shared->items[i]);
        char *hash = cco_file_hash(ctx, shared->items[i]);
        cco_str_array_push(&parts, *hash ? hash : "<missing>");
        free(hash);
    }
    char *fp = cco_make_fingerprint(&parts);
    cco_str_array_free(&parts);
    return fp;
}

static bool cco_compile_one(Cco_Ctx *ctx, const Cco_Target *t, const char *src,
                            const Cco_Str_Array *includes, const Cco_Str_Array *defines) {
    if (!cco_path_is_file(src)) {
        fprintf(stderr, "cco: source file not found: %s\n", src);
        return false;
    }
    char *obj = cco_object_path(ctx->ws, t, src);
    char *depfile = cco_depfile_for(ctx->ws, obj);
    char *canon_src = cco_canonical_path(src);
    Cco_SB id_sb = {0};
    cco_sb_append(&id_sb, "compile:");
    cco_sb_append(&id_sb, t->name);
    cco_sb_append_ch(&id_sb, ':');
    cco_sb_append(&id_sb, canon_src);
    Cco_ActionRecord *prev = ctx->state ? cco_state_find_action(ctx->state, cco_sb_cstr(&id_sb)) : NULL;

    Cco_Str_Array empty_deps = {0};
    const Cco_Str_Array *deps = prev ? &prev->deps : &empty_deps;
    Cco_Str_Array cmd = cco_compile_command(ctx->ws, t, src, obj, includes, defines, depfile);
    char *fingerprint = cco_compile_fingerprint(ctx, t, src, obj, &cmd, deps);
    bool has_output = cco_path_is_file(obj);
    if (prev && prev->fingerprint && strcmp(prev->fingerprint, fingerprint) == 0 && has_output) {
        if (ctx->explain) printf("[skip] %s (cache hit)\n", obj);
        free(fingerprint);
        cco_str_array_free(&cmd);
        cco_sb_free(&id_sb);
        free(canon_src);
        free(depfile);
        free(obj);
        return true;
    }
    if (ctx->explain) {
        const char *reason = !prev ? "no cached action" : (!has_output ? "output is missing" : "inputs or command changed");
        printf("[build] %s (%s)\n", obj, reason);
    }
    if (!cco_ensure_parent(obj)) {
        fprintf(stderr, "cco: cannot create parent directory for %s\n", obj);
        free(fingerprint);
        cco_str_array_free(&cmd);
        cco_sb_free(&id_sb);
        free(canon_src);
        free(depfile);
        free(obj);
        return false;
    }
    if (ctx->dry_run) {
        for (size_t i = 0; i < cmd.count; ++i) printf("%s%s", i ? " " : "> ", cmd.items[i]);
        printf("\n");
        free(fingerprint);
        cco_str_array_free(&cmd);
        cco_sb_free(&id_sb);
        free(canon_src);
        free(depfile);
        free(obj);
        return true;
    }
    int rc = cco_run_program(&cmd, ctx->ws->root, ctx->verbose);
    if (rc != 0) {
        fprintf(stderr, "cco: compile failed (%d): %s\n", rc, src);
        free(fingerprint);
        cco_str_array_free(&cmd);
        cco_sb_free(&id_sb);
        free(canon_src);
        free(depfile);
        free(obj);
        return false;
    }
    Cco_Str_Array new_deps = cco_parse_depfile(depfile, ctx->ws->root);
    free(fingerprint);
    fingerprint = cco_compile_fingerprint(ctx, t, src, obj, &cmd, &new_deps);
    if (ctx->state) {
        Cco_ActionRecord *rec = cco_state_add_action(ctx->state, cco_sb_cstr(&id_sb));
        free(rec->fingerprint);
        rec->fingerprint = fingerprint;
        cco_str_array_free(&rec->deps);
        rec->deps = new_deps;
        char *out_key = cco_canonical_path(obj);
        cco_str_array_push(&rec->outputs, out_key);
        free(out_key);
    } else {
        free(fingerprint);
        cco_str_array_free(&new_deps);
    }
    cco_str_array_free(&cmd);
    cco_sb_free(&id_sb);
    free(canon_src);
    free(depfile);
    free(obj);
    return true;
}
static bool cco_compile_target(Cco_Ctx *ctx, const Cco_Target *t, Cco_Target_Array *compiled) {
    if (cco_target_seen(compiled, t)) return true;
    for (size_t i = 0; i < t->links.count; ++i) {
        const Cco_Target *dep = t->links.items[i];
        if (dep->kind == CCO_SHARED_LIBRARY) continue;
        if (!cco_compile_target(ctx, dep, compiled)) return false;
    }
    Cco_Str_Array includes = {0}, defines = {0}, linkflags = {0};
    Cco_Target_Array seen = {0};
    cco_collect_flags(t, &includes, &defines, &linkflags, &seen);
    for (size_t i = 0; i < t->sources.count; ++i) {
        if (!cco_compile_one(ctx, t, t->sources.items[i], &includes, &defines)) {
            cco_str_array_free(&includes);
            cco_str_array_free(&defines);
            cco_str_array_free(&linkflags);
            cco_target_array_free(&seen);
            return false;
        }
    }
    cco_str_array_free(&includes);
    cco_str_array_free(&defines);
    cco_str_array_free(&linkflags);
    cco_target_array_free(&seen);
    cco_target_array_push(compiled, (Cco_Target *)t);
    return true;
}

static bool cco_link_target(Cco_Ctx *ctx, const Cco_Target *t, Cco_Target_Array *compiled,
                            Cco_Target_Array *linked) {
    if (t->kind == CCO_LIBRARY) return true;
    for (size_t i = 0; i < t->links.count; ++i) {
        const Cco_Target *dep = t->links.items[i];
        if (dep->kind == CCO_SHARED_LIBRARY) {
            if (!cco_link_target(ctx, dep, compiled, linked)) return false;
        }
    }
    if (!cco_compile_target(ctx, t, compiled)) return false;
    if (cco_target_seen(linked, t)) return true;
    if (t->kind == CCO_SHARED_LIBRARY && ctx->ws->msvc) {
        fprintf(stderr, "cco: shared libraries are not implemented for MSVC yet (`%s`)\n", t->name);
        return false;
    }

    Cco_Str_Array objects = {0};
    Cco_Target_Array seen = {0};
    cco_collect_objects(ctx->ws, t, &objects, &seen);
    cco_target_array_free(&seen);
    if (objects.count == 0) {
        fprintf(stderr, "cco: target `%s` has no sources\n", t->name);
        cco_str_array_free(&objects);
        return false;
    }
    bool shared = t->kind == CCO_SHARED_LIBRARY;
    char *out = shared ? cco_shared_output_path(ctx->ws, t) : cco_target_output(ctx->ws, t);

    Cco_Str_Array includes = {0}, defines = {0}, linkflags = {0};
    Cco_Target_Array flag_seen = {0};
    cco_collect_flags(t, &includes, &defines, &linkflags, &flag_seen);
    Cco_Target_Array shared_seen = {0};
    cco_collect_shared_flags(ctx->ws, t, &linkflags, &shared_seen);
    Cco_Str_Array shared_libs = {0};
    Cco_Target_Array shared_outputs_seen = {0};
    cco_collect_shared_outputs(ctx->ws, t, &shared_libs, &shared_outputs_seen);
    Cco_Str_Array cmd = cco_link_command(ctx->ws, &objects, out, &linkflags, shared);

    Cco_SB id_sb = {0};
    cco_sb_append(&id_sb, "link:");
    cco_sb_append(&id_sb, t->name);
    Cco_ActionRecord *prev = ctx->state ? cco_state_find_action(ctx->state, cco_sb_cstr(&id_sb)) : NULL;
    char *fingerprint = cco_link_fingerprint(ctx, t, out, &objects, &shared_libs, &cmd);
    bool has_output = cco_path_is_file(out);
    if (prev && prev->fingerprint && strcmp(prev->fingerprint, fingerprint) == 0 && has_output) {
        if (ctx->explain) printf("[skip] %s (cache hit)\n", out);
        free(fingerprint);
        cco_sb_free(&id_sb);
        cco_str_array_free(&cmd);
        cco_str_array_free(&includes);
        cco_str_array_free(&defines);
        cco_str_array_free(&linkflags);
        cco_target_array_free(&flag_seen);
        cco_target_array_free(&shared_seen);
        cco_str_array_free(&shared_libs);
        cco_target_array_free(&shared_outputs_seen);
        cco_str_array_free(&objects);
        free(out);
        cco_target_array_push(linked, (Cco_Target *)t);
        return true;
    }
    if (ctx->explain) {
        const char *reason = !prev ? "no cached action" : (!has_output ? "output is missing" : "inputs or command changed");
        printf("[link] %s (%s)\n", out, reason);
    }
    if (!cco_ensure_parent(out)) {
        fprintf(stderr, "cco: cannot create parent directory for %s\n", out);
        free(fingerprint);
        cco_sb_free(&id_sb);
        cco_str_array_free(&cmd);
        cco_str_array_free(&includes);
        cco_str_array_free(&defines);
        cco_str_array_free(&linkflags);
        cco_target_array_free(&flag_seen);
        cco_target_array_free(&shared_seen);
        cco_str_array_free(&shared_libs);
        cco_target_array_free(&shared_outputs_seen);
        cco_str_array_free(&objects);
        free(out);
        return false;
    }
    if (ctx->dry_run) {
        for (size_t i = 0; i < cmd.count; ++i) printf("%s%s", i ? " " : "> ", cmd.items[i]);
        printf("\n");
        free(fingerprint);
        cco_sb_free(&id_sb);
        cco_str_array_free(&cmd);
        cco_str_array_free(&includes);
        cco_str_array_free(&defines);
        cco_str_array_free(&linkflags);
        cco_target_array_free(&flag_seen);
        cco_target_array_free(&shared_seen);
        cco_str_array_free(&shared_libs);
        cco_target_array_free(&shared_outputs_seen);
        cco_str_array_free(&objects);
        free(out);
        cco_target_array_push(linked, (Cco_Target *)t);
        return true;
    }
    int rc = cco_run_program(&cmd, ctx->ws->root, ctx->verbose);
    if (rc != 0) {
        fprintf(stderr, "cco: link failed (%d): %s\n", rc, out);
        free(fingerprint);
        cco_sb_free(&id_sb);
        cco_str_array_free(&cmd);
        cco_str_array_free(&includes);
        cco_str_array_free(&defines);
        cco_str_array_free(&linkflags);
        cco_target_array_free(&flag_seen);
        cco_target_array_free(&shared_seen);
        cco_str_array_free(&shared_libs);
        cco_target_array_free(&shared_outputs_seen);
        cco_str_array_free(&objects);
        free(out);
        return false;
    }
    if (ctx->state) {
        Cco_ActionRecord *rec = cco_state_add_action(ctx->state, cco_sb_cstr(&id_sb));
        free(rec->fingerprint);
        rec->fingerprint = fingerprint;
        char *out_key = cco_canonical_path(out);
        cco_str_array_push(&rec->outputs, out_key);
        free(out_key);
    } else {
        free(fingerprint);
    }
    printf("built %s\n", out);
    cco_sb_free(&id_sb);
    cco_str_array_free(&cmd);
    cco_str_array_free(&includes);
    cco_str_array_free(&defines);
    cco_str_array_free(&linkflags);
    cco_target_array_free(&flag_seen);
    cco_target_array_free(&shared_seen);
    cco_str_array_free(&shared_libs);
    cco_target_array_free(&shared_outputs_seen);
    cco_str_array_free(&objects);
    free(out);
    cco_target_array_push(linked, (Cco_Target *)t);
    return true;
}
static bool cco_build_target(Cco_Ctx *ctx, const Cco_Target *t, Cco_Target_Array *compiled,
                             Cco_Target_Array *linked) {
    if (t->kind == CCO_LIBRARY) return cco_compile_target(ctx, t, compiled);
    return cco_link_target(ctx, t, compiled, linked);
}

static Cco_Target *cco_find_target(Cco_Workspace *ws, const char *name) {
    for (size_t i = 0; i < ws->targets.count; ++i) {
        if (strcmp(ws->targets.items[i]->name, name) == 0) return ws->targets.items[i];
    }
    return NULL;
}

static void cco_print_usage(const char *exe) {
    printf("usage: %s [command] [targets] [options]\n"
           "\n"
           "commands:\n"
           "  build (default)   build targets\n"
           "  run <target>      build and run an executable/test target\n"
           "  test [targets]    build and run test targets\n"
           "  compdb            write compile_commands.json\n"
           "  clean             remove build outputs and state\n"
           "  explain           build and explain rebuild decisions\n"
           "\n"
           "options:\n"
           "  --cc <path>       compiler to use (default: $CC or cc/cl)\n"
           "  --verbose         echo commands\n"
           "  --dry-run         print commands without running them\n"
           "  --explain         print why actions run or are skipped\n"
           "  --jobs <n>        accepted for compatibility\n"
           "  --help            show this help\n",
           exe);
}

static bool cco_write_file(const char *path, const char *content) {
    if (!cco_ensure_parent(path)) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t n = strlen(content);
    bool ok = fwrite(content, 1, n, f) == n;
    fclose(f);
    return ok;
}

static void cco_json_escape(Cco_SB *sb, const char *s) {
    for (const char *p = s; *p; ++p) {
        switch (*p) {
        case '"': cco_sb_append(sb, "\\\""); break;
        case '\\': cco_sb_append(sb, "\\\\"); break;
        case '\n': cco_sb_append(sb, "\\n"); break;
        case '\r': cco_sb_append(sb, "\\r"); break;
        case '\t': cco_sb_append(sb, "\\t"); break;
        default: cco_sb_append_ch(sb, *p); break;
        }
    }
}

static int cco_clean(Cco_Workspace *ws, const Cco_Ctx *ctx) {
    if (ws->build_root && *ws->build_root) {
        char *base = cco_build_base(ws);
        bool ok = cco_path_remove_tree(base);
        if (!ok && cco_path_is_dir(base)) {
            fprintf(stderr, "cco: clean failed: %s\n", base);
            free(base);
            return 1;
        }
        printf("removed %s\n", base);
        free(base);
        return 0;
    }
    char *obj = cco_path_join(ctx->root, "obj");
    cco_path_remove_tree(obj);
    free(obj);
    for (size_t i = 0; i < ws->targets.count; ++i) {
        const Cco_Target *t = ws->targets.items[i];
        if (t->kind == CCO_LIBRARY) continue;
        char *out = t->kind == CCO_SHARED_LIBRARY ? cco_shared_output_path(ws, t)
                                                   : cco_target_output(ws, t);
        CCO_UNLINK(out);
        char *parent = cco_path_parent(out);
        if (strcmp(parent, ctx->root) != 0) CCO_RMDIR(parent);
        free(parent);
        free(out);
    }
    printf("removed obj/ and target outputs\n");
    return 0;
}

static int cco_compdb(Cco_Workspace *ws) {
    Cco_SB sb = {0};
    cco_sb_append(&sb, "[\n");
    Cco_Str_Array seen = {0};
    bool first = true;
    for (size_t i = 0; i < ws->targets.count; ++i) {
        const Cco_Target *t = ws->targets.items[i];
        Cco_Str_Array includes = {0}, defines = {0}, linkflags = {0};
        Cco_Target_Array flag_seen = {0};
        cco_collect_flags(t, &includes, &defines, &linkflags, &flag_seen);
        for (size_t j = 0; j < t->sources.count; ++j) {
            char *abs = cco_path_absolute(t->sources.items[j]);
            if (cco_str_array_contains(&seen, abs)) {
                free(abs);
                continue;
            }
            cco_str_array_push(&seen, abs);
            char *obj = cco_object_path(ws, t, abs);
            Cco_Str_Array cmd = cco_compile_command(ws, t, abs, obj, &includes, &defines, NULL);
            if (!first) cco_sb_append(&sb, ",\n");
            first = false;
            cco_sb_append(&sb, "  {\"directory\":\"");
            cco_json_escape(&sb, ws->root);
            cco_sb_append(&sb, "\",\"file\":\"");
            cco_json_escape(&sb, abs);
            cco_sb_append(&sb, "\",\"output\":\"");
            cco_json_escape(&sb, obj);
            cco_sb_append(&sb, "\",\"arguments\":[");
            for (size_t k = 0; k < cmd.count; ++k) {
                if (k) cco_sb_append_ch(&sb, ',');
                cco_sb_append_ch(&sb, '"');
                cco_json_escape(&sb, cmd.items[k]);
                cco_sb_append_ch(&sb, '"');
            }
            cco_sb_append(&sb, "]}");
            cco_str_array_free(&cmd);
            free(obj);
            free(abs);
        }
        cco_str_array_free(&includes);
        cco_str_array_free(&defines);
        cco_str_array_free(&linkflags);
        cco_target_array_free(&flag_seen);
    }
    cco_sb_append(&sb, "\n]\n");
    char *path = cco_path_join(ws->root, "compile_commands.json");
    bool ok = cco_write_file(path, cco_sb_cstr(&sb));
    if (!ok) fprintf(stderr, "cco: cannot write %s\n", path);
    else printf("wrote %s\n", path);
    free(path);
    cco_sb_free(&sb);
    cco_str_array_free(&seen);
    return ok ? 0 : 1;
}

static int cco_run_target(Cco_Workspace *ws, Cco_Ctx *ctx, const char *name,
                          const Cco_Str_Array *program_args) {
    Cco_Target *t = cco_find_target(ws, name);
    if (!t) {
        fprintf(stderr, "cco: no such target: %s\n", name);
        return 2;
    }
    if (t->kind == CCO_LIBRARY || t->kind == CCO_SHARED_LIBRARY) {
        fprintf(stderr, "cco: `%s` is a library and cannot be run\n", name);
        return 2;
    }
    Cco_Target_Array compiled = {0}, linked = {0};
    bool ok = cco_build_target(ctx, t, &compiled, &linked);
    cco_target_array_free(&compiled);
    cco_target_array_free(&linked);
    if (!ok) return 1;
    if (ws->dry_run) return 0;
    char *out = cco_target_output(ws, t);
    Cco_Str_Array cmd = {0};
    cco_str_array_push(&cmd, out);
    for (size_t i = 0; i < program_args->count; ++i) cco_str_array_push(&cmd, program_args->items[i]);
    int rc = cco_run_program(&cmd, ws->root, false);
    cco_str_array_free(&cmd);
    free(out);
    return rc;
}

static int cco_run_tests(Cco_Workspace *ws, Cco_Ctx *ctx, const Cco_Str_Array *names) {
    Cco_Str_Array selected = {0};
    if (names->count == 0) {
        for (size_t i = 0; i < ws->targets.count; ++i) {
            if (ws->targets.items[i]->kind == CCO_TEST) cco_str_array_push(&selected, ws->targets.items[i]->name);
        }
    } else {
        for (size_t i = 0; i < names->count; ++i) cco_str_array_push(&selected, names->items[i]);
    }
    if (selected.count == 0) {
        printf("no test targets\n");
        cco_str_array_free(&selected);
        return 0;
    }
    int failures = 0;
    for (size_t i = 0; i < selected.count; ++i) {
        Cco_Target *t = cco_find_target(ws, selected.items[i]);
        if (!t) {
            fprintf(stderr, "cco: no such target: %s\n", selected.items[i]);
            cco_str_array_free(&selected);
            return 2;
        }
        Cco_Target_Array compiled = {0}, linked = {0};
        bool ok = cco_build_target(ctx, t, &compiled, &linked);
        cco_target_array_free(&compiled);
        cco_target_array_free(&linked);
        if (!ok) {
            cco_str_array_free(&selected);
            return 1;
        }
        if (ws->dry_run) continue;
        char *out = cco_target_output(ws, t);
        printf("running %s\n", t->name);
        int rc = cco_run_program(&((Cco_Str_Array){.items = &out, .count = 1, .capacity = 1}), ws->root, false);
        if (rc != 0) {
            fprintf(stderr, "cco: test `%s` failed (%d)\n", t->name, rc);
            ++failures;
        }
        free(out);
    }
    cco_str_array_free(&selected);
    if (failures) {
        fprintf(stderr, "cco: %d test target(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}

static bool cco_is_command(const char *s) {
    return strcmp(s, "build") == 0 || strcmp(s, "run") == 0 || strcmp(s, "test") == 0 ||
           strcmp(s, "compdb") == 0 || strcmp(s, "clean") == 0 || strcmp(s, "explain") == 0 ||
           strcmp(s, "graph") == 0 || strcmp(s, "bsp") == 0 || strcmp(s, "watch") == 0;
}

int cco_workspace_run(Cco_Workspace *ws) {
    Cco_Str_Array positional = {0};
    Cco_Str_Array program_args = {0};
    bool after_dashdash = false;
    for (int i = 1; i < ws->argc; ++i) {
        const char *a = ws->argv[i];
        if (after_dashdash) {
            cco_str_array_push(&program_args, a);
        } else if (strcmp(a, "--") == 0) {
            after_dashdash = true;
        } else if (strcmp(a, "--verbose") == 0) {
            ws->verbose = true;
        } else if (strcmp(a, "--dry-run") == 0) {
            ws->dry_run = true;
        } else if (strcmp(a, "--explain") == 0) {
            ws->explain = true;
        } else if (strcmp(a, "--no-color") == 0) {
            /* accepted */
        } else if (strcmp(a, "--cc") == 0) {
            if (i + 1 >= ws->argc) {
                fprintf(stderr, "cco: --cc expects a value\n");
                cco_str_array_free(&positional);
                cco_str_array_free(&program_args);
                return 2;
            }
            free(ws->cc_override);
            ws->cc_override = cco_xstrdup(ws->argv[++i]);
        } else if (strcmp(a, "--jobs") == 0) {
            if (i + 1 >= ws->argc) {
                fprintf(stderr, "cco: --jobs expects a value\n");
                cco_str_array_free(&positional);
                cco_str_array_free(&program_args);
                return 2;
            }
            ++i;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            cco_print_usage(ws->argv[0]);
            cco_str_array_free(&positional);
            cco_str_array_free(&program_args);
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "cco: unknown option: %s\n", a);
            cco_str_array_free(&positional);
            cco_str_array_free(&program_args);
            return 2;
        } else {
            cco_str_array_push(&positional, a);
        }
    }

    if (ws->cc_override && *ws->cc_override) {
        free(ws->cc);
        ws->cc = cco_xstrdup(ws->cc_override);
        const char *base = cco_path_basename(ws->cc);
        ws->msvc = strcmp(base, "cl") == 0 || strcmp(base, "cl.exe") == 0 ||
                   strcmp(base, "clang-cl") == 0 || strcmp(base, "clang-cl.exe") == 0;
    }

    const char *command = "build";
    size_t target_start = 0;
    if (positional.count > 0 && cco_is_command(positional.items[0])) {
        command = positional.items[0];
        target_start = 1;
    }
    Cco_Str_Array target_names = {0};
    for (size_t i = target_start; i < positional.count; ++i) cco_str_array_push(&target_names, positional.items[i]);

    char *toolchain_fp = cco_toolchain_fingerprint(ws);
    Cco_State state = {0};
    char *state_file = cco_state_path(ws);
    cco_state_load(&state, state_file);
    if (state.toolchain && strcmp(state.toolchain, toolchain_fp) != 0) cco_state_clear(&state);
    free(state.toolchain);
    state.toolchain = cco_xstrdup(toolchain_fp);
    Cco_Ctx ctx = {ws, ws->root, ws->verbose, ws->dry_run, ws->explain, &state, toolchain_fp};

    int result = 0;
    if (strcmp(command, "clean") == 0) {
        result = cco_clean(ws, &ctx);
    } else if (strcmp(command, "compdb") == 0) {
        result = cco_compdb(ws);
    } else if (strcmp(command, "graph") == 0 || strcmp(command, "bsp") == 0 ||
               strcmp(command, "watch") == 0) {
        fprintf(stderr, "cco: `%s` is not implemented in this prototype\n", command);
        result = 1;
    } else if (strcmp(command, "run") == 0) {
        if (target_names.count == 0) {
            fprintf(stderr, "cco: run expects a target\n");
            result = 2;
        } else {
            result = cco_run_target(ws, &ctx, target_names.items[0], &program_args);
        }
        if (!ws->dry_run) {
            if (!cco_state_save(&state, state_file)) fprintf(stderr, "cco: cannot save build state\n");
        }
    } else if (strcmp(command, "test") == 0) {
        result = cco_run_tests(ws, &ctx, &target_names);
        if (!ws->dry_run) {
            if (!cco_state_save(&state, state_file)) fprintf(stderr, "cco: cannot save build state\n");
        }
    } else {
        if (strcmp(command, "explain") == 0) {
            ws->explain = true;
            ctx.explain = true;
        }
        Cco_Str_Array selected = {0};
        if (target_names.count > 0) {
            for (size_t i = 0; i < target_names.count; ++i) {
                Cco_Target *t = cco_find_target(ws, target_names.items[i]);
                if (!t) {
                    fprintf(stderr, "cco: no such target: %s\n", target_names.items[i]);
                    cco_str_array_free(&selected);
                    cco_str_array_free(&target_names);
                    cco_str_array_free(&positional);
                    cco_str_array_free(&program_args);
                    return 2;
                }
                cco_str_array_push(&selected, t->name);
            }
        } else {
            for (size_t i = 0; i < ws->targets.count; ++i) {
                Cco_Target *t = ws->targets.items[i];
                if (t->kind == CCO_EXECUTABLE || t->kind == CCO_SHARED_LIBRARY) cco_str_array_push(&selected, t->name);
            }
            if (selected.count == 0) {
                for (size_t i = 0; i < ws->targets.count; ++i) {
                    Cco_Target *t = ws->targets.items[i];
                    if (t->kind == CCO_LIBRARY) cco_str_array_push(&selected, t->name);
                }
            }
            if (selected.count == 0) {
                for (size_t i = 0; i < ws->targets.count; ++i) {
                    Cco_Target *t = ws->targets.items[i];
                    if (t->kind == CCO_TEST) cco_str_array_push(&selected, t->name);
                }
            }
        }
        if (selected.count == 0) {
            fprintf(stderr, "cco: no targets registered\n");
            result = 2;
        } else {
            Cco_Target_Array compiled = {0}, linked = {0};
            for (size_t i = 0; i < selected.count; ++i) {
                Cco_Target *t = cco_find_target(ws, selected.items[i]);
                if (!cco_build_target(&ctx, t, &compiled, &linked)) {
                    result = 1;
                    break;
                }
            }
            cco_target_array_free(&compiled);
            cco_target_array_free(&linked);
        }
        cco_str_array_free(&selected);
        if (!ws->dry_run) {
            if (!cco_state_save(&state, state_file)) fprintf(stderr, "cco: cannot save build state\n");
        }
    }

    cco_state_clear(&state);
    free(state_file);
    free(toolchain_fp);
    cco_str_array_free(&target_names);
    cco_str_array_free(&positional);
    cco_str_array_free(&program_args);
    return result;
}

int cco_go_rebuild_urself(int argc, char **argv, const char *source_path, const char *header_path) {
    if (argc <= 0 || !argv || !argv[0]) return 0;
    char *binary = cco_executable_path(argv[0]);
#ifdef _WIN32
    const char *ext = strrchr(binary, '.');
    if (!ext || strcmp(ext, ".exe") != 0) {
        Cco_SB sb = {0};
        cco_sb_append(&sb, binary);
        cco_sb_append(&sb, ".exe");
        free(binary);
        binary = cco_xstrdup(cco_sb_cstr(&sb));
        cco_sb_free(&sb);
    }
#endif
    double bin_time = cco_path_mtime(binary);
    double src_time = cco_path_mtime(source_path);
    double hdr_time = cco_path_mtime(header_path);
    bool have_source = cco_path_is_file(source_path);
    bool need = have_source && (bin_time < 0.0 || src_time > bin_time || hdr_time > bin_time);
    if (need) {
        Cco_SB old = {0};
        cco_sb_append(&old, binary);
        cco_sb_append(&old, ".old");
        bool had = cco_path_is_file(binary);
        if (had) rename(binary, cco_sb_cstr(&old));
        Cco_Str_Array cmd = {0};
#ifdef _MSC_VER
        cco_str_array_push(&cmd, "cl");
        cco_str_array_push(&cmd, "/nologo");
        cco_str_array_push(&cmd, "/std:c11");
        cco_str_array_push(&cmd, "/O2");
        Cco_SB fe = {0};
        cco_sb_append(&fe, "/Fe:");
        cco_sb_append(&fe, binary);
        cco_str_array_push(&cmd, cco_sb_cstr(&fe));
        cco_sb_free(&fe);
#else
        cco_str_array_push(&cmd, "cc");
        cco_str_array_push(&cmd, "-std=c11");
        cco_str_array_push(&cmd, "-O2");
        cco_str_array_push(&cmd, "-o");
        cco_str_array_push(&cmd, binary);
#endif
        cco_str_array_push(&cmd, source_path);
        int rc = cco_run_program(&cmd, NULL, true);
        cco_str_array_free(&cmd);
        if (rc != 0) {
            fprintf(stderr, "cco: self-rebuild failed\n");
            if (had) rename(cco_sb_cstr(&old), binary);
            cco_sb_free(&old);
            free(binary);
            exit(1);
        }
        cco_sb_free(&old);
        Cco_Str_Array args = {0};
        for (int i = 0; i < argc; ++i) cco_str_array_push(&args, i == 0 ? binary : argv[i]);
        int code = cco_run_program(&args, NULL, false);
        cco_str_array_free(&args);
        free(binary);
        exit(code < 0 ? 1 : code);
    }
    free(binary);
    return 0;
}

#endif /* CCO_IMPLEMENTATION */


#endif /* CCO_H */
