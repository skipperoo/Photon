#pragma once

#include <QObject>
#include <QImage>
#include <QSize>
#include <vector>

namespace photon {

/**
 * @brief Handles offloading the heavy patch-matching (SSD) phase to the GPU.
 * This is a placeholder for future full RHI implementation.
 */
class GpuSearcher : public QObject {
    Q_OBJECT
public:
    explicit GpuSearcher(QObject* parent = nullptr);
    ~GpuSearcher();

    struct SearchResult {
        int x;
        int y;
        float ssd;
    };

    /**
     * @brief Currently performs optimized CPU search as a fallback.
     */
    std::vector<SearchResult> runSearch(const float* luma, int width, int height, int searchWindow);
};

} // namespace photon
