#include "GpuSearcher.h"

namespace photon {

GpuSearcher::GpuSearcher(QObject* parent)
    : QObject(parent) {
}

GpuSearcher::~GpuSearcher() = default;

std::vector<GpuSearcher::SearchResult> GpuSearcher::runSearch(const float* luma, int width, int height, int searchWindow) {
    Q_UNUSED(luma)
    Q_UNUSED(width)
    Q_UNUSED(height)
    Q_UNUSED(searchWindow)
    return {};
}

} // namespace photon
