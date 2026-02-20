#include "DemosaicEngine.h"

#include <QDebug>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace photon {

DemosaicEngine::DemosaicEngine(QObject* parent) : QObject(parent) {}

DemosaicEngine::~DemosaicEngine() = default;

QStringList DemosaicEngine::availableMethods() {
  return QStringList() << "LibRaw" << "PPG" << "RCD" << "AMaZE" << "VNG4";
}

QString DemosaicEngine::methodName(DemosaicMethod method) {
  switch (method) {
    case DemosaicMethod::LibRaw:
      return "LibRaw";
    case DemosaicMethod::PPG:
      return "PPG";
    case DemosaicMethod::RCD:
      return "RCD";
    case DemosaicMethod::AMaZE:
      return "AMaZE";
    case DemosaicMethod::VNG4:
      return "VNG4";
    default:
      return "Unknown";
  }
}

DemosaicMethod DemosaicEngine::methodFromString(const QString& name) {
  if (name == "PPG") return DemosaicMethod::PPG;
  if (name == "RCD") return DemosaicMethod::RCD;
  if (name == "AMaZE") return DemosaicMethod::AMaZE;
  if (name == "VNG4") return DemosaicMethod::VNG4;
  return DemosaicMethod::LibRaw;
}

bool DemosaicEngine::demosaic(const float* input, float* output, int width,
                              int height, uint32_t filters,
                              DemosaicMethod method) {
  if (!input || !output || width <= 0 || height <= 0) {
    return false;
  }

  switch (method) {
    case DemosaicMethod::PPG:
      return demosaicPPG(input, output, width, height, filters);
    case DemosaicMethod::RCD:
      return demosaicRCD(input, output, width, height, filters);
    case DemosaicMethod::LibRaw:
    default:
      // For LibRaw, we don't handle it here - caller should use dcraw_process
      return false;
  }
}

// PPG (Patterned Pixel Grouping) implementation
// Based on darktable's PPG demosaicing algorithm
bool DemosaicEngine::demosaicPPG(const float* input, float* output, int width,
                                 int height, uint32_t filters) {
  if (width < 6 || height < 6) {
    return false;
  }

  // Step 1: Border interpolation (3-pixel border)
  borderInterpolate(output, input, width, height, filters, 3);

  // Step 2: Interpolate green channel
  interpolateGreenPPG(output, input, width, height, filters);

  // Step 3: Interpolate red and blue channels
  interpolateRedBluePPG(output, width, height, filters);

  return true;
}

void DemosaicEngine::borderInterpolate(float* output, const float* input,
                                       int width, int height, uint32_t filters,
                                       int border_size) {
  float sum[8];

  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i++) {
      // Skip inner region
      if (i == border_size && j >= border_size && j < height - border_size) {
        i = width - border_size;
      }
      if (i == width) break;

      std::memset(sum, 0, sizeof(sum));

      // Collect samples from 3x3 neighborhood
      for (int y = j - 1; y <= j + 1; y++) {
        for (int x = i - 1; x <= i + 1; x++) {
          if (y >= 0 && x >= 0 && y < height && x < width) {
            int f = fc(y, x, filters);
            sum[f] += std::max(0.0f, input[y * width + x]);
            sum[f + 4] += 1.0f;
          }
        }
      }

      int f = fc(j, i, filters);
      for (int c = 0; c < 3; c++) {
        if (c != f && sum[c + 4] > 0.0f) {
          output[(j * width + i) * 4 + c] = sum[c] / sum[c + 4];
        } else {
          output[(j * width + i) * 4 + c] =
              std::max(0.0f, input[j * width + i]);
        }
      }
      output[(j * width + i) * 4 + 3] = 0.0f;
    }
  }
}

