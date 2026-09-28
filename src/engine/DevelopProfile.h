#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace photon {

namespace develop {

inline constexpr const char* kProfileNormal = "normal";
inline constexpr const char* kProfileAgx = "agx";
inline constexpr const char* kProfileBlackAndWhite = "black_and_white";

inline QString normalizeProfile(const QString& profile) {
  if (profile.isEmpty()) return QString::fromLatin1(kProfileNormal);
  if (profile == QLatin1String("blackAndWhite")) {
    return QString::fromLatin1(kProfileBlackAndWhite);
  }
  return profile;
}

inline bool isBlackAndWhite(const QString& profile) {
  return normalizeProfile(profile) == QLatin1String(kProfileBlackAndWhite);
}

inline int profileToIndex(const QString& profile) {
  const QString normalized = normalizeProfile(profile);
  if (normalized == QLatin1String(kProfileAgx)) return 1;
  if (normalized == QLatin1String(kProfileBlackAndWhite)) return 2;
  return 0;
}

inline QString profileDisplayName(const QString& profile) {
  const QString normalized = normalizeProfile(profile);
  if (normalized == QLatin1String(kProfileNormal)) {
    return QStringLiteral("Normal");
  }
  if (normalized == QLatin1String(kProfileAgx)) {
    return QStringLiteral("AgX");
  }
  if (normalized == QLatin1String(kProfileBlackAndWhite)) {
    return QStringLiteral("Black & White");
  }

  // Fallback for profiles added in the future: underscore-joined composite
  // tokens are shown as space separated, capitalized words.
  QString display = normalized;
  display.replace('_', ' ');
  bool capitalize = true;
  for (int i = 0; i < display.size(); ++i) {
    if (capitalize && display.at(i).isLetter()) {
      display[i] = display.at(i).toUpper();
      capitalize = false;
    } else if (display.at(i) == ' ') {
      capitalize = true;
    }
  }
  return display;
}

inline QVariantList profileOptions() {
  QVariantList options;
  const char* tokens[3] = {kProfileNormal, kProfileAgx, kProfileBlackAndWhite};
  for (const char* token : tokens) {
    QVariantMap option;
    option["value"] = QString::fromLatin1(token);
    option["label"] = profileDisplayName(QString::fromLatin1(token));
    options.append(option);
  }
  return options;
}

inline QString legacyTonemappingToProfile(bool agx) {
  return QString::fromLatin1(agx ? kProfileAgx : kProfileNormal);
}

}  // namespace develop

}  // namespace photon
