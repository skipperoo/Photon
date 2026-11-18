#pragma once

#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

// Edit video: a short clip that starts from the unedited photo, applies the
// edit one Develop group at a time (each fading in under a pill with the
// group's name; groups the user chose not to show are applied together as
// "Tweaks" at the end), returns to the original and closes with a slow
// before/after slider: the line rests on the right, sweeps back to the left
// (the original) and after a second sweeps to the right again (the finished
// edit), both return sweeps twice as fast as the first.
//
// This file holds the parts that do not encode: the group table (which edit
// keys belong to which Develop panel section), the per-step settings, the
// output canvas and the frame compositor. Rendering the steps is done by the
// exporter / preview controller with ImageDeveloper; encoding by
// photon::video::Encoder.
namespace photon::editvideo {

// --- Edit groups --------------------------------------------------------------

struct EditGroup {
  QString name;             // shown in the pill, as titled in the Develop panel
  QStringList keys;         // exact edit keys
  QStringList keyPrefixes;  // keys starting with one of these
};

// The Develop panel's groups, top to bottom (the order of the video).
const std::vector<EditGroup>& editGroups();

// Group index of an edit key, or -1 for keys applied in every step:
// geometry (crop, rotation, flips), lens corrections and render keys.
int groupOfKey(const QString& key);

// Keys that are deliberately in no group (applied from the first frame).
// Every key an edit stores must be in a group or here (tested).
const QStringList& baselineKeys();

// A set of groups: bit i = editGroups()[i].
using GroupMask = uint32_t;
inline GroupMask groupBit(int group) { return GroupMask(1) << group; }
GroupMask allGroups();

// Settings with only the groups in `groups` applied: `full` without the keys
// of the other groups, so ImageDeveloper uses its defaults for them.
QJsonObject stepSettings(const QJsonObject& full, GroupMask groups);

// True when the edit has at least one key of `group`.
bool editHasGroup(const QJsonObject& full, int group);

// The steps of a video: the original (no group), one step per shown group in
// panel order (cumulative), then "Tweaks" with every group when `tweaks`
// (some edited groups are not shown individually).
struct StepPlan {
  QString name;  // empty for the original
  GroupMask groups;
};
std::vector<StepPlan> planSteps(const std::vector<int>& shownGroups, bool tweaks);
inline const char* tweaksName() { return "Tweaks"; }

// --- Output canvas ------------------------------------------------------------

enum class Aspect { Original, Portrait4x5, Landscape5x4, Landscape16x9, Portrait9x16 };

// Parses "original", "4:5", "5:4", "16:9", "9:16" (Original otherwise).
Aspect aspectFromString(const QString& s);

// Canvas for a photo of `photo` size: the short side is `shortSide` pixels
// (1080 for Full HD, 2160 for 4K), both sides even (4:2:0 video).
QSize canvasSize(Aspect aspect, QSize photo, int shortSide);

// --- Pill -----------------------------------------------------------------------

enum class PillStyle { FrostedGlass, LiquidGlass, White, Black };
enum class PillShape { RoundedRect, Capsule };
enum class PillPosition { Top, Center, Bottom };

struct PillOptions {
  PillStyle style = PillStyle::FrostedGlass;
  PillShape shape = PillShape::RoundedRect;
  PillPosition position = PillPosition::Center;
};

// "frosted", "liquid", "white", "black"; "rounded", "capsule";
// "top", "center", "bottom" (defaults otherwise).
PillStyle pillStyleFromString(const QString& s);
PillShape pillShapeFromString(const QString& s);
PillPosition pillPositionFromString(const QString& s);

// --- Timeline ------------------------------------------------------------------

struct Timing {
  int fps = 30;
  double intro = 1.2;         // the original photo, still
  double fade = 1.0;          // a group's effect fading in
  double pillIn = 0.35;       // pill fade-in, from the start of the effect fade
  double pillHold = 0.5;      // pill fully visible after the effect fade
  double pillOut = 0.35;      // pill fade-out
  double stepRest = 0.2;      // still, before the next group
  double outroHold = 0.8;     // the finished edit, still
  double toOriginal = 0.6;    // fade back to the original
  double originalHold = 0.6;  // the original, still
  double sweep = 3.0;         // slow slider line from left (before) to right (after);
                              // the two return sweeps take half this, same easing
  double sweepHold = 0.6;     // the line resting on the right (after)
  double leftHold = 1.0;      // the line resting on the left (before)
  double endHold = 3.0;       // back on the right: the finished edit
  double lineFade = 0.5;      // the line fading out at the start of endHold
};

// --- Compositor ----------------------------------------------------------------

struct Step {
  QString name;  // empty for the original photo
  QImage image;  // the developed step (any size, same aspect for all steps)
};

class Composer {
 public:
  // `steps[0]` is the original photo; the others follow the plan (the last
  // is the finished edit).
  // `showLine`: draw the slider's vertical line (the sweep happens either way).
  Composer(std::vector<Step> steps, QSize canvas, Timing timing = Timing(),
           PillOptions pill = PillOptions(), bool showLine = true);

  int frameCount() const { return m_frameCount; }
  int fps() const { return m_timing.fps; }
  double duration() const { return double(m_frameCount) / m_timing.fps; }
  QSize canvas() const { return m_canvas; }
  QRect photoRect() const { return m_photoRect; }
  // The pill's font family (Instrument Sans from the app resources, or the
  // system sans-serif when they are not available).
  QString fontFamily() const { return m_fontFamily; }
  // Pill options can change without rebuilding the layers (live preview).
  void setPillOptions(const PillOptions& pill) { m_pill = pill; }
  const PillOptions& pillOptions() const { return m_pill; }
  void setShowLine(bool show) { m_showLine = show; }
  bool showLine() const { return m_showLine; }
  // Frame `index` (0 .. frameCount-1) as Format_RGB32.
  QImage frame(int index) const;
  // The pill's rectangle for `text` at full opacity (tests, layout).
  QRectF pillRect(const QString& text) const;

 private:
  QImage blend(int a, int b, double t) const;
  // After on the left of `position` (0..1 across the photo), before on the
  // right; the line drawn at `lineOpacity` (kept on the photo at the edges).
  QImage split(double position, double lineOpacity) const;
  void drawPill(QImage& frame, const QString& text, double opacity) const;

  std::vector<Step> m_steps;
  std::vector<QImage> m_layers;  // blurred extension + fitted photo, per step
  QSize m_canvas;
  QRect m_photoRect;
  Timing m_timing;
  PillOptions m_pill;
  bool m_showLine = true;
  int m_frameCount = 0;
  QString m_fontFamily;
};

// Separable box blur (3 passes approximate a Gaussian) of an RGB32 image.
QImage boxBlur(const QImage& image, int radius);

}  // namespace photon::editvideo