void DemosaicEngine::interpolateGreenPPG(float* output, const float* input,
                                         int width, int height,
                                         uint32_t filters) {
  // Process inner region (excluding 3-pixel border)
  for (int j = 3; j < height - 3; j++) {
    for (int i = 3; i < width - 3; i++) {
      int c = fc(j, i, filters);
      float* pixel = &output[(j * width + i) * 4];
      const float* in_row = &input[j * width];

      if (c == 0 || c == 2) {  // Red or blue pixel
        float pc = std::max(0.0f, in_row[i]);
        pixel[c] = pc;

        // Get neighbors
        float pym = std::max(0.0f, in_row[i - width]);
        float pym2 = std::max(0.0f, in_row[i - 2 * width]);
        float pym3 = std::max(0.0f, in_row[i - 3 * width]);
        float pyM = std::max(0.0f, in_row[i + width]);
        float pyM2 = std::max(0.0f, in_row[i + 2 * width]);
        float pyM3 = std::max(0.0f, in_row[i + 3 * width]);
        float pxm = std::max(0.0f, in_row[i - 1]);
        float pxm2 = std::max(0.0f, in_row[i - 2]);
        float pxm3 = std::max(0.0f, in_row[i - 3]);
        float pxM = std::max(0.0f, in_row[i + 1]);
        float pxM2 = std::max(0.0f, in_row[i + 2]);
        float pxM3 = std::max(0.0f, in_row[i + 3]);

        // Calculate directional estimates
        float guessx = (pxm + pc + pxM) * 2.0f - pxM2 - pxm2;
        float diffx = (std::fabs(pxm2 - pc) + std::fabs(pxM2 - pc) +
                       std::fabs(pxm - pxM)) *
                          3.0f +
                      (std::fabs(pxM3 - pxM) + std::fabs(pxm3 - pxm)) * 2.0f;
        float guessy = (pym + pc + pyM) * 2.0f - pyM2 - pym2;
        float diffy = (std::fabs(pym2 - pc) + std::fabs(pyM2 - pc) +
                       std::fabs(pym - pyM)) *
                          3.0f +
                      (std::fabs(pyM3 - pyM) + std::fabs(pym3 - pym)) * 2.0f;

        if (diffx > diffy) {
          // Use vertical direction
          float m = std::min(pym, pyM);
          float M = std::max(pym, pyM);
          pixel[1] = std::max(std::min(guessy * 0.25f, M), m);
        } else {
          // Use horizontal direction
          float m = std::min(pxm, pxM);
          float M = std::max(pxm, pxM);
          pixel[1] = std::max(std::min(guessx * 0.25f, M), m);
        }
      } else {
        // Green pixel - just copy
        pixel[1] = std::max(0.0f, in_row[i]);
      }
      pixel[3] = 0.0f;
    }
  }
}

static float hueTransit(float l1, float l2, float l3, float v1, float v3) {
  if (std::abs(l3 - l1) < 1e-6f) return (v1 + v3) * 0.5f + (l2 * 2.0f - l1 - l3) * 0.25f;
  if ((l1 < l2 && l2 < l3) || (l1 > l2 && l2 > l3)) {
    return v1 + (v3 - v1) * (l2 - l1) / (l3 - l1);
  } else {
    return (v1 + v3) * 0.5f + (l2 * 2.0f - l1 - l3) * 0.25f;
  }
}

