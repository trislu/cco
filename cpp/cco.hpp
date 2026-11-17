/* SPDX-License-Identifier: MIT OR Unlicense
   cco - v0.2.0 - https://github.com/yourname/compiler-only

   compiler only: a single-header C++17 build runtime.

   Quick start:

       #define CCO_IMPLEMENTATION
       #include "cco.hpp"

       int main(int argc, char **argv) {
           CCO_GO_REBUILD_URSELF(argc, argv);
           cco::Workspace ws(argc, argv);
           auto &app = ws.executable("app").sources({"src/main.cpp"});
           return ws.run();
       }

   Bootstrap:

       c++ -std=c++17 build.cpp -o build
       ./build

   This is the M2 prototype: content fingerprints + depfiles
   (GCC/Clang -MD, MSVC /sourceDependencies), a stat fast path, and
   sequential builds. Response files and BSP are not implemented yet.
   The public API is intentionally close to docs/design.md.

   Scaffolding (`new` / `init`) is deliberately NOT part of this header.
   It lives in cco.cpp next to this header and is compiled into the
   separate `cco++` seed binary; generated projects only carry the runtime.
*/

#ifndef CCO_HPP
#define CCO_HPP

#define CCO_VERSION "0.2.0"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <process.h>
#else
#  include <sys/wait.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <mach-o/dyld.h>
#  endif
#endif

namespace cco {

// `__FILE__` must be captured where the header is defined, not where the
// macro is used; otherwise CCO_HEADER_FILE would expand to build.cpp.
inline const char *header_file_path() { return __FILE__; }

} // namespace cco

#ifndef CCO_HEADER_FILE
#  define CCO_HEADER_FILE ::cco::header_file_path()
#endif

namespace cco {

namespace fs = std::filesystem;

inline fs::path current_executable_path(const char *argv0) {
#ifdef _WIN32
    wchar_t buf[4096];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    if (n > 0 && n < sizeof(buf) / sizeof(buf[0])) return fs::path(std::wstring(buf, n));
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size > 0) {
        std::vector<char> buf(size + 1);
        if (_NSGetExecutablePath(buf.data(), &size) == 0) return fs::path(buf.data());
    }
#elif defined(__linux__)
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return fs::path(buf);
    }
#endif
    std::error_code ec;
    fs::path path = fs::absolute(argv0, ec);
    return ec ? fs::path(argv0) : path;
}

struct Toolchain {
    enum class Kind { Gnu, Msvc };
    Kind kind = Kind::Gnu;
    std::string cxx = "c++";
    std::string fingerprint;

    static Toolchain detect(const std::string &override_cxx = {});
    bool is_msvc() const { return kind == Kind::Msvc; }
};

struct Target {
    enum class Kind { Executable, Library, SharedLibrary, Test };

    std::string name;
    Kind kind = Kind::Executable;
    std::vector<fs::path> source_files;
    std::vector<std::string> include_dirs;
    std::vector<std::string> define_list;
    std::vector<std::string> cxxflags;
    std::vector<std::string> linkflags;
    std::vector<Target *> links;
    fs::path output;
    std::string cppstd = "c++17";

    Target(std::string n, Kind k) : name(std::move(n)), kind(k) {}

    Target &sources(std::vector<fs::path> files) {
        source_files = std::move(files);
        return *this;
    }
    Target &include(std::string dir) {
        include_dirs.push_back(std::move(dir));
        return *this;
    }
    Target &define(std::string d) {
        define_list.push_back(std::move(d));
        return *this;
    }
    Target &cxxflag(std::string f) {
        cxxflags.push_back(std::move(f));
        return *this;
    }
    Target &linkflag(std::string f) {
        linkflags.push_back(std::move(f));
        return *this;
    }
    Target &link(Target &other) {
        links.push_back(&other);
        return *this;
    }
    Target &output_to(fs::path p) {
        output = std::move(p);
        return *this;
    }
    Target &standard(std::string s) {
        cppstd = std::move(s);
        return *this;
    }
};

struct RunOptions {
    fs::path cwd;
    bool echo = false;
};

struct FileRecord {
    std::uintmax_t size = 0;
    std::int64_t mtime_ns = 0;
    std::string hash;
};

struct ActionRecord {
    std::string fingerprint;
    std::vector<std::string> deps;
    std::vector<std::string> outputs;
};

struct BuildState {
    int schema = 1;
    std::string toolchain;
    std::map<std::string, FileRecord> files;
    std::map<std::string, ActionRecord> actions;

    bool load(const fs::path &path);
    bool save(const fs::path &path, std::string &err) const;
};

bool read_file(const fs::path &path, std::string &out);
bool write_file(const fs::path &path, std::string_view content, std::string &err);

struct Workspace {
    Workspace(int argc_, char **argv_);

    Target &executable(std::string name, std::vector<fs::path> sources = {});
    Target &library(std::string name, std::vector<fs::path> sources = {});
    Target &shared_library(std::string name, std::vector<fs::path> sources = {});
    Target &test(std::string name, std::vector<fs::path> sources = {});
    int run();

    // Manual mode (default): binaries land in the working directory and
    // objects in ./obj/. Managed mode: .cco/bin/ and .cco/obj/. The
    // generated build.cpp opts into managed mode; hand-written build.cpp
    // keeps full control and may also call set_build_root().
    Workspace &managed_layout();
    Workspace &set_build_root(fs::path dir);

    int argc = 0;
    char **argv = nullptr;
    std::deque<Target> targets;
    Toolchain toolchain;
    fs::path root;
    fs::path build_root_path;
    bool verbose = false;
    bool dry_run = false;
    bool explain = false;
    std::string cxx_override;
};

int run_program(const std::vector<std::string> &args, const RunOptions &opt = {});
void go_rebuild_urself(int argc, char **argv, const char *source_path, const char *header_path);
std::vector<fs::path> glob(std::string pattern);

} // namespace cco

#define CCO_GO_REBUILD_URSELF(argc, argv) \
    ::cco::go_rebuild_urself((argc), (argv), __FILE__, CCO_HEADER_FILE)

#ifdef CCO_IMPLEMENTATION

namespace cco {

namespace {

std::string json_escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string safe_component(std::string s) {
    for (char &c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|') {
            c = '_';
        }
    }
    if (s.empty()) s = "target";
    return s;
}

#ifdef _WIN32
std::wstring widen(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

std::wstring quote_arg(const std::wstring &arg) {
    if (arg.empty()) return L"\"\"";
    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(c);
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring build_cmdline(const std::vector<std::string> &args) {
    std::wstring cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) cmd.push_back(L' ');
        cmd += quote_arg(widen(args[i]));
    }
    return cmd;
}

std::wstring resolve_program(const std::wstring &name) {
    if (name.find(L'\\') != std::wstring::npos || name.find(L'/') != std::wstring::npos) return name;
    wchar_t buf[MAX_PATH * 4];
    DWORD n = SearchPathW(nullptr, name.c_str(), L".exe", static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])),
                          buf, nullptr);
    if (n > 0 && n < sizeof(buf) / sizeof(buf[0])) return std::wstring(buf, n);
    return name;
}
#endif

