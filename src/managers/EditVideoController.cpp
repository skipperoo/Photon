#include "EditVideoController.h"

#include <QtConcurrent>

#include "EditVideoSource.h"
#include "ExportManager.h"
#include "LogManager.h"

namespace photon {

namespace ev = photon::editvideo;

namespace {
void log(const QString& msg, LogLevel level = PHOTON_INFO) {
  LogManager::instance()->log("[ EditVideoController.cpp ] - " + msg, level);
}
}  // namespace

EditVideoController::EditVideoController(ExportManager* exporter, QObject* parent)
    : QObject(parent), m_exporter(exporter) {}

EditVideoController::~EditVideoController() = default;

QVariantList EditVideoController::groups() const {
  QVariantList list;
  for (const int g : m_effective) {
    QVariantMap item;
    item["group"] = g;
    item["name"] = ev::editGroups()[size_t(g)].name;
    item["shown"] = m_hidden.count(g) == 0;
    list << item;
  }
  return list;
}

int EditVideoController::frameCount() const {
  return m_composer ? m_composer->frameCount() : 0;
}

int EditVideoController::fps() const {
  return m_composer ? m_composer->fps() : ev::Timing().fps;
}

QImage EditVideoController::frame(int index) const {
  return m_composer ? m_composer->frame(index) : QImage();
}

ev::PillOptions EditVideoController::pillOptions() const {
  ev::PillOptions pill;
  pill.style = ev::pillStyleFromString(m_pillStyle);
  pill.shape = ev::pillShapeFromString(m_pillShape);
  pill.position = ev::pillPositionFromString(m_pillPosition);
  return pill;
}

std::vector<ev::StepPlan> EditVideoController::plan() const {
  std::vector<int> shown;
  for (const int g : m_effective)
    if (m_hidden.count(g) == 0) shown.push_back(g);
  return ev::planSteps(shown, !m_hidden.empty());
}

void EditVideoController::setStatus(bool loading, const QString& status) {
  m_loading = loading;
  m_status = status;
  emit stateChanged();
}

void EditVideoController::setAspect(const QString& aspect) {
  if (m_aspect == aspect) return;
  m_aspect = aspect;
  emit optionsChanged();
  rebuildComposer();
}

void EditVideoController::setResolution(int resolution) {
  if (m_resolution == resolution) return;
  m_resolution = resolution;
  emit optionsChanged();
}

void EditVideoController::setPillStyle(const QString& style) {
  if (m_pillStyle == style) return;
  m_pillStyle = style;
  emit optionsChanged();
  if (m_composer) {
    m_composer->setPillOptions(pillOptions());
    ++m_previewVersion;
    emit previewChanged();
  }
}

void EditVideoController::setPillShape(const QString& shape) {
  if (m_pillShape == shape) return;
  m_pillShape = shape;
  emit optionsChanged();
  if (m_composer) {
    m_composer->setPillOptions(pillOptions());
    ++m_previewVersion;
    emit previewChanged();
  }
}

void EditVideoController::setPillPosition(const QString& position) {
  if (m_pillPosition == position) return;
  m_pillPosition = position;
  emit optionsChanged();
  if (m_composer) {
    m_composer->setPillOptions(pillOptions());
    ++m_previewVersion;
    emit previewChanged();
  }
}

void EditVideoController::setShowLine(bool show) {
  if (m_showLine == show) return;
  m_showLine = show;
  emit optionsChanged();
  if (m_composer) {
    m_composer->setShowLine(show);
    ++m_previewVersion;
    emit previewChanged();
  }
}

void EditVideoController::open(const QString& path) {
  const int generation = ++m_generation;
  m_path = path;
  m_source.reset();
  m_effective.clear();
  m_hidden.clear();
  m_cache.clear();
  m_composer.reset();
  emit sourceChanged();
  emit groupsChanged();
  ++m_previewVersion;
  emit previewChanged();
  setStatus(true, QStringLiteral("Preparing the preview…"));

  QRhi* rhi = m_exporter ? m_exporter->rhi() : nullptr;
  QQuickWindow* window = m_exporter ? m_exporter->window() : nullptr;
  struct Result {
    std::shared_ptr<EditVideoSource> source;
    std::vector<int> effective;
    std::map<ev::GroupMask, QImage> renders;
    bool ok = false;
  };
  auto* watcher = new QFutureWatcher<Result>(this);
  connect(watcher, &QFutureWatcher<Result>::finished, this,
          [this, watcher, generation]() {
            const Result r = watcher->result();
            watcher->deleteLater();
            if (generation != m_generation) return;  // reopened or closed
            if (!r.ok) {
              setStatus(false, QStringLiteral("This photo could not be read"));
              return;
            }
            m_source = r.source;
            m_effective = r.effective;
            m_cache = r.renders;
            emit groupsChanged();
            update();
          });
  watcher->setFuture(QtConcurrent::run([path, rhi, window]() {
    Result r;
    r.source = std::make_shared<EditVideoSource>();
    if (!r.source->load(path, kPreviewLongEdge, rhi, window)) return r;
    // Which edited groups change the photo, applied cumulatively in panel
    // order: a group that renders identically (e.g. B&W mix on a colour
    // photo) is left out, so the masks equal the default plan's.
    ev::GroupMask mask = 0;
    QImage previous = r.source->render(0);
    r.renders[0] = previous;
    for (const int g : r.source->editedGroups()) {
      const ev::GroupMask next = mask | ev::groupBit(g);
      QImage image = r.source->render(next);
      if (image.isNull() || image == previous) continue;
      mask = next;
      r.effective.push_back(g);
      r.renders[mask] = image;
      previous = std::move(image);
    }
    r.ok = !previous.isNull();
    return r;
  }));
}

void EditVideoController::close() {
  ++m_generation;
  m_source.reset();
  m_cache.clear();
  m_composer.reset();
  m_effective.clear();
  m_hidden.clear();
  m_busy = false;
  m_pending = false;
  setStatus(false, QString());
  emit groupsChanged();
  ++m_previewVersion;
  emit previewChanged();
}

void EditVideoController::setGroupShown(int group, bool shown) {
  const bool hidden = m_hidden.count(group) > 0;
  if (shown == !hidden) return;
  if (shown)
    m_hidden.erase(group);
  else
    m_hidden.insert(group);
  emit groupsChanged();
  update();
}

void EditVideoController::update() {
  if (!m_source) return;
  std::vector<ev::GroupMask> missing;
  for (const ev::StepPlan& step : plan())
    if (m_cache.count(step.groups) == 0) missing.push_back(step.groups);
  if (missing.empty()) {
    rebuildComposer();
    setStatus(false, QString());
    return;
  }
  if (m_busy) {  // one render job at a time; the latest choice wins
    m_pending = true;
    return;
  }
  m_busy = true;
  setStatus(true, QStringLiteral("Updating the preview…"));
  const int generation = m_generation;
  const std::shared_ptr<EditVideoSource> source = m_source;
  using Renders = std::map<ev::GroupMask, QImage>;
  auto* watcher = new QFutureWatcher<Renders>(this);
  connect(watcher, &QFutureWatcher<Renders>::finished, this,
          [this, watcher, generation]() {
            const Renders renders = watcher->result();
            watcher->deleteLater();
            if (generation != m_generation) return;
            m_busy = false;
            for (const auto& [mask, image] : renders) m_cache[mask] = image;
            if (m_pending) m_pending = false;
            update();  // renders anything still missing, then rebuilds
          });
  watcher->setFuture(QtConcurrent::run([source, missing]() {
    Renders renders;
    for (const ev::GroupMask mask : missing) renders[mask] = source->render(mask);
    return renders;
  }));
}

void EditVideoController::rebuildComposer() {
  if (!m_source) return;
  std::vector<ev::Step> steps;
  for (const ev::StepPlan& step : plan()) {
    const auto it = m_cache.find(step.groups);
    if (it == m_cache.end()) return;  // still rendering
    steps.push_back({step.name, it->second});
  }
  const QSize canvas = ev::canvasSize(ev::aspectFromString(m_aspect),
                                      steps.front().image.size(),
                                      kPreviewShortSide);
  m_composer = std::make_unique<ev::Composer>(std::move(steps), canvas,
                                              ev::Timing(), pillOptions(),
                                              m_showLine);
  ++m_previewVersion;
  emit previewChanged();
  emit stateChanged();
}

void EditVideoController::startExport(const QString& folder) {
  if (!m_exporter || m_path.isEmpty() || !m_source) return;
  VideoExportOptions options;
  options.aspect = m_aspect;
  options.shortSide = m_resolution;
  options.pill = pillOptions();
  options.showLine = m_showLine;
  for (const int g : m_effective)
    if (m_hidden.count(g) == 0) options.shownGroups.push_back(g);
  options.tweaks = !m_hidden.empty();
  log(QString("export %1: %2 group(s) shown, tweaks %3, %4 %5p, label %6/%7/%8")
          .arg(m_path)
          .arg(int(options.shownGroups.size()))
          .arg(options.tweaks)
          .arg(m_aspect)
          .arg(m_resolution)
          .arg(m_pillStyle, m_pillShape, m_pillPosition));
  m_exporter->startVideoExport(m_path, folder, options);
}

}  // namespace photon