void DemosaicEngine::interpolateRedBluePPG(float* output, int width, int height,
                                           uint32_t filters) {
  // Process inner region (excluding 1-pixel border)
  for (int j = 1; j < height - 1; j++) {
    for (int i = 1; i < width - 1; i++) {
      int c = fc(j, i, filters);
      float* pixel = &output[(j * width + i) * 4];

      if (c & 1) {  // Green pixel
        // Calculate red and blue for green pixels
        float* nt = &output[((j - 1) * width + i) * 4];
        float* nb = &output[((j + 1) * width + i) * 4];
        float* nl = &output[(j * width + i - 1) * 4];
        float* nr = &output[(j * width + i + 1) * 4];

        float g_x = pixel[1];
        float g_n = nt[1], g_s = nb[1], g_w = nl[1], g_e = nr[1];

        if (fc(j, i + 1, filters) == 0) {  // Red neighbor in same row
          pixel[0] = hueTransit(g_w, g_x, g_e, nl[0], nr[0]);
          pixel[2] = hueTransit(g_n, g_x, g_s, nt[2], nb[2]);
        } else {  // Blue neighbor in same row
          pixel[2] = hueTransit(g_w, g_x, g_e, nl[2], nr[2]);
          pixel[0] = hueTransit(g_n, g_x, g_s, nt[0], nb[0]);
        }
      } else {
        // Red or blue pixel - fill the other color
        float* ntl = &output[((j - 1) * width + i - 1) * 4];
        float* ntr = &output[((j - 1) * width + i + 1) * 4];
        float* nbl = &output[((j + 1) * width + i - 1) * 4];
        float* nbr = &output[((j + 1) * width + i + 1) * 4];

        float g_x = pixel[1];
        float g_nw = ntl[1], g_ne = ntr[1], g_sw = nbl[1], g_se = nbr[1];

        if (c == 0) {  // Red pixel, fill blue
          float diff_ne_sw = std::abs(ntr[2] - nbl[2]) + std::abs(g_ne - g_x) + std::abs(g_sw - g_x);
          float diff_nw_se = std::abs(ntl[2] - nbr[2]) + std::abs(g_nw - g_x) + std::abs(g_se - g_x);

          if (diff_ne_sw < diff_nw_se) {
            pixel[2] = hueTransit(g_ne, g_x, g_sw, ntr[2], nbl[2]);
          } else {
            pixel[2] = hueTransit(g_nw, g_x, g_se, ntl[2], nbr[2]);
          }
        } else {  // Blue pixel, fill red
          float diff_ne_sw = std::abs(ntr[0] - nbl[0]) + std::abs(g_ne - g_x) + std::abs(g_sw - g_x);
          float diff_nw_se = std::abs(ntl[0] - nbr[0]) + std::abs(g_nw - g_x) + std::abs(g_se - g_x);

          if (diff_ne_sw < diff_nw_se) {
            pixel[0] = hueTransit(g_ne, g_x, g_sw, ntr[0], nbl[0]);
          } else {
            pixel[0] = hueTransit(g_nw, g_x, g_se, ntl[0], nbr[0]);
          }
        }
      }
      pixel[3] = 0.0f;
    }
  }
}