std::string quote_for_display(const std::string &s) {
    bool need = s.find_first_of(" \t\"") != std::string::npos;
    if (!need) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    out += "\"";
    return out;
}

std::string join_for_display(const std::vector<std::string> &args) {
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) out += ' ';
        out += quote_for_display(args[i]);
    }
    return out;
}

bool file_is_regular(const fs::path &path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

std::vector<std::string> split_components(std::string s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '/' || c == '\\') {
            if (!cur.empty() && cur != ".") out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty() && cur != ".") out.push_back(cur);
    return out;
}

bool wildcard_match(std::string_view pattern, std::string_view text) {
    size_t p = 0, t = 0;
    size_t star = std::string_view::npos;
    size_t mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

bool match_components(const std::vector<std::string> &pattern, size_t pi,
                      const std::vector<std::string> &path, size_t si) {
    if (pi == pattern.size()) return si == path.size();
    if (pattern[pi] == "**") {
        if (match_components(pattern, pi + 1, path, si)) return true;
        if (si < path.size() && match_components(pattern, pi, path, si + 1)) return true;
        return false;
    }
    if (si == path.size()) return false;
    if (!wildcard_match(pattern[pi], path[si])) return false;
    return match_components(pattern, pi + 1, path, si + 1);
}

bool is_excluded_component(const std::string &s) {
    return s == ".cco" || s == ".git" || s == ".hg" || s == ".svn";
}

std::string hex_encode(std::string_view input) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(input.size() * 2);
    for (unsigned char c : input) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 0x0f]);
    }
    return out;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string hex_decode(std::string_view input) {
    if (input.size() % 2 != 0) return {};
    std::string out;
    out.reserve(input.size() / 2);
    for (size_t i = 0; i < input.size(); i += 2) {
        int hi = hex_digit(input[i]);
        int lo = hex_digit(input[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

class Sha256 {
public:
    Sha256() { reset(); }

    void update(const void *data, size_t len) {
        const auto *p = static_cast<const unsigned char *>(data);
        bitlen_ += static_cast<std::uint64_t>(len) * 8;
        for (size_t i = 0; i < len; ++i) {
            buffer_[buflen_++] = p[i];
            if (buflen_ == 64) {
                transform(buffer_);
                buflen_ = 0;
            }
        }
    }

    std::string hex() {
        finalize();
        static const char digits[] = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (std::uint32_t word : state_) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                out.push_back(digits[(word >> shift) & 0x0f]);
            }
        }
        return out;
    }

private:
    std::uint32_t state_[8]{};
    std::uint64_t bitlen_ = 0;
    unsigned char buffer_[64]{};
    size_t buflen_ = 0;

    static std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }

    void reset() {
        state_[0] = 0x6a09e667u;
        state_[1] = 0xbb67ae85u;
        state_[2] = 0x3c6ef372u;
        state_[3] = 0xa54ff53au;
        state_[4] = 0x510e527fu;
        state_[5] = 0x9b05688cu;
        state_[6] = 0x1f83d9abu;
        state_[7] = 0x5be0cd19u;
        bitlen_ = 0;
        buflen_ = 0;
    }

    void transform(const unsigned char *chunk) {
        static const std::uint32_t k[64] = {
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

        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(chunk[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(chunk[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(chunk[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(chunk[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t temp1 = h + s1 + ch + k[i] + w[i];
            std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t temp2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    void finalize() {
        std::uint64_t bits = bitlen_;
        buffer_[buflen_++] = 0x80;
        if (buflen_ > 56) {
            while (buflen_ < 64) buffer_[buflen_++] = 0;
            transform(buffer_);
            buflen_ = 0;
        }
        while (buflen_ < 56) buffer_[buflen_++] = 0;
        for (int i = 0; i < 8; ++i) {
            buffer_[56 + i] = static_cast<unsigned char>((bits >> (56 - i * 8)) & 0xff);
        }
        transform(buffer_);
        buflen_ = 0;
    }
};

std::string make_fingerprint(const std::vector<std::string> &parts) {
    Sha256 h;
    for (const auto &part : parts) {
        std::uint64_t len = part.size();
        h.update(&len, sizeof(len));
        h.update(part.data(), part.size());
    }
    return h.hex();
}

bool stat_file(const fs::path &path, std::uintmax_t &size, std::int64_t &mtime_ns) {
    std::error_code ec;
    size = fs::file_size(path, ec);
    if (ec) return false;
    auto ft = fs::last_write_time(path, ec);
    if (ec) return false;
    mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(ft.time_since_epoch()).count();
    return true;
}

fs::path find_program_path(const std::string &name) {
    std::error_code ec;
    fs::path direct(name);
    if (direct.has_parent_path()) {
        fs::path abs = fs::absolute(direct, ec);
        return ec ? direct : abs;
    }
    if (fs::exists(direct, ec)) return fs::absolute(direct, ec);

    const char *path_env = std::getenv("PATH");
    if (!path_env) return {};
#ifdef _WIN32
    constexpr char sep = ';';
#else
    constexpr char sep = ':';
#endif
    std::string path = path_env;
    size_t begin = 0;
    while (begin <= path.size()) {
        size_t end = path.find(sep, begin);
        std::string dir = path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!dir.empty()) {
            fs::path candidate = fs::path(dir) / name;
            if (!fs::exists(candidate, ec)) {
#ifdef _WIN32
                candidate += ".exe";
#endif
            }
            if (fs::exists(candidate, ec)) return fs::absolute(candidate, ec);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

std::string shell_quote(const std::string &s) {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    out += '"';
    return out;
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += '\'';
    return out;
#endif
}

std::string capture_output(const std::vector<std::string> &args) {
    if (args.empty()) return {};
    std::string cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) cmd += ' ';
        cmd += shell_quote(args[i]);
    }
#ifdef _WIN32
    FILE *pipe = _popen(cmd.c_str(), "r");
#else
    FILE *pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) return {};
    std::string out;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe)) {
        out += buf;
        if (out.size() > 1024 * 1024) break;
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return out;
}

std::string toolchain_fingerprint(const Toolchain &tc) {
    std::vector<std::string> parts = {"toolchain", tc.cxx, tc.is_msvc() ? "msvc" : "gnu"};
    fs::path executable = find_program_path(tc.cxx);
    if (!executable.empty()) {
        std::uintmax_t size = 0;
        std::int64_t mtime = 0;
        if (stat_file(executable, size, mtime)) {
            parts.push_back(executable.string());
            parts.push_back(std::to_string(size));
            parts.push_back(std::to_string(mtime));
        }
    }
    std::vector<std::string> version_args = {tc.cxx, tc.is_msvc() ? "/Bv" : "--version"};
    parts.push_back(capture_output(version_args));
    return make_fingerprint(parts);
}

} // namespace

bool read_file(const fs::path &path, std::string &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool write_file(const fs::path &path, std::string_view content, std::string &err) {
    std::error_code ec;
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path(), ec);
    if (ec) {
        err = "cannot create directory " + path.parent_path().string() + ": " + ec.message();
        return false;
    }

    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) {
            err = "cannot write " + tmp.string();
            return false;
        }
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!f) {
            err = "cannot write " + tmp.string();
            return false;
        }
    }

    fs::rename(tmp, path, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(path, ignored);
        ec.clear();
        fs::rename(tmp, path, ec);
    }
    if (ec) {
        err = "cannot rename " + tmp.string() + " to " + path.string() + ": " + ec.message();
        std::error_code ignored;
        fs::remove(tmp, ignored);
        return false;
    }
    return true;
}

bool BuildState::load(const fs::path &path) {
    std::ifstream f(path);
    if (!f) return false;

    schema = 1;
    toolchain.clear();
    files.clear();
    actions.clear();

    std::string line;
    if (!std::getline(f, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "CCOSTATE 1") return false;

    ActionRecord *current = nullptr;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream is(line);
        std::string tag;
        is >> tag;
        if (tag == "T") {
            std::string value;
            is >> value;
            toolchain = hex_decode(value);
        } else if (tag == "F") {
            std::uintmax_t size = 0;
            std::int64_t mtime = 0;
            std::string hash;
            std::string path_hex;
            is >> size >> mtime >> hash >> path_hex;
            if (!is) return false;
            std::string key = hex_decode(path_hex);
            if (key.empty()) return false;
            if (hash == "-") hash.clear();
            files[key] = FileRecord{size, mtime, hash};
        } else if (tag == "A") {
            std::string id_hex;
            std::string fp;
            is >> id_hex >> fp;
            if (!is) return false;
            std::string id = hex_decode(id_hex);
            if (id.empty()) return false;
            current = &actions[id];
            current->fingerprint = fp;
        } else if (tag == "D") {
            std::string path_hex;
            is >> path_hex;
            if (!is || !current) return false;
            current->deps.push_back(hex_decode(path_hex));
        } else if (tag == "O") {
            std::string path_hex;
            is >> path_hex;
            if (!is || !current) return false;
            current->outputs.push_back(hex_decode(path_hex));
        }
    }
    return true;
}

bool BuildState::save(const fs::path &path, std::string &err) const {
    std::ostringstream out;
    out << "CCOSTATE " << schema << "\n";
    out << "T " << hex_encode(toolchain) << "\n";
    for (const auto &[key, record] : files) {
        out << "F " << record.size << ' ' << record.mtime_ns << ' '
            << (record.hash.empty() ? "-" : record.hash) << ' ' << hex_encode(key) << "\n";
    }
    for (const auto &[id, record] : actions) {
        out << "A " << hex_encode(id) << ' ' << record.fingerprint << "\n";
        for (const auto &dep : record.deps) out << "D " << hex_encode(dep) << "\n";
        for (const auto &output : record.outputs) out << "O " << hex_encode(output) << "\n";
    }
    return write_file(path, out.str(), err);
}

Toolchain Toolchain::detect(const std::string &override_cxx) {
    Toolchain t;
#ifdef _MSC_VER
    t.kind = Kind::Msvc;
    t.cxx = "cl";
#else
    t.kind = Kind::Gnu;
    t.cxx = "c++";
#endif
    if (const char *env = std::getenv("CXX")) {
        if (*env) t.cxx = env;
    }
    if (!override_cxx.empty()) t.cxx = override_cxx;

    std::string base = fs::path(t.cxx).filename().string();
    std::transform(base.begin(), base.end(), base.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (base == "cl" || base == "cl.exe" || base == "clang-cl" || base == "clang-cl.exe") {
        t.kind = Kind::Msvc;
    } else {
        t.kind = Kind::Gnu;
    }
    t.fingerprint = toolchain_fingerprint(t);
    return t;
}

int run_program(const std::vector<std::string> &args, const RunOptions &opt) {
    if (args.empty()) return -1;
    if (opt.echo) {
        std::fprintf(stdout, "> %s\n", join_for_display(args).c_str());
    }
    std::fflush(stdout);
    std::fflush(stderr);

#ifdef _WIN32
    std::wstring cmdline = build_cmdline(args);
    std::wstring program = resolve_program(widen(args[0]));
    std::wstring cwd;
    if (!opt.cwd.empty()) cwd = opt.cwd.wstring();

    std::vector<wchar_t> mutable_cmd(cmdline.begin(), cmdline.end());
    mutable_cmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(program.c_str(), mutable_cmd.data(), nullptr, nullptr, TRUE, 0, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    if (!ok) {
        std::fprintf(stderr, "cco: cannot start `%s` (error %lu)\n", args[0].c_str(),
                     static_cast<unsigned long>(GetLastError()));
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
#else
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (const auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        std::fprintf(stderr, "cco: fork failed: %s\n", std::strerror(errno));
        return -1;
    }
    if (pid == 0) {
        if (!opt.cwd.empty()) {
            if (chdir(opt.cwd.c_str()) != 0) _exit(127);
        }
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

std::vector<fs::path> glob(std::string pattern) {
    std::replace(pattern.begin(), pattern.end(), '\\', '/');
    while (pattern.rfind("./", 0) == 0) pattern.erase(0, 2);

    std::error_code ec;
    fs::path direct(pattern);
    if (pattern.find_first_of("*?") == std::string::npos) {
        if (fs::is_regular_file(direct, ec)) return {direct};
        return {};
    }

    std::vector<std::string> pat = split_components(pattern);
    std::vector<fs::path> out;
    fs::path root = fs::current_path(ec);
    if (ec) return out;

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        fs::path rel = fs::relative(it->path(), root, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        std::string rels = rel.generic_string();
        std::vector<std::string> comps = split_components(rels);
        bool excluded = false;
        for (const auto &c : comps) {
            if (is_excluded_component(c)) {
                excluded = true;
                break;
            }
        }
        if (excluded) continue;
        if (match_components(pat, 0, comps, 0)) out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

Workspace::Workspace(int argc_, char **argv_) : argc(argc_), argv(argv_) {
    std::error_code ec;
    root = fs::current_path(ec);
    if (ec) root = ".";
    toolchain = Toolchain::detect();
}

Workspace &Workspace::managed_layout() {
    build_root_path = ".cco";
    return *this;
}

Workspace &Workspace::set_build_root(fs::path dir) {
    build_root_path = std::move(dir);
    return *this;
}

Target &Workspace::executable(std::string name, std::vector<fs::path> sources) {
    targets.emplace_back(std::move(name), Target::Kind::Executable);
    targets.back().sources(std::move(sources));
    return targets.back();
}

Target &Workspace::library(std::string name, std::vector<fs::path> sources) {
    targets.emplace_back(std::move(name), Target::Kind::Library);
    targets.back().sources(std::move(sources));
    return targets.back();
}

Target &Workspace::shared_library(std::string name, std::vector<fs::path> sources) {
    targets.emplace_back(std::move(name), Target::Kind::SharedLibrary);
    targets.back().sources(std::move(sources));
    return targets.back();
}

Target &Workspace::test(std::string name, std::vector<fs::path> sources) {
    targets.emplace_back(std::move(name), Target::Kind::Test);
    targets.back().sources(std::move(sources));
    return targets.back();
}

namespace {

struct BuildContext {
    const Toolchain &tc;
    fs::path root;
    fs::path build_root;
    bool verbose = false;
    bool dry_run = false;
    bool explain = false;
    BuildState *state = nullptr;
};

std::string obj_ext(const Toolchain &tc) { return tc.is_msvc() ? ".obj" : ".o"; }
std::string exe_ext() {
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}

fs::path build_base(const BuildContext &ctx) {
    if (ctx.build_root.empty()) return ctx.root;
    if (ctx.build_root.is_absolute()) return ctx.build_root;
    return ctx.root / ctx.build_root;
}

fs::path state_path(const BuildContext &ctx) {
    if (ctx.build_root.empty()) return ctx.root / "obj" / "state.txt";
    return build_base(ctx) / "state.txt";
}

std::string canonical_path(const fs::path &path) {
    std::error_code ec;
    fs::path abs = fs::absolute(path, ec);
    if (ec) abs = path;
    return abs.lexically_normal().generic_string();
}

std::string file_hash(BuildContext &ctx, const fs::path &path) {
    std::uintmax_t size = 0;
    std::int64_t mtime = 0;
    if (!stat_file(path, size, mtime)) return {};

    std::string key = canonical_path(path);
    if (ctx.state) {
        auto it = ctx.state->files.find(key);
        if (it != ctx.state->files.end() && it->second.size == size &&
            it->second.mtime_ns == mtime && !it->second.hash.empty()) {
            return it->second.hash;
        }
    }

    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    Sha256 sha;
    std::vector<char> buffer(1024 * 1024);
    while (f) {
        f.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize n = f.gcount();
        if (n > 0) sha.update(buffer.data(), static_cast<size_t>(n));
    }
    if (!f.eof()) return {};
    std::string hash = sha.hex();
    if (ctx.state) {
        ctx.state->files[key] = FileRecord{size, mtime, hash};
    }
    return hash;
}

fs::path depfile_for(const BuildContext &ctx, const fs::path &obj) {
    fs::path dep = obj;
    dep += ctx.tc.is_msvc() ? ".d.json" : ".d";
    return dep;
}

std::string json_unescape_string(std::string_view input) {
    std::string out;
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (c != '\\' || i + 1 >= input.size()) {
            out.push_back(c);
            continue;
        }
        char n = input[++i];
        switch (n) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        default: out.push_back(n); break;
        }
    }
    return out;
}

std::vector<std::string> parse_msvc_depfile(const fs::path &depfile, const fs::path &root) {
    std::vector<std::string> deps;
    std::string text;
    if (!read_file(depfile, text)) return deps;
    std::set<std::string> seen;
    for (const char *key : {"\"Path\"", "\"Source\""}) {
        size_t pos = 0;
        while ((pos = text.find(key, pos)) != std::string::npos) {
            pos += std::strlen(key);
            pos = text.find_first_not_of(" \t\r\n", pos);
            if (pos == std::string::npos || text[pos] != ':') continue;
            ++pos;
            pos = text.find_first_not_of(" \t\r\n", pos);
            if (pos == std::string::npos || text[pos] != '"') continue;
            size_t begin = ++pos;
            while (pos < text.size() && text[pos] != '"') {
                if (text[pos] == '\\') ++pos;
                ++pos;
            }
            if (pos > text.size()) break;
            std::string value = json_unescape_string(std::string_view(text).substr(begin, pos - begin));
            fs::path p(value);
            if (p.is_relative()) p = root / p;
            std::string canonical = canonical_path(p);
            if (seen.insert(canonical).second) deps.push_back(canonical);
            ++pos;
        }
    }
    std::sort(deps.begin(), deps.end());
    return deps;
}

std::vector<std::string> parse_depfile(const fs::path &depfile, const fs::path &root) {
    if (depfile.extension() == ".json") return parse_msvc_depfile(depfile, root);
    std::vector<std::string> deps;
    if (depfile.empty()) return deps;
    std::string text;
    if (!read_file(depfile, text)) return deps;

    std::string joined;
    joined.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == '\n') {
            ++i;
            joined.push_back(' ');
        } else if (text[i] == '\\' && i + 2 < text.size() && text[i + 1] == '\r' &&
                   text[i + 2] == '\n') {
            i += 2;
            joined.push_back(' ');
        } else {
            joined.push_back(text[i]);
        }
    }

    size_t colon = joined.find(": ");
    if (colon == std::string::npos) colon = joined.find(":\t");
    if (colon == std::string::npos) colon = joined.find(":\n");
    if (colon == std::string::npos) return deps;

    std::string rest = joined.substr(colon + 1);
    std::string current;
    bool escaped = false;
    std::set<std::string> seen;
    auto flush = [&] {
        if (current.empty()) return;
        fs::path p(current);
        if (p.is_relative()) p = root / p;
        std::string key = canonical_path(p);
        if (seen.insert(key).second) deps.push_back(key);
        current.clear();
    };
    for (char c : rest) {
        if (escaped) {
            current.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            flush();
        } else {
            current.push_back(c);
        }
    }
    if (escaped) current.push_back('\\');
    flush();
    std::sort(deps.begin(), deps.end());
    return deps;
}

std::string compile_fingerprint(BuildContext &ctx, const Target &t, const fs::path &src,
                                const fs::path &obj, const std::vector<std::string> &argv,
                                const std::vector<std::string> &deps) {
    std::vector<std::string> parts = {"compile", ctx.tc.fingerprint, ctx.root.generic_string(),
                                      t.name, canonical_path(src), canonical_path(obj)};
    std::string src_hash = file_hash(ctx, src);
    parts.push_back(src_hash.empty() ? "<missing>" : src_hash);
    for (const auto &arg : argv) parts.push_back(arg);
    for (const auto &dep : deps) {
        parts.push_back(canonical_path(dep));
        std::string hash = file_hash(ctx, dep);
        parts.push_back(hash.empty() ? "<missing>" : hash);
    }
    return make_fingerprint(parts);
}

fs::path shared_output_path(const BuildContext &ctx, const Target &t);

void collect_shared_outputs(const BuildContext &ctx, const Target &t, std::vector<fs::path> &out,
                            std::set<const Target *> &seen) {
    if (!seen.insert(&t).second) return;
    for (const Target *dep : t.links) {
        if (dep->kind == Target::Kind::SharedLibrary) out.push_back(shared_output_path(ctx, *dep));
        collect_shared_outputs(ctx, *dep, out, seen);
    }
}

std::string link_fingerprint(BuildContext &ctx, const Target &t, const fs::path &out,
                             const std::vector<fs::path> &objects,
                             const std::vector<fs::path> &shared_libs,
                             const std::vector<std::string> &argv) {
    std::vector<std::string> parts = {"link", ctx.tc.fingerprint, ctx.root.generic_string(),
                                      t.name, canonical_path(out)};
    for (const auto &arg : argv) parts.push_back(arg);
    for (const auto &obj : objects) {
        parts.push_back(canonical_path(obj));
        std::string hash = file_hash(ctx, obj);
        parts.push_back(hash.empty() ? "<missing>" : hash);
    }
    for (const auto &lib : shared_libs) {
        parts.push_back(canonical_path(lib));
        std::string hash = file_hash(ctx, lib);
        parts.push_back(hash.empty() ? "<missing>" : hash);
    }
    return make_fingerprint(parts);
}

fs::path object_path(const BuildContext &ctx, const Target &t, const fs::path &src) {
    std::error_code ec;
    fs::path abs_src = fs::absolute(src, ec);
    if (ec) abs_src = src;
    fs::path rel = fs::relative(abs_src, ctx.root, ec);
    if (ec || rel.empty()) rel = src.filename();
    std::string rels = rel.generic_string();
    if (rels.rfind("..", 0) == 0) rel = src.filename();

    fs::path base = ctx.build_root.empty() ? (ctx.root / "obj") : (build_base(ctx) / "obj");
    fs::path p = base / safe_component(t.name) / rel;
    p += obj_ext(ctx.tc);
    return p;
}

void add_unique(std::vector<std::string> &dst, const std::string &value) {
    if (std::find(dst.begin(), dst.end(), value) == dst.end()) dst.push_back(value);
}

fs::path target_output(const BuildContext &ctx, const Target &t) {
    if (!t.output.empty()) {
        if (t.output.is_absolute()) return t.output;
        return ctx.root / t.output;
    }
    if (!ctx.build_root.empty()) {
        return build_base(ctx) / "bin" / (safe_component(t.name) + exe_ext());
    }
    return ctx.root / "bin" / (safe_component(t.name) + exe_ext());
}

std::string shared_ext() {
#ifdef __APPLE__
    return ".dylib";
#elif defined(_WIN32)
    return ".dll";
#else
    return ".so";
#endif
}

std::string shared_prefix() {
#ifdef _WIN32
    return "";
#else
    return "lib";
#endif
}

fs::path shared_output_path(const BuildContext &ctx, const Target &t) {
    if (!t.output.empty()) {
        if (t.output.is_absolute()) return t.output;
        return ctx.root / t.output;
    }
    if (!ctx.build_root.empty()) {
        return build_base(ctx) / "bin" / (shared_prefix() + safe_component(t.name) + shared_ext());
    }
    return ctx.root / "bin" / (shared_prefix() + safe_component(t.name) + shared_ext());
}

void collect_shared_flags(const BuildContext &ctx, const Target &t, std::vector<std::string> &flags,
                          std::set<const Target *> &seen) {
    if (!seen.insert(&t).second) return;
    for (const Target *dep : t.links) {
        if (dep->kind == Target::Kind::SharedLibrary) {
            fs::path out = shared_output_path(ctx, *dep);
            add_unique(flags, "-L" + out.parent_path().string());
            add_unique(flags, "-l" + safe_component(dep->name));
#if defined(__APPLE__)
            add_unique(flags, "-Wl,-rpath,@loader_path");
#elif !defined(_WIN32)
            add_unique(flags, "-Wl,-rpath,$ORIGIN");
#endif
        }
        collect_shared_flags(ctx, *dep, flags, seen);
    }
}

void collect_flags(const Target &t, std::vector<std::string> &includes,
                   std::vector<std::string> &defines, std::vector<std::string> &linkflags,
                   std::set<const Target *> &seen) {
    if (!seen.insert(&t).second) return;
    for (const auto &i : t.include_dirs) add_unique(includes, i);
    for (const auto &d : t.define_list) add_unique(defines, d);
    for (const auto &l : t.linkflags) add_unique(linkflags, l);
    for (const Target *dep : t.links) collect_flags(*dep, includes, defines, linkflags, seen);
}

void collect_objects(const BuildContext &ctx, const Target &t, std::vector<fs::path> &objects,
                     std::set<const Target *> &seen) {
    if (!seen.insert(&t).second) return;
    for (const auto &src : t.source_files) objects.push_back(object_path(ctx, t, src));
    for (const Target *dep : t.links) {
        if (dep->kind == Target::Kind::SharedLibrary) continue;
        collect_objects(ctx, *dep, objects, seen);
    }
}

std::vector<std::string> compile_command(const BuildContext &ctx, const Target &t, const fs::path &src,
                                         const fs::path &obj, const std::vector<std::string> &includes,
                                         const std::vector<std::string> &defines,
                                         const fs::path &depfile = {}) {
    std::vector<std::string> cmd;
    cmd.push_back(ctx.tc.cxx);
    if (ctx.tc.is_msvc()) {
        cmd.push_back("/nologo");
        cmd.push_back("/std:" + t.cppstd);
        cmd.push_back("/EHsc");
        cmd.push_back("/utf-8");
    } else {
        cmd.push_back("-std=" + t.cppstd);
    }
    for (const auto &i : includes) cmd.push_back(ctx.tc.is_msvc() ? "/I" + i : "-I" + i);
    for (const auto &d : defines) cmd.push_back(ctx.tc.is_msvc() ? "/D" + d : "-D" + d);
    if (!ctx.tc.is_msvc() && t.kind == Target::Kind::SharedLibrary) cmd.push_back("-fPIC");
    for (const auto &f : t.cxxflags) cmd.push_back(f);
    if (!ctx.tc.is_msvc() && !depfile.empty()) {
        cmd.push_back("-MD");
        cmd.push_back("-MF");
        cmd.push_back(depfile.string());
        cmd.push_back("-MT");
        cmd.push_back(obj.string());
    }
    if (ctx.tc.is_msvc()) {
        if (!depfile.empty()) {
            cmd.push_back("/sourceDependencies");
            cmd.push_back(depfile.string());
        }
        cmd.push_back("/c");
        cmd.push_back(src.string());
        cmd.push_back("/Fo" + obj.string());
    } else {
        cmd.push_back("-c");
        cmd.push_back(src.string());
        cmd.push_back("-o");
        cmd.push_back(obj.string());
    }
    return cmd;
}

std::vector<std::string> link_command(const BuildContext &ctx,
                                      const std::vector<fs::path> &objects, const fs::path &out,
                                      const std::vector<std::string> &linkflags) {
    std::vector<std::string> cmd;
    cmd.push_back(ctx.tc.cxx);
    if (ctx.tc.is_msvc()) cmd.push_back("/nologo");
    for (const auto &obj : objects) cmd.push_back(obj.string());
    if (ctx.tc.is_msvc()) {
        cmd.push_back("/Fe:" + out.string());
    } else {
        cmd.push_back("-o");
        cmd.push_back(out.string());
    }
    for (const auto &f : linkflags) cmd.push_back(f);
    return cmd;
}

std::vector<std::string> shared_link_command(const BuildContext &ctx,
                                             const std::vector<fs::path> &objects,
                                             const fs::path &out,
                                             const std::vector<std::string> &linkflags) {
    std::vector<std::string> cmd;
    cmd.push_back(ctx.tc.cxx);
    if (ctx.tc.is_msvc()) cmd.push_back("/nologo");
    for (const auto &obj : objects) cmd.push_back(obj.string());
    if (ctx.tc.is_msvc()) {
        cmd.push_back("/LD");
        cmd.push_back("/Fe:" + out.string());
    } else {
#ifdef __APPLE__
        cmd.push_back("-dynamiclib");
#else
        cmd.push_back("-shared");
#endif
        cmd.push_back("-o");
        cmd.push_back(out.string());
    }
    for (const auto &f : linkflags) cmd.push_back(f);
    return cmd;
}

bool compile_one(BuildContext &ctx, const Target &t, const fs::path &src,
                 const std::vector<std::string> &includes, const std::vector<std::string> &defines) {
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) {
        std::fprintf(stderr, "cco: source file not found: %s\n", src.string().c_str());
        return false;
    }

    fs::path obj = object_path(ctx, t, src);
    fs::path depfile = depfile_for(ctx, obj);
    std::vector<std::string> cmd = compile_command(ctx, t, src, obj, includes, defines, depfile);

    std::string id = "compile:" + t.name + ":" + canonical_path(src);
    ActionRecord *prev = nullptr;
    if (ctx.state) {
        auto it = ctx.state->actions.find(id);
        if (it != ctx.state->actions.end()) prev = &it->second;
    }
    std::vector<std::string> deps = prev ? prev->deps : std::vector<std::string>{};
    std::string fingerprint = compile_fingerprint(ctx, t, src, obj, cmd, deps);
    bool has_output = file_is_regular(obj);

    if (prev && prev->fingerprint == fingerprint && has_output) {
        if (ctx.explain) {
            std::fprintf(stdout, "[skip] %s (cache hit)\n", obj.string().c_str());
        }
        return true;
    }

    if (ctx.explain) {
        const char *reason = !prev        ? "no cached action"
                             : !has_output ? "output is missing"
                                           : "inputs or command changed";
        std::fprintf(stdout, "[build] %s (%s)\n", obj.string().c_str(), reason);
    }

    ec.clear();
    fs::create_directories(obj.parent_path(), ec);
    if (ec) {
        std::fprintf(stderr, "cco: cannot create %s: %s\n", obj.parent_path().string().c_str(),
                     ec.message().c_str());
        return false;
    }
    if (ctx.dry_run) {
        std::fprintf(stdout, "> %s\n", join_for_display(cmd).c_str());
        return true;
    }

    int rc = run_program(cmd, RunOptions{ctx.root, ctx.verbose});
    if (rc != 0) {
        std::fprintf(stderr, "cco: compile failed (%d): %s\n", rc, src.string().c_str());
        return false;
    }

    std::vector<std::string> new_deps = parse_depfile(depfile, ctx.root);
    std::string final_fp = compile_fingerprint(ctx, t, src, obj, cmd, new_deps);
    if (ctx.state) {
        ctx.state->actions[id] = ActionRecord{final_fp, std::move(new_deps), {canonical_path(obj)}};
    }
    return true;
}
bool compile_target(BuildContext &ctx, const Target &t, std::set<const Target *> &compiled) {
    if (compiled.count(&t)) return true;
    for (const Target *dep : t.links) {
        if (dep->kind == Target::Kind::SharedLibrary) continue;
        if (!compile_target(ctx, *dep, compiled)) return false;
    }
    std::vector<std::string> includes, defines, linkflags;
    std::set<const Target *> seen;
    collect_flags(t, includes, defines, linkflags, seen);
    for (const auto &src : t.source_files) {
        if (!compile_one(ctx, t, src, includes, defines)) return false;
    }
    compiled.insert(&t);
    return true;
}

bool link_target(BuildContext &ctx, const Target &t, std::set<const Target *> &compiled,
                 std::set<const Target *> &linked) {
    if (t.kind == Target::Kind::Library) return true;

    // Shared libraries are built first; their objects are not folded into
    // the consumer, only their import/link flags are.
    for (const Target *dep : t.links) {
        if (dep->kind == Target::Kind::SharedLibrary) {
            if (!link_target(ctx, *dep, compiled, linked)) return false;
        }
    }

    if (!compile_target(ctx, t, compiled)) return false;
    if (linked.count(&t)) return true;

    if (t.kind == Target::Kind::SharedLibrary && ctx.tc.is_msvc()) {
        std::fprintf(stderr,
                     "cco: shared libraries are not implemented for MSVC yet (`%s`)\n",
                     t.name.c_str());
        return false;
    }

    std::vector<fs::path> objects;
    std::set<const Target *> obj_seen;
    collect_objects(ctx, t, objects, obj_seen);
    if (objects.empty()) {
        std::fprintf(stderr, "cco: target `%s` has no sources\n", t.name.c_str());
        return false;
    }

    const bool shared = t.kind == Target::Kind::SharedLibrary;
    fs::path out = shared ? shared_output_path(ctx, t) : target_output(ctx, t);

    std::vector<std::string> includes, defines, linkflags;
    std::set<const Target *> flag_seen;
    collect_flags(t, includes, defines, linkflags, flag_seen);
    std::set<const Target *> shared_seen;
    collect_shared_flags(ctx, t, linkflags, shared_seen);
    std::vector<fs::path> shared_libs;
    std::set<const Target *> shared_output_seen;
    collect_shared_outputs(ctx, t, shared_libs, shared_output_seen);

    std::vector<std::string> cmd =
        shared ? shared_link_command(ctx, objects, out, linkflags)
               : link_command(ctx, objects, out, linkflags);

    std::string id = "link:" + t.name;
    ActionRecord *prev = nullptr;
    if (ctx.state) {
        auto it = ctx.state->actions.find(id);
        if (it != ctx.state->actions.end()) prev = &it->second;
    }
    std::string fingerprint = link_fingerprint(ctx, t, out, objects, shared_libs, cmd);
    bool has_output = file_is_regular(out);

    if (prev && prev->fingerprint == fingerprint && has_output) {
        if (ctx.explain) {
            std::fprintf(stdout, "[skip] %s (cache hit)\n", out.string().c_str());
        }
        linked.insert(&t);
        return true;
    }

    if (ctx.explain) {
        const char *reason = !prev        ? "no cached action"
                             : !has_output ? "output is missing"
                                           : "inputs or command changed";
        std::fprintf(stdout, "[link] %s (%s)\n", out.string().c_str(), reason);
    }

    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    if (ec) {
        std::fprintf(stderr, "cco: cannot create %s: %s\n", out.parent_path().string().c_str(),
                     ec.message().c_str());
        return false;
    }
    if (ctx.dry_run) {
        std::fprintf(stdout, "> %s\n", join_for_display(cmd).c_str());
        linked.insert(&t);
        return true;
    }
    int rc = run_program(cmd, RunOptions{ctx.root, ctx.verbose});
    if (rc != 0) {
        std::fprintf(stderr, "cco: link failed (%d): %s\n", rc, out.string().c_str());
        return false;
    }
    if (ctx.state) {
        ctx.state->actions[id] = ActionRecord{fingerprint, {}, {canonical_path(out)}};
    }
    std::fprintf(stdout, "built %s\n", out.string().c_str());
    linked.insert(&t);
    return true;
}
bool build_target(BuildContext &ctx, const Target &t, std::set<const Target *> &compiled,
                  std::set<const Target *> &linked) {
    if (t.kind == Target::Kind::Library) return compile_target(ctx, t, compiled);
    return link_target(ctx, t, compiled, linked);
}

Target *find_target(std::deque<Target> &targets, const std::string &name) {
    for (auto &t : targets) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

void print_usage(const char *exe) {
    std::fprintf(stdout,
                 "usage: %s [command] [targets] [options]\n"
                 "\n"
                 "commands:\n"
                 "  build (default)   build targets\n"
                 "  run <target>      build and run an executable/test target\n"
                 "  test [targets]    build and run test targets\n"
                 "  compdb            write compile_commands.json\n"
                 "  clean             remove .cco/\n"
                 "  explain           build and explain rebuild decisions\n"
                 "\n"
                 "options:\n"
                 "  --cxx <path>      compiler to use (default: $CXX or c++/cl)\n"
                 "  --jobs <n>        accepted for compatibility (prototype is sequential)\n"
                 "  --verbose         echo commands\n"
                 "  --dry-run         print commands without running them\n"
                 "  --explain         print why actions run or are skipped\n"
                 "  --no-color        accepted for compatibility\n"
                 "  --help            show this help\n",
                 exe);
}

} // namespace

int Workspace::run() {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    std::vector<std::string> positional;
    std::vector<std::string> program_args;
    bool after_dashdash = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string &a = args[i];
        if (after_dashdash) {
            program_args.push_back(a);
            continue;
        }
        if (a == "--") {
            after_dashdash = true;
        } else if (a == "--verbose") {
            verbose = true;
        } else if (a == "--dry-run") {
            dry_run = true;
        } else if (a == "--explain") {
            explain = true;
        } else if (a == "--no-color") {
            // accepted, no-op
        } else if (a == "--cxx") {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "cco: --cxx expects a value\n");
                return 2;
            }
            cxx_override = args[++i];
        } else if (a == "--jobs") {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "cco: --jobs expects a value\n");
                return 2;
            }
            ++i;
        } else if (a == "--help" || a == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "cco: unknown option: %s\n", a.c_str());
            return 2;
        } else {
            positional.push_back(a);
        }
    }

    toolchain = Toolchain::detect(cxx_override);

    static const std::set<std::string> commands = {"build", "run",   "test",  "compdb",
                                                   "clean", "explain", "graph", "bsp",
                                                   "watch"};
    std::string command = "build";
    std::vector<std::string> target_names;
    if (!positional.empty() && commands.count(positional[0])) {
        command = positional[0];
        target_names.assign(positional.begin() + 1, positional.end());
    } else {
        target_names = positional;
    }

    if (command == "clean") {
        BuildContext cctx{toolchain, root, build_root_path, verbose, dry_run, explain};
        std::error_code ec;
        if (!cctx.build_root.empty()) {
            fs::path base = build_base(cctx);
            fs::remove_all(base, ec);
            if (ec) {
                std::fprintf(stderr, "cco: clean failed: %s\n", ec.message().c_str());
                return 1;
            }
            std::fprintf(stdout, "removed %s\n", base.string().c_str());
        } else {
            fs::remove_all(root / "obj", ec);
            if (ec) {
                std::fprintf(stderr, "cco: clean failed: %s\n", ec.message().c_str());
                return 1;
            }
            auto remove_empty_parents = [&](const fs::path &out) {
                std::error_code dir_ec;
                fs::path dir = out.parent_path();
                while (!dir.empty()) {
                    std::error_code rel_ec;
                    fs::path rel = fs::relative(dir, root, rel_ec);
                    if (rel_ec || rel.empty() || rel == ".") break;
                    std::string rels = rel.generic_string();
                    if (rels == ".." || rels.rfind("../", 0) == 0) break;
                    if (!fs::is_directory(dir, dir_ec) || dir_ec) break;
                    if (!fs::remove(dir, dir_ec) || dir_ec) break;
                    dir = dir.parent_path();
                }
            };
            for (const auto &t : targets) {
                if (t.kind == Target::Kind::Library) continue;
                fs::path out = t.kind == Target::Kind::SharedLibrary
                                   ? shared_output_path(cctx, t)
                                   : target_output(cctx, t);
                ec.clear();
                fs::remove(out, ec);
                if (ec) {
                    std::fprintf(stderr, "cco: clean failed: %s\n", ec.message().c_str());
                    return 1;
                }
                remove_empty_parents(out);
            }
            std::fprintf(stdout, "removed %s and target outputs\n",
                         (root / "obj").string().c_str());
        }
        return 0;
    }

    if (command == "graph" || command == "bsp" || command == "watch") {
        std::fprintf(stderr, "cco: `%s` is not implemented in this prototype\n", command.c_str());
        return 1;
    }

    if (command == "compdb") {
        std::vector<std::string> entries;
        std::set<std::string> seen_files;
        for (const auto &t : this->targets) {
            std::vector<std::string> includes, defines, linkflags;
            std::set<const Target *> seen;
            collect_flags(t, includes, defines, linkflags, seen);
            for (const auto &src : t.source_files) {
                std::error_code ec;
                fs::path abs_src = fs::absolute(src, ec);
                if (ec) abs_src = src;
                std::string key = abs_src.generic_string();
                if (!seen_files.insert(key).second) continue;
                fs::path obj = object_path(BuildContext{toolchain, root, build_root_path, verbose, dry_run, explain},
                                           t, src);
                std::vector<std::string> cmd = compile_command(
                    BuildContext{toolchain, root, build_root_path, verbose, dry_run, explain}, t, abs_src, obj, includes,
                    defines);
                std::string args_json;
                for (size_t i = 0; i < cmd.size(); ++i) {
                    if (i) args_json += ",";
                    args_json += "\"" + json_escape(cmd[i]) + "\"";
                }
                std::string entry = "{\"directory\":\"" + json_escape(root.string()) +
                                    "\",\"file\":\"" + json_escape(abs_src.string()) +
                                    "\",\"output\":\"" + json_escape(obj.string()) +
                                    "\",\"arguments\":[" + args_json + "]}";
                entries.push_back(entry);
            }
        }
        std::string json = "[\n";
        for (size_t i = 0; i < entries.size(); ++i) {
            json += "  " + entries[i];
            if (i + 1 < entries.size()) json += ",";
            json += "\n";
        }
        json += "]\n";
        std::string err;
        fs::path out = root / "compile_commands.json";
        if (!write_file(out, json, err)) {
            std::fprintf(stderr, "cco: %s\n", err.c_str());
            return 1;
        }
        std::fprintf(stdout, "wrote %s\n", out.string().c_str());
        return 0;
    }

    BuildContext ctx{toolchain, root, build_root_path, verbose, dry_run, explain};

    BuildState state;
    fs::path cache_path = state_path(ctx);
    state.load(cache_path);
    if (state.toolchain != toolchain.fingerprint) {
        state.actions.clear();
        state.toolchain = toolchain.fingerprint;
    }
    ctx.state = &state;
    auto save_state = [&] {
        if (dry_run) return;
        std::string err;
        if (!state.save(cache_path, err)) {
            std::fprintf(stderr, "cco: cannot save build state: %s\n", err.c_str());
        }
    };

    if (command == "run") {
        if (target_names.empty()) {
            std::fprintf(stderr, "cco: run expects a target\n");
            return 2;
        }
        Target *t = find_target(this->targets, target_names[0]);
        if (!t) {
            std::fprintf(stderr, "cco: no such target: %s\n", target_names[0].c_str());
            return 2;
        }
        if (t->kind == Target::Kind::Library || t->kind == Target::Kind::SharedLibrary) {
            std::fprintf(stderr, "cco: `%s` is a library and cannot be run\n", t->name.c_str());
            return 2;
        }
        std::set<const Target *> compiled, linked;
        if (!build_target(ctx, *t, compiled, linked)) {
            save_state();
            return 1;
        }
        save_state();
        if (dry_run) return 0;
        fs::path out = target_output(ctx, *t);
        std::vector<std::string> cmd = {out.string()};
        cmd.insert(cmd.end(), program_args.begin(), program_args.end());
        return run_program(cmd, RunOptions{root, false});
    }

    if (command == "test") {
        std::vector<Target *> selected;
        if (target_names.empty()) {
            for (auto &t : this->targets) {
                if (t.kind == Target::Kind::Test) selected.push_back(&t);
            }
        } else {
            for (const auto &name : target_names) {
                Target *t = find_target(this->targets, name);
                if (!t) {
                    std::fprintf(stderr, "cco: no such target: %s\n", name.c_str());
                    return 2;
                }
                selected.push_back(t);
            }
        }
        if (selected.empty()) {
            std::fprintf(stdout, "no test targets\n");
            return 0;
        }
        int failures = 0;
        for (Target *t : selected) {
            std::set<const Target *> compiled, linked;
            if (!build_target(ctx, *t, compiled, linked)) {
                save_state();
                return 1;
            }
            save_state();
            if (dry_run) continue;
            fs::path out = target_output(ctx, *t);
            std::fprintf(stdout, "running %s\n", t->name.c_str());
            int rc = run_program({out.string()}, RunOptions{root, false});
            if (rc != 0) {
                std::fprintf(stderr, "cco: test `%s` failed (%d)\n", t->name.c_str(), rc);
                ++failures;
            }
        }
        save_state();
        if (failures) {
            std::fprintf(stderr, "cco: %d test target(s) failed\n", failures);
            return 1;
        }
        std::fprintf(stdout, "all tests passed\n");
        return 0;
    }

    if (command == "explain") {
        explain = true;
        ctx.explain = true;
    }

    std::vector<Target *> selected;
    if (!target_names.empty()) {
        for (const auto &name : target_names) {
            Target *t = find_target(this->targets, name);
            if (!t) {
                std::fprintf(stderr, "cco: no such target: %s\n", name.c_str());
                return 2;
            }
            selected.push_back(t);
        }
    } else {
        for (auto &t : this->targets) {
            if (t.kind == Target::Kind::Executable || t.kind == Target::Kind::SharedLibrary) {
                selected.push_back(&t);
            }
        }
        if (selected.empty()) {
            for (auto &t : this->targets) {
                if (t.kind == Target::Kind::Library) selected.push_back(&t);
            }
        }
        if (selected.empty()) {
            for (auto &t : this->targets) {
                if (t.kind == Target::Kind::Test) selected.push_back(&t);
            }
        }
    }

    if (selected.empty()) {
        std::fprintf(stderr, "cco: no targets registered\n");
        return 2;
    }

    std::set<const Target *> compiled, linked;
    bool ok = true;
    for (Target *t : selected) {
        if (!build_target(ctx, *t, compiled, linked)) {
            ok = false;
            break;
        }
    }
    save_state();
    return ok ? 0 : 1;
}

void go_rebuild_urself(int argc, char **argv, const char *source_path, const char *header_path) {
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr) return;

    std::error_code ec;
    fs::path binary = current_executable_path(argv[0]);
#ifdef _WIN32
    if (binary.extension() != ".exe") binary += ".exe";
#endif

    bool need = false;
    auto bin_time = fs::last_write_time(binary, ec);
    if (ec) {
        need = true;
    } else {
        auto src_time = fs::last_write_time(source_path, ec);
        if (!ec && src_time > bin_time) need = true;
        ec.clear();
        auto hdr_time = fs::last_write_time(header_path, ec);
        if (!ec && hdr_time > bin_time) need = true;
    }
    if (!need) return;

    fs::path old = binary;
    old += ".old";
    bool had_binary = file_is_regular(binary);
    if (had_binary) {
        fs::rename(binary, old, ec);
        if (ec) {
            std::fprintf(stderr, "cco: cannot move `%s` aside: %s\n", binary.string().c_str(),
                         ec.message().c_str());
            return;
        }
    }

    std::vector<std::string> cmd;
#ifdef _MSC_VER
    cmd = {"cl", "/nologo", "/std:c++17", "/O2", "/Fe:" + binary.string(), source_path};
#else
    cmd = {"c++", "-std=c++17", "-O2", "-o", binary.string(), source_path};
#endif
    int rc = run_program(cmd, RunOptions{fs::current_path(), true});
    if (rc != 0) {
        std::fprintf(stderr, "cco: self-rebuild failed; restoring old binary\n");
        if (had_binary) {
            std::error_code restore_ec;
            fs::rename(old, binary, restore_ec);
        }
        std::exit(1);
    }

    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    args[0] = binary.string();

    int code = run_program(args, RunOptions{fs::current_path(), false});
    std::exit(code < 0 ? 1 : code);
}

} // namespace cco

#endif // CCO_IMPLEMENTATION

#endif // CCO_HPP
