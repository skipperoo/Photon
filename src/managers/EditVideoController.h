#pragma once

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "../engine/EditVideo.h"

namespace photon {

class EditVideoSource;
class ExportManager;

// The edit video dialog's model: prepares a fast preview of one photo's edit
// video (reduced resolution), lets the user choose the aspect ratio,
// resolution, pill style / shape / position and which edited groups appear
// one by one (the others are shown together as "Tweaks"), and exports the
// final video with exactly those choices.
class EditVideoController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString path READ path NOTIFY sourceChanged)
  Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
  Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
  Q_PROPERTY(QString status READ status NOTIFY stateChanged)
  // [{ group: int, name: string, shown: bool }] for the groups that change
  // the photo, in panel order.
  Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
  Q_PROPERTY(QString aspect READ aspect WRITE setAspect NOTIFY optionsChanged)
  Q_PROPERTY(int resolution READ resolution WRITE setResolution NOTIFY optionsChanged)
  Q_PROPERTY(QString pillStyle READ pillStyle WRITE setPillStyle NOTIFY optionsChanged)
  Q_PROPERTY(QString pillShape READ pillShape WRITE setPillShape NOTIFY optionsChanged)
  Q_PROPERTY(QString pillPosition READ pillPosition WRITE setPillPosition NOTIFY optionsChanged)
  // The before/after slider's vertical line (the sweep happens either way).
  Q_PROPERTY(bool showLine READ showLine WRITE setShowLine NOTIFY optionsChanged)
  Q_PROPERTY(int frameCount READ frameCount NOTIFY previewChanged)
  Q_PROPERTY(int fps READ fps NOTIFY previewChanged)
  // Bumped whenever the preview's frames change (style, aspect, edits).
  Q_PROPERTY(int previewVersion READ previewVersion NOTIFY previewChanged)

 public:
  explicit EditVideoController(ExportManager* exporter, QObject* parent = nullptr);
  ~EditVideoController() override;

  QString path() const { return m_path; }
  bool loading() const { return m_loading; }
  bool ready() const { return m_composer != nullptr && !m_loading; }
  QString status() const { return m_status; }
  QVariantList groups() const;
  QString aspect() const { return m_aspect; }
  int resolution() const { return m_resolution; }
  QString pillStyle() const { return m_pillStyle; }
  QString pillShape() const { return m_pillShape; }
  QString pillPosition() const { return m_pillPosition; }
  bool showLine() const { return m_showLine; }
  int frameCount() const;
  int fps() const;
  int previewVersion() const { return m_previewVersion; }

  void setAspect(const QString& aspect);
  void setResolution(int resolution);
  void setPillStyle(const QString& style);
  void setPillShape(const QString& shape);
  void setPillPosition(const QString& position);
  void setShowLine(bool show);

  // Prepares the preview of `path` (in the background).
  Q_INVOKABLE void open(const QString& path);
  // Releases the preview (dialog closed).
  Q_INVOKABLE void close();
  Q_INVOKABLE void setGroupShown(int group, bool shown);
  // Exports the video with the current choices to <folder>/<name>.mp4.
  Q_INVOKABLE void startExport(const QString& folder);

  // A preview frame (canvas with a short side of kPreviewShortSide).
  QImage frame(int index) const;

  static constexpr int kPreviewLongEdge = 1600;  // develop size
  static constexpr int kPreviewShortSide = 540;  // canvas size

 signals:
  void sourceChanged();
  void stateChanged();
  void groupsChanged();
  void optionsChanged();
  void previewChanged();

 private:
  std::vector<editvideo::StepPlan> plan() const;
  editvideo::PillOptions pillOptions() const;
  void setStatus(bool loading, const QString& status);
  // Renders the plan's missing steps (background), then rebuilds.
  void update();
  void rebuildComposer();

  ExportManager* m_exporter = nullptr;
  QString m_path;
  bool m_loading = false;
  QString m_status;
  QString m_aspect = QStringLiteral("original");
  int m_resolution = 1080;
  QString m_pillStyle = QStringLiteral("frosted");
  QString m_pillShape = QStringLiteral("rounded");
  QString m_pillPosition = QStringLiteral("center");
  bool m_showLine = true;
  int m_previewVersion = 0;

  std::shared_ptr<EditVideoSource> m_source;
  std::vector<int> m_effective;  // groups that change the photo
  std::set<int> m_hidden;        // effective groups shown as "Tweaks"
  std::map<editvideo::GroupMask, QImage> m_cache;  // preview renders
  std::unique_ptr<editvideo::Composer> m_composer;

  int m_generation = 0;  // stale background results are dropped
  bool m_busy = false;
  bool m_pending = false;
};

}  // namespace photon