// Simplified RCD implementation
// Based on darktable's RCD demosaicing algorithm
bool DemosaicEngine::demosaicRCD(const float* input, float* output, int width,
                                 int height, uint32_t filters) {
  // Constants from RCD paper/implementation
  constexpr int RCD_TILESIZE = 112; // Optimized for L2 cache
  constexpr int RCD_BORDER = 9;     // Avoid tile-overlap errors
  constexpr int RCD_MARGIN = 7;     // Outer border
  constexpr int RCD_TILEVALID = RCD_TILESIZE - 2 * RCD_BORDER;

  if (width < 2 * RCD_BORDER || height < 2 * RCD_BORDER) {
    rcdBorderInterpolate(output, input, width, height, filters, RCD_BORDER);
    return true;
  }

  // Handle borders first
  rcdBorderInterpolate(output, input, width, height, filters, RCD_MARGIN);

  const int num_vertical =
      1 + (height - 2 * RCD_BORDER - 1) / RCD_TILEVALID;
  const int num_horizontal =
      1 + (width - 2 * RCD_BORDER - 1) / RCD_TILEVALID;

  // Allocate tile buffers
  // Using std::vector for automatic memory management
  std::vector<float> vh_dir(RCD_TILESIZE * RCD_TILESIZE);
  std::vector<float> cfa(RCD_TILESIZE * RCD_TILESIZE);
  std::vector<float> pq_dir(RCD_TILESIZE * RCD_TILESIZE / 2);
  
  // rgb buffer: 3 channels
  std::vector<float> rgb0(RCD_TILESIZE * RCD_TILESIZE);
  std::vector<float> rgb1(RCD_TILESIZE * RCD_TILESIZE);
  std::vector<float> rgb2(RCD_TILESIZE * RCD_TILESIZE);
  float* rgb[3] = {rgb0.data(), rgb1.data(), rgb2.data()};

  // lpf reuses pq_dir memory in darktable implementation
  float* lpf = pq_dir.data(); 

  for (int tile_vertical = 0; tile_vertical < num_vertical; tile_vertical++) {
    for (int tile_horizontal = 0; tile_horizontal < num_horizontal; tile_horizontal++) {
      const int rowStart = tile_vertical * RCD_TILEVALID;
      const int rowEnd = std::min(rowStart + RCD_TILESIZE, height);

      const int colStart = tile_horizontal * RCD_TILEVALID;
      const int colEnd = std::min(colStart + RCD_TILESIZE, width);

      const int tileRows = std::min(rowEnd - rowStart, RCD_TILESIZE);
      const int tileCols = std::min(colEnd - colStart, RCD_TILESIZE);

      if (rowStart + RCD_TILESIZE > height ||
          colStart + RCD_TILESIZE > width) {
        std::fill(vh_dir.begin(), vh_dir.end(), 0.0f);
        std::fill(rgb0.begin(), rgb0.end(), 0.0f);
        std::fill(rgb1.begin(), rgb1.end(), 0.0f);
        std::fill(rgb2.begin(), rgb2.end(), 0.0f);
      }

      // Step 0: Fill data
      for (int row = rowStart; row < rowEnd; row++) {
        for (int col = colStart, indx = (row - rowStart) * RCD_TILESIZE,
                 in_indx = row * width + colStart;
             col < colEnd; col++, indx++, in_indx++) {
          float val = std::max(0.0f, input[in_indx]);
          cfa[indx] = val;
          int c = fc(row, col, filters);
          rgb[c][indx] = val;
        }
      }

      // Step 1: Find interpolation directions
      rcdCalculateVHDir(vh_dir.data(), cfa.data(), tileRows, tileCols, RCD_TILESIZE);

      // Step 2: Low pass filter
      rcdCalculateLPF(lpf, cfa.data(), tileRows, tileCols, RCD_TILESIZE, filters);

      // Step 3: Populate Green
      rcdInterpolateGreen(rgb, cfa.data(), vh_dir.data(), lpf, tileRows, tileCols, RCD_TILESIZE, filters);

      // Step 4: P/Q Directions
      rcdCalculatePQDir(pq_dir.data(), cfa.data(), tileRows, tileCols, RCD_TILESIZE, filters);

      // Step 4 & 5: Red/Blue
      rcdInterpolateRedBlue(rgb, cfa.data(), pq_dir.data(), vh_dir.data(), tileRows, tileCols, RCD_TILESIZE, filters);

      // Write output
      const int first_vertical = rowStart + ((tile_vertical == 0) ? RCD_MARGIN : RCD_BORDER);
      const int last_vertical = rowEnd - ((tile_vertical == num_vertical - 1) ? RCD_MARGIN : RCD_BORDER);
      const int first_horizontal = colStart + ((tile_horizontal == 0) ? RCD_MARGIN : RCD_BORDER);
      const int last_horizontal = colEnd - ((tile_horizontal == num_horizontal - 1) ? RCD_MARGIN : RCD_BORDER);

      for (int row = first_vertical; row < last_vertical; row++) {
        for (int col = first_horizontal,
                 idx = (row - rowStart) * RCD_TILESIZE + col - colStart,
                 o_idx = (row * width + col) * 4;
             col < last_horizontal; col++, o_idx += 4, idx++) {
          output[o_idx] = std::max(0.0f, rgb[0][idx]);
          output[o_idx + 1] = std::max(0.0f, rgb[1][idx]);
          output[o_idx + 2] = std::max(0.0f, rgb[2][idx]);
          output[o_idx + 3] = 0.0f; // Alpha
        }
      }
    }
  }

  return true;
}

