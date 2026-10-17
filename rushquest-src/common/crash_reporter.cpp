// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/arch.h"
#include "common/crash_reporter.h"

#ifdef _WIN32
#include <Windows.h>
#include <dbghelp.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <cstdlib>

namespace Common {
namespace {
std::atomic_flag dump_started = ATOMIC_FLAG_INIT;
std::mutex stream_mutex;
std::uint64_t stream_misses{};
std::string first_stream_miss;
std::string last_stream_miss;

std::filesystem::path DiagnosticsDirectory() {
    if (const char* configured = std::getenv("BLOODRUSH_DIAGNOSTICS_DIR");
        configured && *configured) {
        return std::filesystem::path(configured);
    }
    return std::filesystem::current_path() / "user" / "log";
}

void AppendRecentLog(std::ofstream& report) {
    const auto log = std::filesystem::current_path() / "user" / "log" / "shad_log.txt";
    std::ifstream input(log, std::ios::binary);
    if (!input) return;
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0) return;
    constexpr std::streamoff MaxTail = 256 * 1024;
    input.seekg(size > MaxTail ? size - MaxTail : std::streampos{0}, std::ios::beg);
    std::string tail(static_cast<std::size_t>(std::min<std::streamoff>(size, MaxTail)), '\0');
    input.read(tail.data(), static_cast<std::streamsize>(tail.size()));

