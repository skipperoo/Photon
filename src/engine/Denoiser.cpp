#include <immintrin.h>
#include "Denoiser.h"
#include <QtConcurrent>
#include <QRgba64>
#include <cmath>

namespace photon {

Denoiser::DctTables::DctTables() {
    const float PI = 3.14159265358979323846f;
    for (int k = 0; k < 8; ++k) {
        for (int n = 0; n < 8; ++n) {
            float c = k * PI / 8.0f;
            float val = std::cos((n + 0.5f) * c);
            float scale = (k == 0) ? 0.35355339f : 0.5f;
            dct_coeff[k * 8 + n] = val * scale;
        }
    }
    for (int n = 0; n < 8; ++n) {
        for (int k = 0; k < 8; ++k) {
            float theta = (PI / 8.0f) * (n + 0.5f) * k;
            float scale = (k == 0) ? 0.35355339f : 0.5f;
            idct_coeff[n * 8 + k] = scale * std::cos(theta);
        }
    }
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            float wx = std::sin(PI * x / 7.0f);
            float wy = std::sin(PI * y / 7.0f);
            kaiser[y * 8 + x] = wx * wy;
        }
    }
}

Denoiser::AtomicAccumulator::AtomicAccumulator(size_t size) : data(size) {
    for (size_t i = 0; i < size; ++i) {
        data[i].store(0);
    }
}

void Denoiser::AtomicAccumulator::add(size_t index, float value) {
    if (index < data.size()) {
        int64_t fixed = static_cast<int64_t>(value * FIXED_POINT_SCALE);
        data[index].fetch_add(fixed, std::memory_order_relaxed);
    }
}

std::vector<float> Denoiser::AtomicAccumulator::toVector() const {
    std::vector<float> res(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        res[i] = static_cast<float>(data[i].load(std::memory_order_relaxed)) / FIXED_POINT_SCALE;
    }
    return res;
}

QImage Denoiser::denoise(const QImage& input, float intensity, std::atomic<bool>* abort) {
    if (input.isNull() || intensity <= 0.0f) return input;
    if (abort && abort->load()) return input;

    int width = input.width();
    int height = input.height();
    int size = width * height;

    std::vector<std::vector<float>> channels(3, std::vector<float>(size));
    if (input.format() == QImage::Format_RGBX64 || input.format() == QImage::Format_RGBA64) {
        const QRgba64* bits = reinterpret_cast<const QRgba64*>(input.constBits());
        for (int i = 0; i < size; ++i) {
            channels[0][i] = bits[i].red() / 257.0f; // Scale to 0-255
            channels[1][i] = bits[i].green() / 257.0f;
            channels[2][i] = bits[i].blue() / 257.0f;
        }
    } else {
        QImage converted = input.convertToFormat(QImage::Format_RGB888);
        const uchar* bits = converted.constBits();
        for (int i = 0; i < size; ++i) {
            channels[0][i] = bits[i * 3];
            channels[1][i] = bits[i * 3 + 1];
            channels[2][i] = bits[i * 3 + 2];
        }
    }

    Bm3dParams params = Bm3dParams::fromIntensity(intensity / 100.0f);
    DctTables tables;

    auto denoised_channels = bm3d_process_joint(channels, width, height, params, tables, abort);
    if (abort && abort->load()) return input;

    QImage output(width, height, QImage::Format_RGBX64);
    QRgba64* out_bits = reinterpret_cast<QRgba64*>(output.bits());
    for (int i = 0; i < size; ++i) {
        ushort r = static_cast<ushort>(std::clamp(denoised_channels[0][i], 0.0f, 255.0f) * 257.0f);
        ushort g = static_cast<ushort>(std::clamp(denoised_channels[1][i], 0.0f, 255.0f) * 257.0f);
        ushort b = static_cast<ushort>(std::clamp(denoised_channels[2][i], 0.0f, 255.0f) * 257.0f);
        out_bits[i] = QRgba64::fromRgba64(r, g, b, 65535);
    }

    return output;
}