// RCD Helper Implementations

void DemosaicEngine::rcdCalculateVHDir(float* vh_dir, const float* cfa, int tile_rows,
                                     int tile_cols, int tile_size) {
    constexpr float epssq = 1e-10f;
    std::vector<float> bufferV[3];
    for(int i=0; i<3; ++i) bufferV[i].resize(tile_size);
    std::vector<float> bufferH(tile_size);

    float* V0 = bufferV[0].data();
    float* V1 = bufferV[1].data();
    float* V2 = bufferV[2].data();

    // Fill bufferV initially
    for(int row = 3; row < std::min(tile_rows - 3, 5); row++) {
        for(int col = 4, indx = row * tile_size + col; col < tile_cols - 4; col++, indx++) {
            float v_val = (cfa[indx - 3*tile_size] - cfa[indx - tile_size] - cfa[indx + tile_size] + cfa[indx + 3*tile_size]) 
                           - 3.0f * (cfa[indx - 2*tile_size] + cfa[indx + 2*tile_size]) + 6.0f * cfa[indx];
            bufferV[row - 3][col - 4] = v_val * v_val; 
        }
    }

    for(int row = 4; row < tile_rows - 4; row++) {
        for(int col = 3, indx = row * tile_size + col; col < tile_cols - 3; col++, indx++) {
            float h_val = (cfa[indx - 3] - cfa[indx - 1] - cfa[indx + 1] + cfa[indx + 3]) 
                          - 3.0f * (cfa[indx - 2] + cfa[indx + 2]) + 6.0f * cfa[indx];
            bufferH[col - 3] = h_val * h_val;
        }
        
        for(int col = 4, indx = (row + 1) * tile_size + col; col < tile_cols - 4; col++, indx++) {
             float v_val = (cfa[indx - 3*tile_size] - cfa[indx - tile_size] - cfa[indx + tile_size] + cfa[indx + 3*tile_size]) 
                          - 3.0f * (cfa[indx - 2*tile_size] + cfa[indx + 2*tile_size]) + 6.0f * cfa[indx];
             V2[col - 4] = v_val * v_val;
        }

        for(int col = 4, indx = row * tile_size + col; col < tile_cols - 4; col++, indx++) {
            float V_Stat = std::max(epssq, V0[col - 4] + V1[col - 4] + V2[col - 4]);
            float H_Stat = std::max(epssq, bufferH[col - 4] + bufferH[col - 3] + bufferH[col - 2]);
            vh_dir[indx] = V_Stat / (V_Stat + H_Stat);
        }
        
        // Rotate buffers
        float* tmp = V0; V0 = V1; V1 = V2; V2 = tmp;
    }
}

void DemosaicEngine::rcdCalculateLPF(float* lpf, const float* cfa, int tile_rows,
                       int tile_cols, int tile_size, uint32_t filters) {
    int w1 = tile_size;
    for(int row = 2; row < tile_rows - 2; row++) {
        for(int col = 2 + (fc(row, 0, filters) & 1), indx = row * tile_size + col, lp_indx = indx / 2; 
            col < tile_cols - 2; col += 2, indx += 2, lp_indx++) {
            
            lpf[lp_indx] = cfa[indx]
                        + 0.5f * (cfa[indx - w1]     + cfa[indx + w1] +     cfa[indx - 1] +      cfa[indx + 1])
                       + 0.25f * (cfa[indx - w1 - 1] + cfa[indx - w1 + 1] + cfa[indx + w1 - 1] + cfa[indx + w1 + 1]);
        }
    }
}

