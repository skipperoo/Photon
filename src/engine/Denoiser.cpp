#include "Denoiser.h"

#include <immintrin.h>

#include <QRgba64>
#include <QtConcurrent>
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
    res[i] = static_cast<float>(data[i].load(std::memory_order_relaxed)) /
             FIXED_POINT_SCALE;
  }
  return res;
}

QImage Denoiser::denoise(
    const QImage& input, float intensity, std::atomic<bool>* abort, bool step2,
    int stride, const std::vector<GpuSearcher::SearchResult>& gpuMatches) {
  if (input.isNull() || intensity <= 0.0f) return input;
  if (abort && abort->load()) return input;

  int width = input.width();
  int height = input.height();
  int size = width * height;

  std::vector<std::vector<float>> channels(3, std::vector<float>(size));
  if (input.format() == QImage::Format_RGBX64 ||
      input.format() == QImage::Format_RGBA64) {
    const QRgba64* bits = reinterpret_cast<const QRgba64*>(input.constBits());
    for (int i = 0; i < size; ++i) {
      channels[0][i] = bits[i].red() / 257.0f;  // Scale to 0-255
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

  auto denoised_channels =
      bm3d_process_joint(channels, width, height, params, tables, abort, step2,
                         stride, gpuMatches);
  if (abort && abort->load()) return input;

  QImage output(width, height, QImage::Format_RGBX64);
  QRgba64* out_bits = reinterpret_cast<QRgba64*>(output.bits());
  for (int i = 0; i < size; ++i) {
    ushort r = static_cast<ushort>(
        std::clamp(denoised_channels[0][i], 0.0f, 255.0f) * 257.0f);
    ushort g = static_cast<ushort>(
        std::clamp(denoised_channels[1][i], 0.0f, 255.0f) * 257.0f);
    ushort b = static_cast<ushort>(
        std::clamp(denoised_channels[2][i], 0.0f, 255.0f) * 257.0f);
    out_bits[i] = QRgba64::fromRgba64(r, g, b, 65535);
  }

  return output;
}

std::vector<std::vector<float>> Denoiser::bm3d_process_joint(
    const std::vector<std::vector<float>>& noisy_channels, int width,
    int height, const Bm3dParams& params, const DctTables& tables,
    std::atomic<bool>* abort, bool step2, int stride,
    const std::vector<GpuSearcher::SearchResult>& gpuMatches) {
  // Step 1: Basic Estimate
  auto basic_estimate =
      run_bm3d_step_joint(noisy_channels, noisy_channels, width, height, params,
                          true, tables, abort, stride, gpuMatches);
  if (abort && abort->load()) return noisy_channels;

  if (!step2) return basic_estimate;

  // Step 2: Final Estimate (Wiener)
  return run_bm3d_step_joint(noisy_channels, basic_estimate, width, height,
                             params, false, tables, abort, stride, gpuMatches);
}

std::vector<std::vector<float>> Denoiser::run_bm3d_step_joint(
    const std::vector<std::vector<float>>& noisy,
    const std::vector<std::vector<float>>& guide, int width, int height,
    const Bm3dParams& params, bool is_step_1, const DctTables& tables,
    std::atomic<bool>* abort, int stride,
    const std::vector<GpuSearcher::SearchResult>& gpuMatches) {
  int count = width * height;
  std::vector<std::shared_ptr<AtomicAccumulator>> numerators(3);
  std::vector<std::shared_ptr<AtomicAccumulator>> denominators(3);
  for (int i = 0; i < 3; ++i) {
    numerators[i] = std::make_shared<AtomicAccumulator>(count);
    denominators[i] = std::make_shared<AtomicAccumulator>(count);
  }

  // Pre-extract Luma for search
  std::vector<float> luma_buffer(count);
  for (int i = 0; i < count; ++i) {
    luma_buffer[i] =
        0.2126f * guide[0][i] + 0.7152f * guide[1][i] + 0.0722f * guide[2][i];
  }
  std::vector<std::vector<float>> search_channels = {luma_buffer};

  std::vector<std::pair<int, int>> ref_patches;
  for (int y = 0; y <= height - BLOCK_SIZE; y += stride) {
    for (int x = 0; x <= width - BLOCK_SIZE; x += stride) {
      ref_patches.push_back({x, y});
    }
  }

  QtConcurrent::blockingMap(ref_patches, [&](const std::pair<int, int>& patch) {
    if (abort && abort->load()) return;

    int rx = patch.first;
    int ry = patch.second;

    const GpuSearcher::SearchResult* gpuMatch = nullptr;
    if (!gpuMatches.empty()) {
      gpuMatch = &gpuMatches[ry * width + rx];
    }

    std::pair<int, int> group_locs_buf[MAX_GROUP_SIZE];
    int group_size =
        block_matching_joint(search_channels, width, height, rx, ry, is_step_1,
                             params, group_locs_buf, gpuMatch);

    for (int ch = 0; ch < 3; ++ch) {
      const auto& guide_ch = guide[ch];
      const auto& noisy_ch = noisy[ch];

      std::vector<float> guide_stack(group_size * BLOCK_AREA);
      for (int i = 0; i < group_size; ++i) {
        extract_patch(guide_ch.data(), width, group_locs_buf[i].first,
                      group_locs_buf[i].second, &guide_stack[i * BLOCK_AREA]);
      }

      std::vector<float> noisy_stack;
      if (is_step_1) {
        noisy_stack = guide_stack;
      } else {
        noisy_stack.resize(group_size * BLOCK_AREA);
        for (int i = 0; i < group_size; ++i) {
          extract_patch(noisy_ch.data(), width, group_locs_buf[i].first,
                        group_locs_buf[i].second, &noisy_stack[i * BLOCK_AREA]);
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
        nonzero++;  // DC component

        for (size_t i = 8; i < guide_stack.size(); i += 8) {
          __m256 v_val = _mm256_loadu_ps(&guide_stack[i]);
          __m256 v_gt = _mm256_cmp_ps(v_val, v_thresh, _CMP_GE_OQ);
          __m256 v_lt = _mm256_cmp_ps(v_val, v_neg_thresh, _CMP_LE_OQ);
          __m256 v_mask = _mm256_or_ps(v_gt, v_lt);
          __m256 v_res = _mm256_and_ps(v_val, v_mask);
          _mm256_storeu_ps(&guide_stack[i], v_res);
          int mask = _mm256_movemask_ps(v_mask);
          nonzero += __builtin_popcount(mask);
        }
        for (size_t i = 1; i < 8; ++i) {
          if (std::abs(guide_stack[i]) >= threshold)
            nonzero++;
          else
            guide_stack[i] = 0.0f;
        }
        weight = (nonzero > 0) ? (1.0f / nonzero) : 1.0f;
        noisy_stack = guide_stack;
      } else {
        float sum_sq = 0.0f;
        float s2 = params.sigma * params.sigma;
        __m256 v_s2 = _mm256_set1_ps(s2);
        __m256 v_eps = _mm256_set1_ps(1e-5f);
        __m256 v_sum_sq = _mm256_setzero_ps();
        sum_sq += 1.0f;  // DC component

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
        __m128 vlow = _mm256_castps256_ps128(v_sum_sq);
        __m128 vhigh = _mm256_extractf128_ps(v_sum_sq, 1);
        vlow = _mm_add_ps(vlow, vhigh);
        __m128 shuf = _mm_movehdup_ps(vlow);
        __m128 sums = _mm_add_ps(vlow, shuf);
        shuf = _mm_movehl_ps(shuf, sums);
        sums = _mm_add_ss(sums, shuf);
        sum_sq += _mm_cvtss_f32(sums);

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
      } else {
        results[ch][i] = noisy[ch][i];
      }
    }
  }
  return results;
}

int Denoiser::block_matching_joint(
    const std::vector<std::vector<float>>& channels, int w, int h, int rx,
    int ry, bool is_step_1, const Bm3dParams& params,
    std::pair<int, int>* out_buf, const GpuSearcher::SearchResult* gpuMatch) {
  struct Match {
    float dist;
    int x, y;
  };

  const float* luma = channels[0].data();
  float threshold =
      is_step_1 ? params.max_dist_hard : params.max_dist_hard * 0.5f;

  float ref_patch[64];
  extract_patch(luma, w, rx, ry, ref_patch);

  std::vector<Match> candidates;
  candidates.reserve(SEARCH_WINDOW * SEARCH_WINDOW);

  // Seed with GPU match if available
  if (gpuMatch) {
    int mx = rx + gpuMatch->x;
    int my = ry + gpuMatch->y;
    if (mx >= 0 && mx <= w - BLOCK_SIZE && my >= 0 && my <= h - BLOCK_SIZE) {
      // Re-verify the distance for an 8x8 block instead of 1x1
      float dist = compute_ssd_flat(luma, w, mx, my, ref_patch, threshold);
      if (dist < threshold) {
        candidates.push_back({dist, mx, my});
      }
    }
  }

  int half_sw = SEARCH_WINDOW / 2;
  int sx_start = std::max(0, rx - half_sw);
  int sx_end = std::min(w - BLOCK_SIZE, rx + half_sw);
  int sy_start = std::max(0, ry - half_sw);
  int sy_end = std::min(h - BLOCK_SIZE, ry + half_sw);

  for (int y = sy_start; y <= sy_end; ++y) {
    for (int x = sx_start; x <= sx_end; ++x) {
      if (gpuMatch && x == rx + gpuMatch->x && y == ry + gpuMatch->y) continue;

      float dist = compute_ssd_flat(luma, w, x, y, ref_patch, threshold);
      if (dist < threshold) {
        candidates.push_back({dist, x, y});
      }
    }
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const Match& a, const Match& b) { return a.dist < b.dist; });

  int limit = std::min((int)candidates.size(), MAX_GROUP_SIZE);
  int p2_limit = 1;
  while (p2_limit * 2 <= limit) p2_limit *= 2;

  for (int i = 0; i < p2_limit; ++i) {
    out_buf[i] = {candidates[i].x, candidates[i].y};
  }
  return p2_limit;
}

float Denoiser::compute_ssd_flat(const float* img, int w, int x, int y,
                                 const float* ref_patch, float stop_thr) {
  __m256 sum = _mm256_setzero_ps();
  float stop_val = stop_thr * 64.0f;

  for (int dy = 0; dy < 8; ++dy) {
    int img_base = (y + dy) * w + x;
    int ref_base = dy * 8;
    __m256 v_img = _mm256_loadu_ps(img + img_base);
    __m256 v_ref = _mm256_loadu_ps(ref_patch + ref_base);
    __m256 diff = _mm256_sub_ps(v_img, v_ref);
    sum = _mm256_fmadd_ps(diff, diff, sum);

    if (dy % 2 == 1) {
      __m128 vlow = _mm256_castps256_ps128(sum);
      __m128 vhigh = _mm256_extractf128_ps(sum, 1);
      __m128 vsum = _mm_add_ps(vlow, vhigh);
      __m128 shuf = _mm_movehdup_ps(vsum);
      vsum = _mm_add_ps(vsum, shuf);
      shuf = _mm_movehl_ps(shuf, vsum);
      vsum = _mm_add_ss(vsum, shuf);
      if (_mm_cvtss_f32(vsum) > stop_val) return 1000000.0f;
    }
  }
  __m128 vlow = _mm256_castps256_ps128(sum);
  __m128 vhigh = _mm256_extractf128_ps(sum, 1);
  vlow = _mm_add_ps(vlow, vhigh);
  __m128 shuf = _mm_movehdup_ps(vlow);
  __m128 sums = _mm_add_ps(vlow, shuf);
  shuf = _mm_movehl_ps(shuf, sums);
  sums = _mm_add_ss(sums, shuf);
  return _mm_cvtss_f32(sums) / 64.0f;
}

void Denoiser::extract_patch(const float* img, int w, int x, int y,
                             float* out) {
  if (x + 8 <= w) {
    for (int dy = 0; dy < 8; ++dy) {
      __m256 v_row = _mm256_loadu_ps(img + (y + dy) * w + x);
      _mm256_storeu_ps(out + dy * 8, v_row);
    }
  } else {
    for (int dy = 0; dy < 8; ++dy) {
      int remaining = w - x;
      std::copy(img + (y + dy) * w + x, img + (y + dy) * w + x + remaining,
                out + dy * 8);
      for (int dx = remaining; dx < 8; ++dx)
        out[dy * 8 + dx] = out[dy * 8 + remaining - 1];
    }
  }
}

void Denoiser::dct_1d_8(float* x, const float* coeffs) {
  float tmp[8];
  std::copy(x, x + 8, tmp);
  __m256 v_tmp = _mm256_loadu_ps(tmp);
  for (int k = 0; k < 8; ++k) {
    __m256 v_coeffs = _mm256_loadu_ps(coeffs + k * 8);
    __m256 v_prod = _mm256_mul_ps(v_tmp, v_coeffs);
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
    __m256 v_coeffs = _mm256_loadu_ps(coeffs + n * 8);
    __m256 v_prod = _mm256_mul_ps(v_tmp, v_coeffs);
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
    for (int x = y + 1; x < 8; ++x) std::swap(b[y * 8 + x], b[x * 8 + y]);
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
  if (n == 16) {
    float a0 = data[0] + data[1];
    float a1 = data[0] - data[1];
    float a2 = data[2] + data[3];
    float a3 = data[2] - data[3];
    float a4 = data[4] + data[5];
    float a5 = data[4] - data[5];
    float a6 = data[6] + data[7];
    float a7 = data[6] - data[7];
    float a8 = data[8] + data[9];
    float a9 = data[8] - data[9];
    float a10 = data[10] + data[11];
    float a11 = data[10] - data[11];
    float a12 = data[12] + data[13];
    float a13 = data[12] - data[13];
    float a14 = data[14] + data[15];
    float a15 = data[14] - data[15];
    float b0 = a0 + a2;
    float b1 = a1 + a3;
    float b2 = a0 - a2;
    float b3 = a1 - a3;
    float b4 = a4 + a6;
    float b5 = a5 + a7;
    float b6 = a4 - a6;
    float b7 = a5 - a7;
    float b8 = a8 + a10;
    float b9 = a9 + a11;
    float b10 = a8 - a10;
    float b11 = a9 - a11;
    float b12 = a12 + a14;
    float b13 = a13 + a15;
    float b14 = a12 - a14;
    float b15 = a13 - a15;
    float c0 = b0 + b4;
    float c1 = b1 + b5;
    float c2 = b2 + b6;
    float c3 = b3 + b7;
    float c4 = b0 - b4;
    float c5 = b1 - b5;
    float c6 = b2 - b6;
    float c7 = b3 - b7;
    float c8 = b8 + b12;
    float c9 = b9 + b13;
    float c10 = b10 + b14;
    float c11 = b11 + b15;
    float c12 = b8 - b12;
    float c13 = b9 - b13;
    float c14 = b10 - b14;
    float c15 = b11 - b15;
    float d0 = c0 + c8;
    float d1 = c1 + c9;
    float d2 = c2 + c10;
    float d3 = c3 + c11;
    float d4 = c4 + c12;
    float d5 = c5 + c13;
    float d6 = c6 + c14;
    float d7 = c7 + c15;
    float d8 = c0 - c8;
    float d9 = c1 - c9;
    float d10 = c2 - c10;
    float d11 = c3 - c11;
    float d12 = c4 - c12;
    float d13 = c5 - c13;
    float d14 = c6 - c14;
    float d15 = c7 - c15;
    const float s = 0.25f;
    data[0] = d0 * s;
    data[1] = d1 * s;
    data[2] = d2 * s;
    data[3] = d3 * s;
    data[4] = d4 * s;
    data[5] = d5 * s;
    data[6] = d6 * s;
    data[7] = d7 * s;
    data[8] = d8 * s;
    data[9] = d9 * s;
    data[10] = d10 * s;
    data[11] = d11 * s;
    data[12] = d12 * s;
    data[13] = d13 * s;
    data[14] = d14 * s;
    data[15] = d15 * s;
    return;
  }
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

void Denoiser::transform_3d(float* stack, int group_size,
                            const DctTables& tables) {
  for (int i = 0; i < group_size; ++i)
    dct_2d_8x8(stack + i * 64, tables.dct_coeff);
  for (int i = 0; i < 64; ++i) {
    float col[MAX_GROUP_SIZE];
    for (int k = 0; k < group_size; ++k) col[k] = stack[k * 64 + i];
    walsh_hadamard_1d(col, group_size);
    for (int k = 0; k < group_size; ++k) stack[k * 64 + i] = col[k];
  }
}

void Denoiser::inverse_transform_3d(float* stack, int group_size,
                                    const DctTables& tables) {
  for (int i = 0; i < 64; ++i) {
    float col[MAX_GROUP_SIZE];
    for (int k = 0; k < group_size; ++k) col[k] = stack[k * 64 + i];
    walsh_hadamard_1d(col, group_size);
    for (int k = 0; k < group_size; ++k) stack[k * 64 + i] = col[k];
  }
  for (int i = 0; i < group_size; ++i)
    idct_2d_8x8(stack + i * 64, tables.idct_coeff);
}

}  // namespace photon