std::vector<std::vector<float>> Denoiser::bm3d_process_joint(
    const std::vector<std::vector<float>>& noisy_channels,
    int width, int height, const Bm3dParams& params, const DctTables& tables,
    std::atomic<bool>* abort) {
    
    auto basic_estimate = run_bm3d_step_joint(noisy_channels, noisy_channels, width, height, params, true, tables, abort);
    if (abort && abort->load()) return noisy_channels;
    
    return run_bm3d_step_joint(noisy_channels, basic_estimate, width, height, params, false, tables, abort);
}

std::vector<std::vector<float>> Denoiser::run_bm3d_step_joint(
    const std::vector<std::vector<float>>& noisy,
    const std::vector<std::vector<float>>& guide,
    int width, int height, const Bm3dParams& params, bool is_step_1, const DctTables& tables,
    std::atomic<bool>* abort) {

    int count = width * height;
    std::vector<std::shared_ptr<AtomicAccumulator>> numerators(3);
    std::vector<std::shared_ptr<AtomicAccumulator>> denominators(3);
    for (int i = 0; i < 3; ++i) {
        numerators[i] = std::make_shared<AtomicAccumulator>(count);
        denominators[i] = std::make_shared<AtomicAccumulator>(count);
    }

    std::vector<std::pair<int, int>> ref_patches;
    for (int y = 0; y <= height - BLOCK_SIZE; y += STRIDE) {
        for (int x = 0; x <= width - BLOCK_SIZE; x += STRIDE) {
            ref_patches.push_back({x, y});
        }
    }

    QtConcurrent::blockingMap(ref_patches, [&](const std::pair<int, int>& patch) {
        if (abort && abort->load()) return;

        int rx = patch.first;
        int ry = patch.second;

        std::pair<int, int> group_locs_buf[MAX_GROUP_SIZE];
        int group_size = block_matching_joint(guide, width, height, rx, ry, is_step_1, params, group_locs_buf);

        for (int ch = 0; ch < 3; ++ch) {
            const auto& guide_ch = guide[ch];
            const auto& noisy_ch = noisy[ch];

            std::vector<float> guide_stack(group_size * BLOCK_AREA);
            for (int i = 0; i < group_size; ++i) {
                extract_patch(guide_ch.data(), width, group_locs_buf[i].first, group_locs_buf[i].second, &guide_stack[i * BLOCK_AREA]);
            }

            std::vector<float> noisy_stack;
            if (is_step_1) {
                noisy_stack = guide_stack;
            } else {
                noisy_stack.resize(group_size * BLOCK_AREA);
                for (int i = 0; i < group_size; ++i) {
                    extract_patch(noisy_ch.data(), width, group_locs_buf[i].first, group_locs_buf[i].second, &noisy_stack[i * BLOCK_AREA]);
                }
            }

            transform_3d(guide_stack.data(), group_size, tables);
            if (!is_step_1) {
                transform_3d(noisy_stack.data(), group_size, tables);
            }

            float weight = 1.0f;
            if (is_step_1) {
                float threshold = params.hard_th_lambda * params.sigma;
                __m256 v_thresh = _mm256_set1_ps(threshold);
                __m256 v_neg_thresh = _mm256_set1_ps(-threshold);
                int nonzero = 0;
                
                // DC component (index 0) always kept
                nonzero++;
                
                for (size_t i = 8; i < guide_stack.size(); i += 8) {
                    __m256 v_val = _mm256_loadu_ps(&guide_stack[i]);
                    __m256 v_gt = _mm256_cmp_ps(v_val, v_thresh, _CMP_GE_OQ);
                    __m256 v_lt = _mm256_cmp_ps(v_val, v_neg_thresh, _CMP_LE_OQ);
                    __m256 v_mask = _mm256_or_ps(v_gt, v_lt);
                    __m256 v_res = _mm256_and_ps(v_val, v_mask);
                    _mm256_storeu_ps(&guide_stack[i], v_res);
                    
                    // Count non-zeros
                    int mask = _mm256_movemask_ps(v_mask);
                    nonzero += __builtin_popcount(mask);
                }
                // Handle remaining elements from 1 to 7 (loop above starts at 8)
                for (size_t i = 1; i < 8; ++i) {
                    if (std::abs(guide_stack[i]) >= threshold) nonzero++;
                    else guide_stack[i] = 0.0f;
                }

                weight = (nonzero > 0) ? (1.0f / nonzero) : 1.0f;
                noisy_stack = guide_stack;
            } else {
                float sum_sq = 0.0f;
                float s2 = params.sigma * params.sigma;
                __m256 v_s2 = _mm256_set1_ps(s2);
                __m256 v_eps = _mm256_set1_ps(1e-5f);
                __m256 v_sum_sq = _mm256_setzero_ps();

                // DC component
                sum_sq += 1.0f;

                for (size_t i = 8; i < noisy_stack.size(); i += 8) {
                    __m256 v_guide = _mm256_loadu_ps(&guide_stack[i]);
                    __m256 v_noisy = _mm256_loadu_ps(&noisy_stack[i]);
                    
                    __m256 v_energy = _mm256_mul_ps(v_guide, v_guide);
                    __m256 v_denom = _mm256_add_ps(_mm256_add_ps(v_energy, v_s2), v_eps);
                    __m256 v_coef = _mm256_div_ps(v_energy, v_denom);
                    
                    v_noisy = _mm256_mul_ps(v_noisy, v_coef);
                    _mm256_storeu_ps(&noisy_stack[i], v_noisy);
                    
                    v_sum_sq = _mm256_add_ps(v_sum_sq, _mm256_mul_ps(v_coef, v_coef));
                }
                
                // Horizontal sum for Wiener weights
                __m128 vlow = _mm256_castps256_ps128(v_sum_sq);
                __m128 vhigh = _mm256_extractf128_ps(v_sum_sq, 1);
                vlow = _mm_add_ps(vlow, vhigh);
                __m128 shuf = _mm_movehdup_ps(vlow);
                __m128 sums = _mm_add_ps(vlow, shuf);
                shuf = _mm_movehl_ps(shuf, sums);
                sums = _mm_add_ss(sums, shuf);
                sum_sq += _mm_cvtss_f32(sums);

                // Handle remaining elements 1-7
                for (size_t i = 1; i < 8; ++i) {
                    float energy = guide_stack[i] * guide_stack[i];
                    float coef = energy / (energy + s2 + 1e-5f);
                    noisy_stack[i] *= coef;
                    sum_sq += coef * coef;
                }

                weight = (sum_sq > 0.0f) ? (1.0f / sum_sq) : 1.0f;
            }

            inverse_transform_3d(noisy_stack.data(), group_size, tables);

            for (int k = 0; k < group_size; ++k) {
                int lx = group_locs_buf[k].first;
                int ly = group_locs_buf[k].second;
                int patch_offset = k * BLOCK_AREA;
                for (int dy = 0; dy < BLOCK_SIZE; ++dy) {
                    int row_global = (ly + dy) * width + lx;
                    int row_patch = dy * BLOCK_SIZE;
                    for (int dx = 0; dx < BLOCK_SIZE; ++dx) {
                        int idx = row_global + dx;
                        float val = noisy_stack[patch_offset + row_patch + dx];
                        float w_val = tables.kaiser[row_patch + dx] * weight;
                        numerators[ch]->add(idx, val * w_val);
                        denominators[ch]->add(idx, w_val);
                    }
                }
            }
        }
    });

    std::vector<std::vector<float>> results(3);
    for (int ch = 0; ch < 3; ++ch) {
        results[ch] = numerators[ch]->toVector();
        auto dens = denominators[ch]->toVector();
        for (int i = 0; i < count; ++i) {
            if (dens[i] > 1e-6f) {
                results[ch][i] /= dens[i];
            }
        }
    }
    return results;
}