void DemosaicEngine::rcdInterpolateGreen(float* rgb[3], const float* cfa, float* vh_dir,
                           const float* lpf, int tile_rows, int tile_cols,
                           int tile_size, uint32_t filters) {
    constexpr float eps = 1e-5f;
    int w1 = tile_size;
    int w2 = 2 * tile_size;
    int w3 = 3 * tile_size;
    int w4 = 4 * tile_size;

    for(int row = 4; row < tile_rows - 4; row++) {
        for(int col = 4 + (fc(row, 0, filters) & 1), indx = row * tile_size + col, lpindx = indx / 2; 
            col < tile_cols - 4; col += 2, indx += 2, lpindx++) {
            
            float cfai = cfa[indx];
            float lpfi = lpf[lpindx];

            // Cardinal gradients
            float N_Grad = eps + std::abs(cfa[indx - w1] - cfa[indx + w1]) + std::abs(cfai - cfa[indx - w2]) + std::abs(cfa[indx - w1] - cfa[indx - w3]) + std::abs(cfa[indx - w2] - cfa[indx - w4]);
            float S_Grad = eps + std::abs(cfa[indx - w1] - cfa[indx + w1]) + std::abs(cfai - cfa[indx + w2]) + std::abs(cfa[indx + w1] - cfa[indx + w3]) + std::abs(cfa[indx + w2] - cfa[indx + w4]);
            float W_Grad = eps + std::abs(cfa[indx -  1] - cfa[indx +  1]) + std::abs(cfai - cfa[indx -  2]) + std::abs(cfa[indx -  1] - cfa[indx -  3]) + std::abs(cfa[indx -  2] - cfa[indx -  4]);
            float E_Grad = eps + std::abs(cfa[indx -  1] - cfa[indx +  1]) + std::abs(cfai - cfa[indx +  2]) + std::abs(cfa[indx +  1] - cfa[indx +  3]) + std::abs(cfa[indx +  2] - cfa[indx +  4]);

            // Cardinal pixel estimations
            float N_Est = cfa[indx - w1] * (lpfi + lpfi) / (eps + lpfi + lpf[lpindx - w1]);
            float S_Est = cfa[indx + w1] * (lpfi + lpfi) / (eps + lpfi + lpf[lpindx + w1]);
            float W_Est = cfa[indx -  1] * (lpfi + lpfi) / (eps + lpfi + lpf[lpindx -  1]);
            float E_Est = cfa[indx +  1] * (lpfi + lpfi) / (eps + lpfi + lpf[lpindx +  1]);

            // Vertical and horizontal estimations
            float V_Est = (S_Grad * N_Est + N_Grad * S_Est) / (N_Grad + S_Grad);
            float H_Est = (W_Grad * E_Est + E_Grad * W_Est) / (E_Grad + W_Grad);

            // Refined vertical and horizontal local discrimination
            float VH_Central_Value = vh_dir[indx];
            float VH_Neighbourhood_Value = 0.25f * (vh_dir[indx - w1 - 1] + vh_dir[indx - w1 + 1] + vh_dir[indx + w1 - 1] + vh_dir[indx + w1 + 1]);
            float VH_Disc = (std::abs(0.5f - VH_Central_Value) < std::abs(0.5f - VH_Neighbourhood_Value)) ? VH_Neighbourhood_Value : VH_Central_Value;

            // Interpolate
            rgb[1][indx] = H_Est + VH_Disc * (V_Est - H_Est);
        }
    }
}

