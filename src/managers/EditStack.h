#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace photon::editstack {

// Edit sidecars (.PhotonData/edits/<file>.json) are an array of edit states
// (the history), optionally followed by a marker {"__index": i} (which may
// also carry a legacy "rating") that selects the current state. This mirrors
// RawEngine::loadEdits(): readers must use the selected state, never blindly
// the last array element (that is the marker whenever the history was used).

inline bool isMarker(const QJsonObject& entry) {
  return entry.contains(QStringLiteral("__index"));
}

// Index in `arr` of the current state, or -1 when there is no state.
inline int currentIndex(const QJsonArray& arr) {
  if (arr.isEmpty()) return -1;
  const QJsonObject last = arr.last().toObject();
  if (!isMarker(last)) return int(arr.size()) - 1;
  const int states = int(arr.size()) - 1;  // entries before the marker
  if (states <= 0) return -1;
  const int index = last.value(QStringLiteral("__index")).toInt(-1);
  return (index >= 0 && index < states) ? index : states - 1;
}

// The current edit state (empty when the sidecar has none).
inline QJsonObject currentState(const QJsonArray& arr) {
  const int i = currentIndex(arr);
  return i >= 0 ? arr.at(i).toObject() : QJsonObject();
}

// The current state without history metadata (__step, __delta, ...), for
// copying settings to other photos.
inline QJsonObject currentSettings(const QJsonArray& arr) {
  QJsonObject state = currentState(arr);
  for (const QString& key : state.keys()) {
    if (key.startsWith(QStringLiteral("__"))) state.remove(key);
  }
  return state;
}

// Replace the current state in place, keeping the history and the marker.
inline void replaceCurrentState(QJsonArray& arr, const QJsonObject& state) {
  const int i = currentIndex(arr);
  if (i < 0) {
    // No state yet: insert it before a lone marker, or as the only entry.
    if (!arr.isEmpty() && isMarker(arr.last().toObject())) {
      arr.insert(arr.size() - 1, state);
      QJsonObject marker = arr.last().toObject();
      marker[QStringLiteral("__index")] = 0;
      arr.replace(arr.size() - 1, marker);
    } else {
      arr.append(state);
    }
    return;
  }
  arr.replace(i, state);
}

}  // namespace photon::editstack
