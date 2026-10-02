#include "CrashReporter.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <iterator>

#include "Version.h"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace photon {

namespace {

constexpr int kBreadcrumbSize = 256;
constexpr int kCrashDirSize = 1024;
constexpr int kCrashPathSize = kCrashDirSize + 512;

char s_crashDirUtf8[kCrashDirSize] = {};
QString s_crashDir;
char s_breadcrumbA[kBreadcrumbSize] = "startup";
char s_breadcrumbB[kBreadcrumbSize] = "startup";
std::atomic<char*> s_currentBreadcrumb{s_breadcrumbA};
std::atomic<bool> s_installed{false};

const char* currentBreadcrumb() {
  return s_currentBreadcrumb.load(std::memory_order_acquire);
}

void writeMarker(const char* reportPath) {
  char marker[kCrashPathSize];
  std::snprintf(marker, sizeof(marker), "%s/last-crash.txt", s_crashDirUtf8);
  FILE* file = std::fopen(marker, "w");
  if (!file) return;
  std::fputs(reportPath, file);
  std::fclose(file);
}

void writeTerminateReport() {
  char base[256];
  const std::time_t now = std::time(nullptr);
  std::tm tmNow{};
#ifdef _WIN32
  localtime_s(&tmNow, &now);
#else
  localtime_r(&now, &tmNow);
#endif
  std::snprintf(base, sizeof(base), "Photon-%s-%04d%02d%02d-%02d%02d%02d",
                PHOTON_VERSION_STRING, tmNow.tm_year + 1900, tmNow.tm_mon + 1,
                tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);

  char path[kCrashPathSize];
  std::snprintf(path, sizeof(path), "%s/%s.txt", s_crashDirUtf8, base);
  FILE* file = std::fopen(path, "w");
  if (file) {
    std::fprintf(file,
                 "Photon v%s crash report\nreason=std::terminate\n"
                 "breadcrumb=%s\n",
                 PHOTON_VERSION_STRING, currentBreadcrumb());
    std::fclose(file);
  }
  writeMarker(path);
}

#ifdef _WIN32

void appendWindowsTextReport(EXCEPTION_POINTERS* info, const char* base) {
  char path[kCrashPathSize];
  std::snprintf(path, sizeof(path), "%s\\%s.txt", s_crashDirUtf8, base);

  wchar_t widePath[kCrashPathSize];
  MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath,
                      static_cast<int>(std::size(widePath)));
  FILE* file = _wfopen(widePath, L"w");
  if (!file) return;

  const DWORD code =
      info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
  const void* address =
      info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress
                                    : nullptr;

  char moduleName[MAX_PATH] = "<unknown>";
  unsigned long long moduleBase = 0;
  if (address) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(address, &mbi, sizeof(mbi)) == sizeof(mbi) &&
        mbi.AllocationBase) {
      moduleBase = reinterpret_cast<unsigned long long>(mbi.AllocationBase);
      wchar_t wideModule[MAX_PATH] = {};
      if (GetModuleFileNameW(static_cast<HMODULE>(mbi.AllocationBase),
                             wideModule, MAX_PATH) > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wideModule, -1, moduleName,
                            sizeof(moduleName), nullptr, nullptr);
      }
    }
  }

  std::fprintf(file,
               "Photon v%s crash report\ntime=%s\nbreadcrumb=%s\n"
               "exception=0x%08lx\naddress=%p\nfaultingModule=%s+0x%llx\n\n"
               "modules:\n",
               PHOTON_VERSION_STRING,
               QDateTime::currentDateTime()
                   .toString("yyyy-MM-dd HH:mm:ss.zzz")
                   .toUtf8()
                   .constData(),
               currentBreadcrumb(), static_cast<unsigned long>(code), address,
               moduleName,
               address ? reinterpret_cast<unsigned long long>(address) -
                             moduleBase
                       : 0ULL);

  HANDLE snapshot = CreateToolhelp32Snapshot(
      TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
  if (snapshot != INVALID_HANDLE_VALUE) {
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
      do {
        char name[MAX_PATH] = {};
        WideCharToMultiByte(CP_UTF8, 0, entry.szModule, -1, name, sizeof(name),
                            nullptr, nullptr);
        std::fprintf(file, "  0x%p  %s\n",
                     reinterpret_cast<void*>(entry.modBaseAddr), name);
      } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
  }
  std::fclose(file);
}