void DemosaicEngine::rcdCalculatePQDir(float* pq_dir, const float* cfa, int tile_rows,
                         int tile_cols, int tile_size, uint32_t filters) {
    constexpr float epssq = 1e-10f;
    int w1 = tile_size;
    int w2 = 2 * tile_size;
    int w3 = 3 * tile_size;
    
    std::vector<float> p_cdiff(tile_size * tile_size / 2);
    std::vector<float> q_cdiff(tile_size * tile_size / 2);

    for(int row = 3; row < tile_rows - 3; row++) {
        for(int col = 3, indx = row * tile_size + col, indx2 = indx / 2; 
            col < tile_cols - 3; col+=2, indx+=2, indx2++) {
            float p_val = (cfa[indx - w3 - 3] - cfa[indx - w1 - 1] - cfa[indx + w1 + 1] + cfa[indx + w3 + 3]) 
                          - 3.0f * (cfa[indx - w2 - 2] + cfa[indx + w2 + 2]) + 6.0f * cfa[indx];
            p_cdiff[indx2] = p_val * p_val;

            float q_val = (cfa[indx - w3 + 3] - cfa[indx - w1 + 1] - cfa[indx + w1 - 1] + cfa[indx + w3 - 3]) 
                          - 3.0f * (cfa[indx - w2 + 2] + cfa[indx + w2 - 2]) + 6.0f * cfa[indx];
            q_cdiff[indx2] = q_val * q_val;
        }
    }

    for(int row = 4; row < tile_rows - 4; row++) {
        for(int col = 4 + (fc(row, 0, filters) & 1), indx = row * tile_size + col, 
            indx2 = indx / 2, indx3 = (indx - w1 - 1) / 2, indx4 = (indx + w1 - 1) / 2; 
            col < tile_cols - 4; col += 2, indx += 2, indx2++, indx3++, indx4++ ) {
            
            float P_Stat = std::max(epssq, p_cdiff[indx3]     + p_cdiff[indx2] + p_cdiff[indx4 + 1]);
            float Q_Stat = std::max(epssq, q_cdiff[indx3 + 1] + q_cdiff[indx2] + q_cdiff[indx4]);
            pq_dir[indx2] = P_Stat / (P_Stat + Q_Stat);
        }
    }
}