int Denoiser::block_matching_joint(
    const std::vector<std::vector<float>>& channels,
    int w, int h, int rx, int ry, bool is_step_1,
    const Bm3dParams& params, std::pair<int, int>* out_buf) {

    struct Match {
        float dist;
        int x, y;
    };

    std::vector<Match> candidates;
    float threshold = is_step_1 ? params.max_dist_hard : params.max_dist_hard * 0.5f;

    float ref_patches[3][64];
    for (int ch = 0; ch < 3; ++ch) {
        extract_patch(channels[ch].data(), w, rx, ry, ref_patches[ch]);
    }

    int half_sw = SEARCH_WINDOW / 2;
    int sx_start = std::max(0, rx - half_sw);
    int sx_end = std::min(w - BLOCK_SIZE, rx + half_sw);
    int sy_start = std::max(0, ry - half_sw);
    int sy_end = std::min(h - BLOCK_SIZE, ry + half_sw);

    for (int y = sy_start; y <= sy_end; ++y) {
        for (int x = sx_start; x <= sx_end; ++x) {
            float total_dist = 0.0f;
            for (int ch = 0; ch < 3; ++ch) {
                total_dist += compute_ssd_flat(channels[ch].data(), w, x, y, ref_patches[ch], threshold - total_dist);
                if (total_dist > threshold) break;
            }

            if (total_dist < threshold) {
                candidates.push_back({total_dist, x, y});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Match& a, const Match& b) {
        return a.dist < b.dist;
    });

    int limit = std::min((int)candidates.size(), MAX_GROUP_SIZE);
    int p2_limit = 1;
    while (p2_limit * 2 <= limit) p2_limit *= 2;

    for (int i = 0; i < p2_limit; ++i) {
        out_buf[i] = {candidates[i].x, candidates[i].y};
    }
    return p2_limit;
}

float Denoiser::compute_ssd_flat(const float* img, int w, int x, int y, const float* ref_patch, float stop_thr) {
    __m256 sum = _mm256_setzero_ps();
    for (int dy = 0; dy < 8; ++dy) {
        int img_base = (y + dy) * w + x;
        int ref_base = dy * 8;
        
        __m256 v_img = _mm256_loadu_ps(img + img_base);
        __m256 v_ref = _mm256_loadu_ps(ref_patch + ref_base);
        __m256 diff = _mm256_sub_ps(v_img, v_ref);
        sum = _mm256_fmadd_ps(diff, diff, sum);
    }
    
    // Horizontal sum of the 8 floats in the AVX register
    __m128 vlow = _mm256_castps256_ps128(sum);
    __m128 vhigh = _mm256_extractf128_ps(sum, 1);
    vlow = _mm_add_ps(vlow, vhigh);
    __m128 shuf = _mm_movehdup_ps(vlow);
    __m128 sums = _mm_add_ps(vlow, shuf);
    shuf = _mm_movehl_ps(shuf, sums);
    sums = _mm_add_ss(sums, shuf);
    float dist = _mm_cvtss_f32(sums);

    return dist / 64.0f;
}

void Denoiser::extract_patch(const float* img, int w, int x, int y, float* out) {
    for (int dy = 0; dy < 8; ++dy) {
        __m256 v_row = _mm256_loadu_ps(img + (y + dy) * w + x);
        _mm256_storeu_ps(out + dy * 8, v_row);
    }
}

void Denoiser::dct_1d_8(float* x, const float* coeffs) {
    float tmp[8];
    std::copy(x, x + 8, tmp);
    __m256 v_tmp = _mm256_loadu_ps(tmp);

    for (int k = 0; k < 8; ++k) {
        __m256 v_coeffs = _mm256_loadu_ps(coeffs + k * 8);
        __m256 v_prod = _mm256_mul_ps(v_tmp, v_coeffs);
        
        // Horizontal sum
        __m128 vlow = _mm256_castps256_ps128(v_prod);
        __m128 vhigh = _mm256_extractf128_ps(v_prod, 1);
        vlow = _mm_add_ps(vlow, vhigh);
        __m128 shuf = _mm_movehdup_ps(vlow);
        __m128 sums = _mm_add_ps(vlow, shuf);
        shuf = _mm_movehl_ps(shuf, sums);
        sums = _mm_add_ss(sums, shuf);
        x[k] = _mm_cvtss_f32(sums);
    }
}

void Denoiser::idct_1d_8(float* x, const float* coeffs) {
    float tmp[8];
    std::copy(x, x + 8, tmp);

    for (int n = 0; n < 8; ++n) {
        __m256 v_tmp = _mm256_loadu_ps(tmp);
        // We need the n-th column of the coeffs matrix (which is 8x8)
        // Since idct_coeff[n*8 + k] is what we use, it's actually row-major access here.
        __m256 v_coeffs = _mm256_loadu_ps(coeffs + n * 8);
        __m256 v_prod = _mm256_mul_ps(v_tmp, v_coeffs);

        // Horizontal sum
        __m128 vlow = _mm256_castps256_ps128(v_prod);
        __m128 vhigh = _mm256_extractf128_ps(v_prod, 1);
        vlow = _mm_add_ps(vlow, vhigh);
        __m128 shuf = _mm_movehdup_ps(vlow);
        __m128 sums = _mm_add_ps(vlow, shuf);
        shuf = _mm_movehl_ps(shuf, sums);
        sums = _mm_add_ss(sums, shuf);
        x[n] = _mm_cvtss_f32(sums);
    }
}

void Denoiser::transpose_8x8(float* b) {
    for (int y = 0; y < 8; ++y) {
        for (int x = y + 1; x < 8; ++x) {
            std::swap(b[y * 8 + x], b[x * 8 + y]);
        }
    }
}

void Denoiser::dct_2d_8x8(float* block, const float* coeffs) {
    for (int i = 0; i < 8; ++i) dct_1d_8(block + i * 8, coeffs);
    transpose_8x8(block);
    for (int i = 0; i < 8; ++i) dct_1d_8(block + i * 8, coeffs);
    transpose_8x8(block);
}

void Denoiser::idct_2d_8x8(float* block, const float* coeffs) {
    transpose_8x8(block);
    for (int i = 0; i < 8; ++i) idct_1d_8(block + i * 8, coeffs);
    transpose_8x8(block);
    for (int i = 0; i < 8; ++i) idct_1d_8(block + i * 8, coeffs);
}

void Denoiser::walsh_hadamard_1d(float* data, int n) {
    int h = 1;
    while (h < n) {
        for (int i = 0; i < n; i += h * 2) {
            for (int j = i; j < i + h; ++j) {
                float x = data[j];
                float y = data[j + h];
                data[j] = x + y;
                data[j + h] = x - y;
            }
        }
        h *= 2;
    }
    float scale = 1.0f / std::sqrt((float)n);
    for (int i = 0; i < n; ++i) data[i] *= scale;
}

void Denoiser::transform_3d(float* stack, int group_size, const DctTables& tables) {
    for (int i = 0; i < group_size; ++i) {
        dct_2d_8x8(stack + i * 64, tables.dct_coeff);
    }
    for (int i = 0; i < 64; ++i) {
        float col[MAX_GROUP_SIZE];
        for (int k = 0; k < group_size; ++k) col[k] = stack[k * 64 + i];
        walsh_hadamard_1d(col, group_size);
        for (int k = 0; k < group_size; ++k) stack[k * 64 + i] = col[k];
    }
}

void Denoiser::inverse_transform_3d(float* stack, int group_size, const DctTables& tables) {
    for (int i = 0; i < 64; ++i) {
        float col[MAX_GROUP_SIZE];
        for (int k = 0; k < group_size; ++k) col[k] = stack[k * 64 + i];
        walsh_hadamard_1d(col, group_size);
        for (int k = 0; k < group_size; ++k) stack[k * 64 + i] = col[k];
    }
    for (int i = 0; i < group_size; ++i) {
        idct_2d_8x8(stack + i * 64, tables.idct_coeff);
    }
}

} // namespace photon