void writeWindowsCrashReport(EXCEPTION_POINTERS* info) {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  char base[256];
  std::snprintf(base, sizeof(base), "Photon-%s-%04d%02d%02d-%02d%02d%02d",
                PHOTON_VERSION_STRING, now.wYear, now.wMonth, now.wDay,
                now.wHour, now.wMinute, now.wSecond);

  char dumpPath[kCrashPathSize];
  std::snprintf(dumpPath, sizeof(dumpPath), "%s\\%s.dmp", s_crashDirUtf8, base);
  wchar_t wideDumpPath[kCrashPathSize];
  MultiByteToWideChar(CP_UTF8, 0, dumpPath, -1, wideDumpPath,
                      static_cast<int>(std::size(wideDumpPath)));

  HANDLE file = CreateFileW(wideDumpPath, GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
    exceptionInfo.ThreadId = GetCurrentThreadId();
    exceptionInfo.ExceptionPointers = info;
    exceptionInfo.ClientPointers = FALSE;
    const auto dumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal | MiniDumpWithUnloadedModules);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                      dumpType, info ? &exceptionInfo : nullptr, nullptr,
                      nullptr);
    CloseHandle(file);
  }

  appendWindowsTextReport(info, base);
  writeMarker(dumpPath);
}

LONG WINAPI photonUnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
  writeWindowsCrashReport(info);
  return EXCEPTION_EXECUTE_HANDLER;
}

#else

void writePosixCrashReport(int signalNumber, siginfo_t* info) {
  char base[256];
  const std::time_t now = std::time(nullptr);
  std::tm tmNow{};
  localtime_r(&now, &tmNow);
  std::snprintf(base, sizeof(base), "Photon-%s-%04d%02d%02d-%02d%02d%02d",
                PHOTON_VERSION_STRING, tmNow.tm_year + 1900, tmNow.tm_mon + 1,
                tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);

  char path[kCrashPathSize];
  std::snprintf(path, sizeof(path), "%s/%s.txt", s_crashDirUtf8, base);
  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    char header[512];
    const int length = std::snprintf(
        header, sizeof(header),
        "Photon v%s crash report\nsignal=%d\naddress=%p\nbreadcrumb=%s\n\n"
        "backtrace:\n",
        PHOTON_VERSION_STRING, signalNumber,
        info ? info->si_addr : nullptr, currentBreadcrumb());
    if (length > 0) {
      ssize_t unused = write(fd, header, static_cast<size_t>(length));
      (void)unused;
    }
    void* frames[64];
    const int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, fd);
    close(fd);
  }
  writeMarker(path);
}

void photonPosixSignalHandler(int signalNumber, siginfo_t* info, void*) {
  writePosixCrashReport(signalNumber, info);
  signal(signalNumber, SIG_DFL);
  raise(signalNumber);
}

#endif

}  // namespace

void CrashReporter::install() {
  bool expected = false;
  if (!s_installed.compare_exchange_strong(expected, true)) return;

  s_crashDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
               "/crashes";
  QDir().mkpath(s_crashDir);
  const QByteArray crashDirUtf8 = s_crashDir.toUtf8();
  std::snprintf(s_crashDirUtf8, sizeof(s_crashDirUtf8), "%s",
                crashDirUtf8.constData());

  std::set_terminate([]() {
    writeTerminateReport();
    std::abort();
  });

#ifdef _WIN32
  installWindowsHandler();
#else
  installPosixHandlers();
#endif
}

void CrashReporter::installWindowsHandler() {
#ifdef _WIN32
  SetUnhandledExceptionFilter(&photonUnhandledExceptionFilter);
#endif
}

void CrashReporter::installPosixHandlers() {
#ifndef _WIN32
  struct sigaction action;
  std::memset(&action, 0, sizeof(action));
  action.sa_sigaction = &photonPosixSignalHandler;
  action.sa_flags = SA_SIGINFO;
  sigemptyset(&action.sa_mask);
  const int handledSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};
  for (int signalNumber : handledSignals) {
    sigaction(signalNumber, &action, nullptr);
  }
#endif
}

void CrashReporter::setBreadcrumb(const QString& text) {
  char* current = s_currentBreadcrumb.load(std::memory_order_acquire);
  char* target = (current == s_breadcrumbA) ? s_breadcrumbB : s_breadcrumbA;
  const QByteArray utf8 = text.toUtf8();
  std::snprintf(target, kBreadcrumbSize, "%s", utf8.constData());
  s_currentBreadcrumb.store(target, std::memory_order_release);
}

QString CrashReporter::crashDirectory() { return s_crashDir; }

QString CrashReporter::takePendingCrashReport() {
  const QString marker = s_crashDir + "/last-crash.txt";
  QFile file(marker);
  if (!file.exists()) return QString();
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
  const QString reportPath = QString::fromUtf8(file.readAll()).trimmed();
  file.close();
  QFile::rename(marker, marker + ".reported");
  return reportPath;
}

}  // namespace photon