void DemosaicEngine::rcdInterpolateRedBlue(float* rgb[3], const float* cfa, float* pq_dir,
                             const float* vh_dir, int tile_rows, int tile_cols,
                             int tile_size, uint32_t filters) {
    constexpr float eps = 1e-5f;
    int w1 = tile_size;
    int w2 = 2 * tile_size;
    int w3 = 3 * tile_size;

    // Red/Blue at Blue/Red locations
    for(int row = 4; row < tile_rows - 4; row++) {
        for(int col = 4 + (fc(row, 0, filters) & 1), indx = row * tile_size + col, 
            c = 2 - fc(row, col, filters), pqindx = indx / 2, pqindx2 = (indx - w1 - 1) / 2, pqindx3 = (indx + w1 - 1) / 2; 
            col < tile_cols - 4; col += 2, indx += 2, pqindx++, pqindx2++, pqindx3++) {
            
            float PQ_Central_Value = pq_dir[pqindx];
            float PQ_Neighbourhood_Value = 0.25f * (pq_dir[pqindx2] + pq_dir[pqindx2 + 1] + pq_dir[pqindx3] + pq_dir[pqindx3 + 1]);
            float PQ_Disc = (std::abs(0.5f - PQ_Central_Value) < std::abs(0.5f - PQ_Neighbourhood_Value)) ? PQ_Neighbourhood_Value : PQ_Central_Value;

            // Diagonal gradients
            float NW_Grad = eps + std::abs(rgb[c][indx - w1 - 1] - rgb[c][indx + w1 + 1]) + std::abs(rgb[c][indx - w1 - 1] - rgb[c][indx - w3 - 3]) + std::abs(rgb[1][indx] - rgb[1][indx - w2 - 2]);
            float NE_Grad = eps + std::abs(rgb[c][indx - w1 + 1] - rgb[c][indx + w1 - 1]) + std::abs(rgb[c][indx - w1 + 1] - rgb[c][indx - w3 + 3]) + std::abs(rgb[1][indx] - rgb[1][indx - w2 + 2]);
            float SW_Grad = eps + std::abs(rgb[c][indx - w1 + 1] - rgb[c][indx + w1 - 1]) + std::abs(rgb[c][indx + w1 - 1] - rgb[c][indx + w3 - 3]) + std::abs(rgb[1][indx] - rgb[1][indx + w2 - 2]);
            float SE_Grad = eps + std::abs(rgb[c][indx - w1 - 1] - rgb[c][indx + w1 + 1]) + std::abs(rgb[c][indx + w1 + 1] - rgb[c][indx + w3 + 3]) + std::abs(rgb[1][indx] - rgb[1][indx + w2 + 2]);

            // Diagonal colour differences
            float NW_Est = rgb[c][indx - w1 - 1] - rgb[1][indx - w1 - 1];
            float NE_Est = rgb[c][indx - w1 + 1] - rgb[1][indx - w1 + 1];
            float SW_Est = rgb[c][indx + w1 - 1] - rgb[1][indx + w1 - 1];
            float SE_Est = rgb[c][indx + w1 + 1] - rgb[1][indx + w1 + 1];

            // P/Q estimations
            float P_Est = (NW_Grad * SE_Est + SE_Grad * NW_Est) / (NW_Grad + SE_Grad);
            float Q_Est = (NE_Grad * SW_Est + SW_Grad * NE_Est) / (NE_Grad + SW_Grad);

            // Interpolate
            rgb[c][indx] = rgb[1][indx] + Q_Est + PQ_Disc * (P_Est - Q_Est);
        }
    }

    // Red/Blue at Green locations
    for(int row = 4; row < tile_rows - 4; row++) {
        for(int col = 4 + (fc(row, 1, filters) & 1), indx = row * tile_size + col; 
            col < tile_cols - 4; col += 2, indx +=2) {
            
            float VH_Central_Value = vh_dir[indx];
            float VH_Neighbourhood_Value = 0.25f * (vh_dir[indx - w1 - 1] + vh_dir[indx - w1 + 1] + vh_dir[indx + w1 - 1] + vh_dir[indx + w1 + 1]);
            float VH_Disc = (std::abs(0.5f - VH_Central_Value) < std::abs(0.5f - VH_Neighbourhood_Value) ) ? VH_Neighbourhood_Value : VH_Central_Value;
            
            float rgb1 = rgb[1][indx];
            float N1 = eps + std::abs(rgb1 - rgb[1][indx - w2]);
            float S1 = eps + std::abs(rgb1 - rgb[1][indx + w2]);
            float W1 = eps + std::abs(rgb1 - rgb[1][indx -  2]);
            float E1 = eps + std::abs(rgb1 - rgb[1][indx +  2]);

            float rgb1mw1 = rgb[1][indx - w1];
            float rgb1pw1 = rgb[1][indx + w1];
            float rgb1m1 =  rgb[1][indx - 1];
            float rgb1p1 =  rgb[1][indx + 1];

            for(int c = 0; c <= 2; c += 2) {
              float SNabs = std::abs(rgb[c][indx - w1] - rgb[c][indx + w1]);
              float EWabs = std::abs(rgb[c][indx -  1] - rgb[c][indx +  1]);

              float N_Grad = N1 + SNabs + std::abs(rgb[c][indx - w1] - rgb[c][indx - w3]);
              float S_Grad = S1 + SNabs + std::abs(rgb[c][indx + w1] - rgb[c][indx + w3]);
              float W_Grad = W1 + EWabs + std::abs(rgb[c][indx -  1] - rgb[c][indx -  3]);
              float E_Grad = E1 + EWabs + std::abs(rgb[c][indx +  1] - rgb[c][indx +  3]);

              float N_Est = rgb[c][indx - w1] - rgb1mw1;
              float S_Est = rgb[c][indx + w1] - rgb1pw1;
              float W_Est = rgb[c][indx -  1] - rgb1m1;
              float E_Est = rgb[c][indx +  1] - rgb1p1;

              float V_Est = (N_Grad * S_Est + S_Grad * N_Est) / (N_Grad + S_Grad);
              float H_Est = (E_Grad * W_Est + W_Grad * E_Est) / (E_Grad + W_Grad);

              rgb[c][indx] = rgb1 + H_Est + VH_Disc * (V_Est - H_Est);
            }
        }
    }
}

void DemosaicEngine::rcdBorderInterpolate(float* output, const float* input, int width,
                            int height, uint32_t filters, int margin) {
    borderInterpolate(output, input, width, height, filters, margin);
}

}  // namespace photon
