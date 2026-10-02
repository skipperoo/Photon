#pragma once

#include <QString>

namespace photon {

class CrashReporter {
 public:
  static void install();
  static void setBreadcrumb(const QString& text);
  static QString crashDirectory();
  static QString takePendingCrashReport();

 private:
  static void installPosixHandlers();
  static void installWindowsHandler();
};

}  // namespace photon