    std::deque<std::string> useful;
    std::uint64_t stream_lines{};
    std::string first_stream, last_stream;
    std::size_t begin{};
    while (begin < tail.size()) {
        const auto end = tail.find('\n', begin);
        std::string line = tail.substr(begin, end == std::string::npos ? end : end - begin);
        begin = end == std::string::npos ? tail.size() : end + 1;
        if (line.find(".corestream") != std::string::npos) {
            ++stream_lines;
            if (first_stream.empty()) first_stream = line;
            last_stream = std::move(line);
            continue;
        }
        if (line.find("<Warning>") == std::string::npos &&
            line.find("<Error>") == std::string::npos &&
            line.find("<Critical>") == std::string::npos &&
            line.find("GPU_Model:") == std::string::npos &&
            line.find("GPU_Vulkan_Driver:") == std::string::npos &&
            line.find("Revision ") == std::string::npos &&
            line.find("Starting shadps4") == std::string::npos &&
            line.find("Headset found through") == std::string::npos &&
            line.find("Virtual headset connected") == std::string::npos) {
            continue;
        }
        if (useful.size() == 60) useful.pop_front();
        useful.push_back(std::move(line));
    }
    report << "\nRecent game log context (filtered from last 256 KiB):\n";
    for (const auto& line : useful) report << line << '\n';
    if (stream_lines != 0) {
        report << "Repeated .corestream log records in tail: " << stream_lines << '\n'
               << "First: " << first_stream << '\n'
               << "Last: " << last_stream << '\n';
    }
}

LONG WINAPI UnhandledException(EXCEPTION_POINTERS* exception) {
    WriteCrashDump("Unhandled Windows exception", exception);
    return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace

void InitCrashReporter() {
    SetUnhandledExceptionFilter(UnhandledException);
}

void ReportCrash(void*, int, void*) {}

void RecordCorestreamMiss(const char* guest_path) {
    std::lock_guard lock(stream_mutex);
    ++stream_misses;
    if (first_stream_miss.empty() && guest_path) first_stream_miss = guest_path;
    if (guest_path) last_stream_miss = guest_path;
}

void WriteCrashDump(const char* reason, void* exception_pointers) {
    if (dump_started.test_and_set()) return;
    std::error_code error;
    const auto directory = DiagnosticsDirectory();
    std::filesystem::create_directories(directory, error);
    const auto report_path = directory / "report.txt";
    const auto dump_path = directory / "crash.dmp";
    {
        std::ofstream report(report_path, std::ios::app);
        report << "Crash reason: " << (reason ? reason : "unspecified fatal failure") << '\n'
               << "Process ID: " << GetCurrentProcessId() << '\n'
               << "Thread ID: " << GetCurrentThreadId() << '\n'
               << "Crash dump: " << dump_path.string() << '\n';
        if (exception_pointers) {
            const auto* pointers = static_cast<const EXCEPTION_POINTERS*>(exception_pointers);
            if (pointers->ExceptionRecord) {
                report << "Exception code: 0x" << std::hex
                       << pointers->ExceptionRecord->ExceptionCode << "\nException address: "
                       << pointers->ExceptionRecord->ExceptionAddress << std::dec << '\n';
            }
        }
        {
            std::lock_guard lock(stream_mutex);
            if (stream_misses) {
                report << "Missing .corestream opens: " << stream_misses << '\n'
                       << "First missing .corestream: " << first_stream_miss << '\n'
                       << "Last missing .corestream: " << last_stream_miss << '\n';
            }
        }
        AppendRecentLog(report);
    }

    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    if (!dbghelp) {
        std::ofstream(report_path, std::ios::app) << "Dump failure: dbghelp.dll unavailable ("
                                                   << GetLastError() << ")\n";
        return;
    }
    using WriteDump = decltype(&MiniDumpWriteDump);
    const auto write_dump = reinterpret_cast<WriteDump>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    if (!write_dump) {
        std::ofstream(report_path, std::ios::app) << "Dump failure: MiniDumpWriteDump unavailable ("
                                                   << GetLastError() << ")\n";
        FreeLibrary(dbghelp);
        return;
    }
    HANDLE file = CreateFileW(dump_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::ofstream(report_path, std::ios::app) << "Dump failure: could not create file ("
                                                   << GetLastError() << ")\n";
        FreeLibrary(dbghelp);
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION exception_info{};
    MINIDUMP_EXCEPTION_INFORMATION* exception_info_ptr = nullptr;
    if (exception_pointers) {
        exception_info.ThreadId = GetCurrentThreadId();
        exception_info.ExceptionPointers = static_cast<EXCEPTION_POINTERS*>(exception_pointers);
        exception_info.ClientPointers = FALSE;
        exception_info_ptr = &exception_info;
    }
    constexpr auto dump_type = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithFullMemory | MiniDumpWithThreadInfo | MiniDumpWithHandleData |
        MiniDumpWithUnloadedModules | MiniDumpWithFullMemoryInfo);
    const BOOL ok = write_dump(GetCurrentProcess(), GetCurrentProcessId(), file, dump_type,
                               exception_info_ptr, nullptr, nullptr);
    const DWORD dump_error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    FreeLibrary(dbghelp);
    std::ofstream result(report_path, std::ios::app);
    if (ok) {
        result << "Full memory dump written successfully\n";
    } else {
        result << "Dump failure: MiniDumpWriteDump returned error " << dump_error << '\n';
    }
}
} // namespace Common
#else

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifdef ARCH_X86_64
#include <sys/ucontext.h>
#endif

namespace Common {
namespace {

static bool gCrashReporterEnabled = false;

void safe_write(int fd, const char* buf, size_t len) {
    for (size_t written = 0; written < len;) {
        ssize_t n = write(fd, buf + written, len - written);
        if (n <= 0) break;
        written += (size_t)n;
    }
}

void safe_write_str(int fd, const char* s) {
    safe_write(fd, s, std::strlen(s));
}

void safe_write_hex(int fd, uint64_t v) {
    char buf[19];
    int i = 18;
    buf[18] = '\0';
    if (v == 0) {
        buf[--i] = '0';
    } else {
        while (v) {
            int d = (int)(v & 0xF);
            buf[--i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
            v >>= 4;
        }
    }
    buf[--i] = 'x';
    buf[--i] = '0';
    safe_write(fd, buf + i, (size_t)(18 - i));
}

void safe_write_dec(int fd, int v) {
    char buf[12];
    int i = 11;
    buf[11] = '\0';
    if (v == 0) {
        buf[--i] = '0';
    } else {
        unsigned int u = v < 0 ? (unsigned int)(-v) : (unsigned int)v;
        while (u) {
            buf[--i] = (char)('0' + (u % 10));
            u /= 10;
        }
        if (v < 0) buf[--i] = '-';
    }
    safe_write(fd, buf + i, (size_t)(11 - i));
}

} // anonymous namespace

void WriteCrashDump(const char*, void*) {}
void RecordCorestreamMiss(const char*) {}

void LogBinaryIdentity() {
#ifndef _WIN32
    // Stage 2 launch logging: emit the exact binary being launched so a crash
    // record can be symbolized against the matching .debug companion.  Runs once
    // at startup (never in a signal handler), so /proc parsing is acceptable.
    char exe_path[4096];
    ssize_t plen = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (plen <= 0) return;
    exe_path[plen] = '\0';

    safe_write_str(STDOUT_FILENO, "[Bachata.Symbols] binary_path=");
    safe_write_str(STDOUT_FILENO, exe_path);
    safe_write_str(STDOUT_FILENO, "\n");

    // Parse the ELF PT_NOTE to extract the GNU build-id.  Only the ELF + program
    // headers are touched, so even a fully-section-stripped binary works.
    int fd = open(exe_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        safe_write_str(STDOUT_FILENO, "[Bachata.Symbols] build_id=open_failed\n");
        return;
    }
    unsigned char hdr[64];
    if (read(fd, hdr, 64) != 64 || hdr[0] != 0x7f || hdr[1] != 'E') {
        safe_write_str(STDOUT_FILENO, "[Bachata.Symbols] build_id=not_elf\n");
        close(fd);
        return;
    }
    uint64_t e_phoff = *(uint64_t*)(hdr + 0x20);
    uint16_t e_phentsize = *(uint16_t*)(hdr + 0x36);
    uint16_t e_phnum = *(uint16_t*)(hdr + 0x38);
    unsigned char ph[16 * 64];
    size_t phbytes = (size_t)e_phnum * e_phentsize;
    if (phbytes > sizeof(ph)) phbytes = sizeof(ph);
    if (lseek(fd, (off_t)e_phoff, SEEK_SET) < 0 || read(fd, ph, phbytes) != (ssize_t)phbytes) {
        safe_write_str(STDOUT_FILENO, "[Bachata.Symbols] build_id=phdr_read_failed\n");
        close(fd);
        return;
    }
    bool found = false;
    for (int i = 0; i < e_phnum && !found; ++i) {
        unsigned char* p = ph + (size_t)i * e_phentsize;
        if (*(uint32_t*)(p + 0) != 4) continue; // PT_NOTE
        uint64_t p_offset = *(uint64_t*)(p + 8);
        uint64_t p_filesz = *(uint64_t*)(p + 0x20);
        if (p_filesz == 0 || p_filesz > 0x10000) continue;
        unsigned char seg[65536];
        if (lseek(fd, (off_t)p_offset, SEEK_SET) < 0) continue;
        if (read(fd, seg, (size_t)p_filesz) != (ssize_t)p_filesz) continue;
        uint64_t o = 0;
        while (o + 12 <= p_filesz) {
            uint32_t namesz = *(uint32_t*)(seg + o + 0);
            uint32_t descsz = *(uint32_t*)(seg + o + 4);
            uint32_t ntype = *(uint32_t*)(seg + o + 8);
            uint32_t namepad = (namesz + 3) & ~3u;
            uint32_t descpad = (descsz + 3) & ~3u;
            uint64_t consumed = 12 + namepad + descpad;
            if (consumed > p_filesz - o) break;
            if (ntype == 3 && namesz == 4 && descsz == 20 &&
                seg[o + 12] == 'G' && seg[o + 13] == 'N' && seg[o + 14] == 'U') {
                // NT_GNU_BUILD_ID: 20-byte SHA1.
                safe_write_str(STDOUT_FILENO, "[Bachata.Symbols] build_id=");
                const unsigned char* d = seg + o + 12 + namepad;
                for (int b = 0; b < 20; ++b) {
                    char hex[3];
                    hex[0] = (char)("0123456789abcdef"[d[b] >> 4]);
                    hex[1] = (char)("0123456789abcdef"[d[b] & 0xF]);
                    hex[2] = '\0';
                    safe_write_str(STDOUT_FILENO, hex);
                }
                safe_write_str(STDOUT_FILENO, "\n");
                safe_write_str(STDOUT_FILENO,
                               "[Bachata.Symbols] debug_bundle=available\n");
                found = true;
                break;
            }
            o += consumed;
        }
    }
    if (!found) {
        safe_write_str(STDOUT_FILENO, "[Bachata.Symbols] build_id=not_found\n");
    }
    close(fd);
#endif
}

void InitCrashReporter() {
#ifdef _WIN32
    gCrashReporterEnabled = false;
#else
    gCrashReporterEnabled = getenv("BACHATA_CRASH_REGISTERS") != nullptr;
    if (gCrashReporterEnabled) {
        safe_write_str(STDOUT_FILENO, "[Bachata.Crash] reporter enabled\n");
        LogBinaryIdentity();
    }
#endif
}

void dump_maps(int fd) {
    int mapfd = open("/proc/self/maps", O_RDONLY);
    if (mapfd < 0) {
        safe_write_str(fd, "\n[Bachata.Crash] maps open failed\n");
        return;
    }
    safe_write_str(fd, "\n[Bachata.Crash] maps begin\n");
    char buf[4096];
    ssize_t nr;
    while ((nr = read(mapfd, buf, sizeof(buf))) > 0) {
        safe_write(fd, buf, (size_t)nr);
    }
    safe_write_str(fd, "[Bachata.Crash] maps end\n");
    close(mapfd);
}

void ReportCrash(void* raw_context, int signum, void* siginfo_ptr) {
    if (!gCrashReporterEnabled || raw_context == nullptr) return;

    int fd = STDOUT_FILENO;

#ifdef _WIN32
    safe_write_str(fd, "\n[Bachata.Crash] registers not available on Windows\n");
#elif defined(ARCH_X86_64)
    auto* ctx = static_cast<const ucontext_t*>(raw_context);
    void* fault_addr = siginfo_ptr ? reinterpret_cast<siginfo_t*>(siginfo_ptr)->si_addr : nullptr;
    int sig_code = siginfo_ptr ? reinterpret_cast<const siginfo_t*>(siginfo_ptr)->si_code : 0;

    // Thread identity
    char thread_name[16] = {};
    safe_write_str(fd, "\n[Bachata.Crash] tid=");
    safe_write_dec(fd, (int)gettid());

    // Signal info
    safe_write_str(fd, " signal=");
    safe_write_dec(fd, signum);
    safe_write_str(fd, " code=");
    safe_write_dec(fd, sig_code);

    // Registers
    safe_write_str(fd, "\n[Bachata.Crash] pc=");
    safe_write_hex(fd, ctx->uc_mcontext.gregs[REG_RIP]);
    safe_write_str(fd, " sp=");
    safe_write_hex(fd, ctx->uc_mcontext.gregs[REG_RSP]);
    safe_write_str(fd, " bp=");
    safe_write_hex(fd, ctx->uc_mcontext.gregs[REG_RBP]);

    safe_write_str(fd, "\n[Bachata.Crash] rax=");
    safe_write_hex(fd, ctx->uc_mcontext.gregs[REG_RAX]);
    safe_write_str(fd, " rbx=");
    safe_write_hex(fd, ctx->uc_mcontext.gregs[REG_RBX]);
    safe_write_str(fd, " rcx=");
    safe_write_hex(fd, ctx->uc_mcontext.gregs[REG_RCX]);
    safe_write_str(fd, " rdx=");
    safe_write_hex(fd, ctx->uc_mcontext.gregs[REG_RDX]);

    safe_write_str(fd, "\n[Bachata.Crash] rsi=");
    safe_write_hex(fd, ctx->uc_mcontext.gregs[REG_RSI]);
    safe_write_str(fd, " rdi=");
    safe_write_hex(fd, ctx->uc_mcontext.gregs[REG_RDI]);
    safe_write_str(fd, " r8=");
    safe_write_hex(fd, ctx->uc_mcontext.gregs[REG_R8]);
    safe_write_str(fd, " r9=");
    safe_write_hex(fd, ctx->uc_mcontext.gregs[REG_R9]);

    safe_write_str(fd, "\n[Bachata.Crash] r10=");
    safe_write_hex(fd, ctx->uc_mcontext.gregs[REG_R10]);
    safe_write_str(fd, " r11=");
    safe_write_hex(fd, ctx->uc_mcontext.gregs[REG_R11]);
    safe_write_str(fd, " fault=");
    safe_write_hex(fd, reinterpret_cast<uint64_t>(fault_addr));
    safe_write_str(fd, "\n");
#elif defined(ARCH_ARM64)
    auto* ctx = static_cast<const ucontext_t*>(raw_context);
    void* fault_addr = siginfo_ptr ? reinterpret_cast<siginfo_t*>(siginfo_ptr)->si_addr : nullptr;
    int sig_code = siginfo_ptr ? reinterpret_cast<const siginfo_t*>(siginfo_ptr)->si_code : 0;

    safe_write_str(fd, "\n[Bachata.Crash] tid=");
    safe_write_dec(fd, (int)gettid());
    safe_write_str(fd, " signal=");
    safe_write_dec(fd, signum);
    safe_write_str(fd, " code=");
    safe_write_dec(fd, sig_code);

    safe_write_str(fd, "\n[Bachata.Crash] pc=");
    safe_write_hex(fd, (uint64_t)ctx->uc_mcontext.pc);
    safe_write_str(fd, " sp=");
    safe_write_hex(fd, (uint64_t)ctx->uc_mcontext.sp);
    safe_write_str(fd, " fp=");
    safe_write_hex(fd, ctx->uc_mcontext.regs[29]);
    safe_write_str(fd, " lr=");
    safe_write_hex(fd, ctx->uc_mcontext.regs[30]);

    safe_write_str(fd, "\n[Bachata.Crash] x0=");
    safe_write_hex(fd, ctx->uc_mcontext.regs[0]);
    safe_write_str(fd, " x1=");
    safe_write_hex(fd, ctx->uc_mcontext.regs[1]);
    safe_write_str(fd, " x2=");
    safe_write_hex(fd, ctx->uc_mcontext.regs[2]);
    safe_write_str(fd, " x3=");
    safe_write_hex(fd, ctx->uc_mcontext.regs[3]);

    safe_write_str(fd, "\n[Bachata.Crash] x4=");
    safe_write_hex(fd, ctx->uc_mcontext.regs[4]);
    safe_write_str(fd, " x5=");
    safe_write_hex(fd, ctx->uc_mcontext.regs[5]);
    safe_write_str(fd, " x6=");
    safe_write_hex(fd, ctx->uc_mcontext.regs[6]);
    safe_write_str(fd, " x7=");
    safe_write_hex(fd, ctx->uc_mcontext.regs[7]);
    safe_write_str(fd, " x8=");
    safe_write_hex(fd, ctx->uc_mcontext.regs[8]);

    safe_write_str(fd, " fault=");
    safe_write_hex(fd, reinterpret_cast<uint64_t>(fault_addr));
    safe_write_str(fd, "\n");

    // Dump callee-saved regs that BindVertexBuffers uses for dispatch lookup
    safe_write_str(fd, "[Bachata.Crash] x19=");
    safe_write_hex(fd, ctx->uc_mcontext.regs[19]);
    safe_write_str(fd, " x26=");
    safe_write_hex(fd, ctx->uc_mcontext.regs[26]);
    safe_write_str(fd, "\n");

    if (signum == SIGSEGV && (uint64_t)ctx->uc_mcontext.pc == 0) {
        dump_maps(fd);
    }
#endif
}

} // namespace Common

#endif // _WIN32
