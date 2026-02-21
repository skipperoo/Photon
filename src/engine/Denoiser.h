#pragma once

#include <QImage>
#include <vector>
#include <cmath>
#include <algorithm>
#include <memory>
#include <atomic>

namespace photon {

struct Bm3dParams {
    float sigma;
    float hard_th_lambda;
    float max_dist_hard;

    static Bm3dParams fromIntensity(float i) {
        float val = std::clamp(i, 0.001f, 1.0f);
        return {
            val * 80.0f,
            2.0f + (val * 2.5f),
            3000.0f + (val * 20000.0f)
        };
    }
};

class Denoiser {
public:
    static QImage denoise(const QImage& input, float intensity, std::atomic<bool>* abort = nullptr, bool step2 = true, int stride = 6);

private:
    static constexpr int BLOCK_SIZE = 8;
    static constexpr int BLOCK_AREA = 64;
    static constexpr int MAX_GROUP_SIZE = 16;
    static constexpr int SEARCH_WINDOW = 19;

    struct DctTables {
        float dct_coeff[64];
        float idct_coeff[64];
        float kaiser[64];

        DctTables();
    };

    struct AtomicAccumulator {
        std::vector<std::atomic<int64_t>> data;
        static constexpr float FIXED_POINT_SCALE = 100000.0f;

        AtomicAccumulator(size_t size);
        void add(size_t index, float value);
        std::vector<float> toVector() const;
    };

    static std::vector<std::vector<float>> bm3d_process_joint(
        const std::vector<std::vector<float>>& noisy_channels,
        int width, int height, const Bm3dParams& params, const DctTables& tables,
        std::atomic<bool>* abort, bool step2, int stride);

    static std::vector<std::vector<float>> run_bm3d_step_joint(
        const std::vector<std::vector<float>>& noisy,
        const std::vector<std::vector<float>>& guide,
        int width, int height, const Bm3dParams& params, bool is_step_1, const DctTables& tables,
        std::atomic<bool>* abort, int stride);

    static int block_matching_joint(
        const std::vector<std::vector<float>>& channels,
        int w, int h, int rx, int ry, bool is_step_1,
        const Bm3dParams& params, std::pair<int, int>* out_buf);

    static float compute_ssd_flat(const float* img, int w, int x, int y, const float* ref_patch, float stop_thr);
    static void extract_patch(const float* img, int w, int x, int y, float* out);
    static void transform_3d(float* stack, int group_size, const DctTables& tables);
    static void inverse_transform_3d(float* stack, int group_size, const DctTables& tables);
    static void dct_2d_8x8(float* block, const float* coeffs);
    static void idct_2d_8x8(float* block, const float* coeffs);
    static void dct_1d_8(float* x, const float* coeffs);
    static void idct_1d_8(float* x, const float* coeffs);
    static void transpose_8x8(float* b);
    static void walsh_hadamard_1d(float* data, int n);
};

} // namespace photon
