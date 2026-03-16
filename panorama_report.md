# Panorama Stitching Technical Report

## Executive Summary

This report analyzes the panorama stitching implementation found in RapidRAW, a Rust-based RAW image processing application. The system implements a sophisticated feature-based stitching pipeline that combines classical computer vision algorithms with modern optimizations including parallel processing and adaptive seam finding.

---

## Phase 1: Image Loading and Preprocessing

### High-Level Overview

The first phase converts RAW images into a processable format while preparing them for computationally expensive feature detection operations through intelligent downscaling.

### Algorithmic Details

**RAW Processing Pipeline:**

```rust
let mut dynamic_image = load_base_image_from_bytes(&file_bytes, filename, false, 2.5, None)?;
if is_raw_file(filename) {
    apply_cpu_default_raw_processing(&mut dynamic_image);
}
let image_f32 = dynamic_image.to_rgb32f();
```

1. **RAW Decoding**: Uses internal RAW loader with exposure boost (2.5x)
2. **CPU Demosaicing**: Applies default RAW processing pipeline (likely using DCraw or similar)
3. **Format Conversion**: Converts to RGB32F (32-bit floating point per channel) for high dynamic range preservation

**Adaptive Downscaling Strategy:**

```rust
const MAX_PROCESSING_DIMENSION: u32 = 1600;

pub fn calculate_downscale_dimensions(width: u32, height: u32) -> (u32, u32, f64) {
    let long_side = width.max(height);
    if long_side <= MAX_PROCESSING_DIMENSION {
        return (width, height, 1.0);
    }
    let scale_factor = long_side as f64 / MAX_PROCESSING_DIMENSION as f64;
    let new_width = (width as f64 / scale_factor).round() as u32;
    let new_height = (height as f64 / scale_factor).round() as u32;
    (new_width, new_height, scale_factor)
}
```

This ensures feature detection operates on manageable 1600px images regardless of input resolution, dramatically improving performance while maintaining sufficient detail for matching.

**Low-Detail Mask Generation:**
A crucial preprocessing step identifies low-detail regions (sky, walls) using integral images for variance computation:

```rust
pub fn generate_low_detail_mask(gray_full: &GrayImage) -> GrayImage {
    let (sat, sat_sq) = build_integral_images(gray_full);
    // Uses 32x32 windows, threshold = 60.0 variance
}
```

The algorithm:

1. Computes Summed Area Table (SAT) for mean calculation: `SAT(x,y) = Σ_{i≤x, j≤y} I(i,j)`
2. Computes Squared SAT for variance: `Var = E[X²] - (E[X])²`
3. Marks pixels with variance < 60 as low-detail (mask value = 255)

This mask is later used during blending to expand feathering widths in low-detail regions, preventing visible seams in smooth areas.

---

## Phase 2: Feature Detection (FAST + BRIEF)

### High-Level Overview

The system employs a FAST corner detector combined with BRIEF descriptors—a computationally efficient alternative to SIFT/SURF that enables real-time performance without GPU acceleration.

### Algorithmic Details

**Step 2.1: Gaussian Preprocessing**

```rust
let blurred_img_u8 = imageproc::filter::gaussian_blur_f32(img, 1.5);
let corners = corners_fast9(&blurred_img_u8, FAST_THRESHOLD);
```

**FAST9 Corner Detection:**

- **Threshold**: 15 (intensity difference threshold)
- **Mechanism**: Checks 16 pixels on Bresenham circle around candidate pixel
- **Corner condition**: 9+ consecutive pixels with intensity > p + threshold OR < p - threshold
- **Complexity**: O(N) where N = number of pixels

**Step 2.2: Non-Maximal Suppression**

```rust
fn non_maximal_suppression(corners: &[Corner], radius: f32) -> Vec<KeyPoint> {
    let radius_sq = radius * radius; // 15.0 pixels
    // Sorts by corner score, suppresses neighbors within radius
}
```

Prevents feature clustering by keeping only the strongest corner in each 15-pixel radius neighborhood.

**Step 2.3: BRIEF Descriptor Computation**

```rust
pub const BRIEF_DESCRIPTOR_SIZE: usize = 256;
pub type Descriptor = [u8; BRIEF_DESCRIPTOR_SIZE / 8]; // 32 bytes
```

The BRIEF (Binary Robust Independent Elementary Features) algorithm:

1. **Patch Extraction**: 32×32 window around each keypoint (smoothed with σ=2.0 Gaussian)
2. **Binary Tests**: Compares 256 pre-generated random pixel pairs within the patch
3. **Bit Packing**: Each test result becomes one bit in a 32-byte descriptor

```rust
fn compute_brief_descriptor(img: &ImageBuffer<Luma<f32>, Vec<f32>>,
                           kp: &KeyPoint, patch_size: u32,
                           pairs: &[(Point2<i32>, Point2<i32>)]) -> Option<Descriptor> {
    for (i, pair) in pairs.iter().enumerate() {
        let p1_x = (kp.x as i32 + pair.0.x) as u32;
        let p1_y = (kp.y as i32 + pair.0.y) as u32;
        let p2_x = (kp.x as i32 + pair.1.x) as u32;
        let p2_y = (kp.y as i32 + pair.1.y) as u32;

        if img[p1_x, p1_y] < img[p2_x, p2_y] {
            descriptor[byte_index] |= 1 << bit_index;
        }
    }
}
```

**Random Pair Generation (Deterministic):**

```rust
pub fn generate_brief_pairs() -> Vec<(Point2<i32>, Point2<i32>)> {
    let mut rng = StdRng::seed_from_u64(12345); // Fixed seed for reproducibility
    let half_patch = BRIEF_PATCH_SIZE as i32 / 2; // 16
    // Generates 256 pairs from Uniform(-16, 16) distribution
}
```

**Advantages of FAST+BRIEF:**

- **Speed**: Binary comparisons using XOR + popcount (CPU SIMD optimized)
- **Memory**: 32 bytes vs 128 bytes for SIFT
- **Rotation Invariance**: Limited (ORB improves this with orientation)
- **Scale Invariance**: Achieved through image pyramid (not implemented here)

---

## Phase 3: Feature Matching with Ratio Test

### High-Level Overview

Features are matched between image pairs using Hamming distance, filtered by Lowe's ratio test to reject ambiguous matches.

### Algorithmic Details

**Hamming Distance Calculation:**

```rust
fn hamming_distance(d1: &Descriptor, d2: &Descriptor) -> u32 {
    d1.iter()
        .zip(d2.iter())
        .map(|(b1, b2)| (b1 ^ b2).count_ones())
        .sum()
}
```

Computes population count (number of differing bits) between two 256-bit descriptors using CPU popcount instructions.

**Brute-Force Matching with Ratio Test:**

```rust
const MATCH_RATIO_THRESHOLD: f32 = 0.8;

pub fn match_features(features1: &[Feature], features2: &[Feature]) -> Vec<Match> {
    features1.par_iter().enumerate().filter_map(|(i, f1)| {
        let mut best_dist = u32::MAX;
        let mut second_best_dist = u32::MAX;
        let mut best_idx = 0;

        for (j, f2) in features2.iter().enumerate() {
            let dist = hamming_distance(&f1.descriptor, &f2.descriptor);
            if dist < best_dist {
                second_best_dist = best_dist;
                best_dist = dist;
                best_idx = j;
            } else if dist < second_best_dist {
                second_best_dist = dist;
            }
        }

        // Lowe's ratio test: best must be significantly better than second-best
        if second_best_dist > 0 &&
           (best_dist as f32 / second_best_dist as f32) < MATCH_RATIO_THRESHOLD {
            Some(Match { index1: i, index2: best_idx })
        } else {
            None
        }
    }).collect()
}
```

**Lowe's Ratio Test (2004):**

- Rejects matches where the best match is not significantly better than the second-best
- Threshold of 0.8 means best distance must be < 80% of second-best
- Effectively eliminates matches to repetitive patterns and ambiguous features

**Computational Complexity:** O(N×M) for N features in image 1, M in image 2

- Parallelized using Rayon for multi-threading
- Could benefit from k-d tree or FLANN for large feature sets

---

## Phase 4: Geometric Verification with RANSAC

### High-Level Overview

Matches are geometrically validated using RANSAC to estimate a homography transformation, filtering out outliers caused by moving objects, parallax, or mismatches.

### Algorithmic Details

**Random Sample Consensus (RANSAC) Loop:**

```rust
const RANSAC_ITERATIONS: usize = 2500;
const RANSAC_INLIER_THRESHOLD: f64 = 5.0; // pixels
const MIN_INLIERS_FOR_CONNECTION: usize = 15;
```

**Algorithm Steps:**

1. **Random Sampling**: Select 4 random match pairs (minimum for homography)
2. **Collinearity Check**: Reject degenerate configurations

   ```rust
   fn are_points_collinear(p1: Point2<f64>, p2: Point2<f64>, p3: Point2<f64>) -> bool {
       let area = p1.x * (p2.y - p3.y) + p2.x * (p3.y - p1.y) + p3.x * (p1.y - p2.y);
       area.abs() < 1e-6
   }
   ```

3. **Homography Estimation**: Solve 8-DOF transformation using DLT (Direct Linear Transform)

**Direct Linear Transform (DLT):**

```rust
pub fn compute_homography(points: &[(Point2<f64>, Point2<f64>)]) -> Option<Matrix3<f64>> {
    // For N point pairs, builds 2N × 9 matrix A
    // Each point pair contributes 2 rows:
    // [-x, -y, -1, 0, 0, 0, x*x', y*x', x']
    // [0, 0, 0, -x, -y, -1, x*y', y*y', y']

    let a = nalgebra::DMatrix::from_rows(&a_rows);
    let svd = SVD::new(a, true, true);
    let v_t = svd.v_t.expect("SVD failed to compute V_t");
    let h_vec = v_t.row(v_t.nrows() - 1).transpose();
    Some(Matrix3::from_iterator(h_vec.iter().cloned()).transpose())
}
```

**Homography Matrix H (3×3):**

```
[x']   [h11 h12 h13] [x]
[y'] = [h21 h22 h23] [y]
[1 ]   [h31 h32 h33] [1]
```

Maps points from image 1 to image 2 coordinates using projective transformation (preserves lines but not necessarily parallelism).

1. **Inlier Counting**: Transform all points and count matches within threshold

   ```rust
   let p2_transformed = Point2::new(
       p2_h_transformed.x / p2_h_transformed.z,
       p2_transformed.y / p2_h_transformed.z,
   );
   let dist_sq = (p2.x - p2_transformed.x).powi(2) + (p2.y - p2_transformed.y).powi(2);
   if dist_sq < ransac_inlier_threshold_sq { /* inlier */ }
   ```

2. **Refinement**: Recompute homography using all inliers (more stable than single 4-point sample)

**Scale Compensation:**
Since feature detection runs on downscaled images, the homography is scaled back to full resolution:

```rust
let s1 = image_data[i].scale_factor;
let s2 = image_data[j].scale_factor;
let scale_mat_i_inv = Matrix3::new(1.0/s1, 0.0, 0.0, 0.0, 1.0/s1, 0.0, 0.0, 0.0, 1.0);
let scale_mat_j = Matrix3::new(s2, 0.0, 0.0, 0.0, s2, 0.0, 0.0, 0.0, 1.0);
let h_full = scale_mat_j * h_refined * scale_mat_i_inv;
```

---

## Phase 5: Graph-Based Image Ordering

### High-Level Overview

Images are organized into a Minimum Spanning Tree (MST) to determine optimal stitching order and compute global coordinate transformations.

### Algorithmic Details

**Disjoint Set Union (Union-Find) Structure:**

```rust
struct DSU {
    parent: Vec<usize>,
}

impl DSU {
    fn find(&mut self, i: usize) -> usize {
        if self.parent[i] == i { i }
        else {
            self.parent[i] = self.find(self.parent[i]); // Path compression
            self.parent[i]
        }
    }

    fn union(&mut self, i: usize, j: usize) {
        let root_i = self.find(i);
        let root_j = self.find(j);
        if root_i != root_j { self.parent[root_i] = root_j; }
    }
}
```

**Kruskal's MST Algorithm:**

```rust
let mut edges = Vec::new();
for (&(i, j), m) in matches {
    edges.push((m.inliers, i, j));
}
edges.sort_by_key(|&(inliers, _, _)| std::cmp::Reverse(inliers));

let mut dsu = DSU::new(n);
for &(_, i, j) in &edges {
    if dsu.find(i) != dsu.find(j) {
        dsu.union(i, j);
        mst_adj.entry(i).or_default().push(j);
        mst_adj.entry(j).or_default().push(i);
        num_edges += 1;
        if num_edges == n - 1 { break; }
    }
}
```

Prioritizes connections with more inliers (more confident matches).

**Breadth-First Traversal for Global Homographies:**

```rust
let mut q = VecDeque::new();
q.push_back((start_node, Matrix3::identity()));

while let Some((u, h_u_global)) = q.pop_front() {
    ordered_indices.push(u);
    global_homographies.insert(u, h_u_global);

    for &v in neighbors {
        if !visited.contains(&v) {
            let h_vu = if let Some(m) = matches.get(&(v, u)) {
                m.homography
            } else {
                matches.get(&(u, v)).unwrap().homography.try_inverse().unwrap()
            };
            let h_v_global = h_u_global * h_vu;
            q.push_back((v, h_v_global));
        }
    }
}
```

Chains homographies through the MST to compute each image's transformation into the global panorama coordinate system.

---

## Phase 6: Progressive Seam-Based Stitching

### High-Level Overview

The final blending phase uses dynamic programming to find optimal seams between overlapping images, with adaptive feathering based on local image content.

### Algorithmic Details

**Canvas Size Computation:**

```rust
for &img_info in images {
    let h = global_homographies[&img_info.id];
    let corners = [
        Point3::new(0.0, 0.0, 1.0),
        Point3::new(w as f64, 0.0, 1.0),
        Point3::new(w as f64, h_img as f64, 1.0),
        Point3::new(0.0, h_img as f64, 1.0),
    ];
    for p in corners.iter() {
        let tp = h * p;
        let tx = tp.x / tp.z;
        let ty = tp.y / tp.z;
        // Update bounds
    }
}
```

Transforms all image corners to determine output canvas dimensions.

**Dynamic Programming Seam Finding:**

**Vertical Seam (for horizontally overlapping images):**

```rust
fn find_pairwise_seam_dp_vertical(...) -> Vec<i32> {
    // Build cost matrix: energy = color difference between images at overlap
    for y_out in 0..out_height as usize {
        for x_out in 0..out_width as usize {
            if overlap_exists {
                cost_matrix[y_out][x_out] = sqrt(ΔR² + ΔG² + ΔB²);
            }
        }
    }

    // Dynamic programming: accumulate minimum cost paths
    for y in (first_overlap_row + 1)..=last_overlap_row {
        for x in 0..out_width as usize {
            let up_left = if x > 0 { cost_matrix[y - 1][x - 1] } else { INF };
            let up = cost_matrix[y - 1][x];
            let up_right = if x < width-1 { cost_matrix[y - 1][x + 1] } else { INF };

            cost_matrix[y][x] += up.min(up_left).min(up_right);
            path_matrix[y][x] = direction_of_min_cost;
        }
    }

    // Backtrack from minimum cost endpoint
    let mut seam = vec![0i32; out_height as usize];
    // ...trace path through path_matrix
}
```

**Energy Function:** Euclidean distance in RGB space between corresponding pixels from the two images at each overlap location.

**Adaptive Blending with Feathering:**

```rust
const FEATHER_WIDTH: f64 = 100.0;

if dist_to_seam.abs() < dynamic_feather_width / 2.0 {
    // Cosine-smoothed alpha blending
    let alpha = if new_image_is_dominant_side {
        (dist_to_seam + dynamic_feather_width / 2.0) / dynamic_feather_width
    } else {
        (-dist_to_seam + dynamic_feather_width / 2.0) / dynamic_feather_width
    };

    let weight_add = (1.0 - (alpha.clamp(0.0, 1.0) * PI).cos()) / 2.0;
    let weight_pano = 1.0 - weight_add;

    // Blend colors
    final_color = pano_color * weight_pano + add_color * weight_add;
}
```

**Content-Adaptive Feathering:**

```rust
let is_low_detail = low_detail_mask_add.get_pixel(sx_u, sy_u)[0] > 0;
let dynamic_feather_width = if is_low_detail { FEATHER_WIDTH * 5.0 } else { FEATHER_WIDTH };
```

Low-detail regions (sky, walls) receive 5x wider feathering (500px) to prevent visible seams in smooth gradients.

**Bilinear Interpolation:**

```rust
fn get_interpolated_pixel(img: &Rgb32FImage, x: f64, y: f64) -> Rgb<f32> {
    let x_floor = x.floor() as u32;
    let y_floor = y.floor() as u32;
    let dx = x - x_floor as f64;
    let dy = y - y_floor as f64;

    // Sample 4 neighbors and interpolate
    let top = p00 * (1.0 - dx) + p10 * dx;
    let bottom = p01 * (1.0 - dx) + p11 * dx;
    top * (1.0 - dy) + bottom * dy
}
```

Prevents aliasing when sampling from transformed coordinates.

---

## C++ Implementation Recommendations

### Recommended Libraries

**1. Matrix and Linear Algebra: Eigen3**

```cpp
#include <Eigen/Dense>

using Matrix3d = Eigen::Matrix<double, 3, 3>;
using Vector3d = Eigen::Matrix<double, 3, 1>;

// Homography computation
Matrix3d computeHomography(const std::vector<std::pair<Vector2d, Vector2d>>& points) {
    Eigen::MatrixXd A(points.size() * 2, 9);
    // Fill A matrix with DLT equations
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(A, Eigen::ComputeFullV);
    VectorXd h = svd.matrixV().col(8);
    return Eigen::Map<Matrix3d>(h.data()).transpose();
}
```

**2. Image Processing: OpenCV (cv::Mat)**

```cpp
#include <opencv2/opencv.hpp>

// Gaussian blur
cv::Mat blurred;
cv::GaussianBlur(input, blurred, cv::Size(0, 0), 1.5);

// Warp perspective
cv::Mat warped;
cv::warpPerspective(src, warped, homography, cv::Size(width, height));
```

**3. Parallelism: Intel TBB or C++17 Parallel Algorithms**

```cpp
#include <execution>
#include <algorithm>

// Parallel for_each
std::for_each(std::execution::par_unseq,
              features.begin(), features.end(),
              [&](const Feature& f) {
                  // Process feature
              });
```

**4. Random Number Generation: STL <random>**

```cpp
#include <random>

std::mt19937_64 rng(12345);  // Fixed seed like Rust version
std::uniform_int_distribution<int> dist(-16, 16);
```

### Implementation Structure

```cpp
// Core data structures
struct KeyPoint {
    uint32_t x, y;
};

using Descriptor = std::array<uint8_t, 32>;  // 256 bits

struct Feature {
    KeyPoint kp;
    Descriptor desc;
};

struct Match {
    size_t idx1, idx2;
    float distance;
};

// Feature detector class
class FASTBriefDetector {
public:
    std::vector<Feature> detect(const cv::Mat& grayImage);

private:
    std::vector<std::pair<int, int>> briefPairs_;
    static constexpr int FAST_THRESHOLD = 15;
    static constexpr float NMS_RADIUS = 15.0f;
    static constexpr int PATCH_SIZE = 32;
    static constexpr int DESC_SIZE = 256;

    void generateBriefPairs();
    std::vector<KeyPoint> fastDetect(const cv::Mat& img);
    void nonMaximalSuppression(std::vector<KeyPoint>& keypoints);
    Descriptor computeBrief(const cv::Mat& img, const KeyPoint& kp);
};

// RANSAC homography estimator
class RansacHomography {
public:
    struct Result {
        Eigen::Matrix3d H;
        std::vector<Match> inliers;
    };

    std::optional<Result> estimate(const std::vector<Match>& matches,
                                   const std::vector<KeyPoint>& kp1,
                                   const std::vector<KeyPoint>& kp2);

private:
    static constexpr int ITERATIONS = 2500;
    static constexpr double INLIER_THRESHOLD = 5.0;
    static constexpr int MIN_INLIERS = 15;
};

// Stitcher with seam finding
class PanoramaStitcher {
public:
    cv::Mat stitch(const std::vector<cv::Mat>& images,
                   const std::vector<Eigen::Matrix3d>& homographies);

private:
    struct SeamInfo {
        enum Orientation { VERTICAL, HORIZONTAL };
        Orientation orient;
        std::vector<int> coords;
        bool newImageDominant;
    };

    std::optional<SeamInfo> findAdaptiveSeam(...);
    std::vector<int> findVerticalSeamDP(...);
    std::vector<int> findHorizontalSeamDP(...);
    void blendImages(cv::Mat& panorama, const cv::Mat& newImage,
                     const SeamInfo& seam);
};
```

---

## OpenCV Building Blocks Evaluation

### Available OpenCV Functions and Their Equivalents

| Rust Implementation       | OpenCV Equivalent                           | Function    | Notes                                  |
| ------------------------- | ------------------------------------------- | ----------- | -------------------------------------- |
| `gaussian_blur_f32`       | `cv::GaussianBlur`                          | ✓ Available | Native implementation, supports CV_32F |
| `corners_fast9`           | `cv::FastFeatureDetector`                   | ✓ Available | Use `TYPE_9_16` for 9/16 pixel test    |
| `non_maximal_suppression` | Built into FAST                             | ⚠️ Partial  | OpenCV's FAST includes NMS             |
| BRIEF descriptor          | `cv::xfeatures2d::BriefDescriptorExtractor` | ✓ Available | In contrib module                      |
| Hamming distance          | `cv::NORM_HAMMING`                          | ✓ Available | Optimized POPCNT instruction           |
| `match_features`          | `cv::BFMatcher`                             | ✓ Available | Supports ratio test natively           |
| `compute_homography`      | `cv::findHomography`                        | ✓ Available | Built-in RANSAC support                |
| `find_homography_ransac`  | `cv::findHomography` with `RANSAC`          | ✓ Available | Single function call                   |
| `warpPerspective`         | `cv::warpPerspective`                       | ✓ Available | Supports various interpolation modes   |
| `get_interpolated_pixel`  | `cv::remap` or `cv::warpPerspective`        | ✓ Available | Prefer warp for whole image            |
| Seam finding (DP)         | Not directly available                      | ✗ Custom    | OpenCV has graph cut but not DP seam   |
| Multi-band blending       | `cv::detail::MultiBandBlender`              | ✓ Available | Advanced blending in stitching module  |
| Exposure compensation     | `cv::detail::ExposureCompensator`           | ✓ Available | For exposure differences               |

### Recommended OpenCV Pipeline

```cpp
#include <opencv2/opencv.hpp>
#include <opencv2/stitching.hpp>
#include <opencv2/xfeatures2d.hpp>

class OpenCVPanoramaStitcher {
public:
    cv::Mat stitch(const std::vector<cv::Mat>& images) {
        // Method 1: Use OpenCV's built-in Stitcher (simpler but less control)
        cv::Ptr<cv::Stitcher> stitcher = cv::Stitcher::create();
        cv::Mat result;
        stitcher->stitch(images, result);
        return result;

        // Method 2: Custom pipeline with OpenCV primitives
        // (More control over individual steps)
    }

    cv::Mat customStitch(const std::vector<cv::Mat>& images) {
        // 1. Detect FAST + BRIEF
        cv::Ptr<cv::FastFeatureDetector> fast =
            cv::FastFeatureDetector::create(15, true); // threshold, nonmaxSuppression
        cv::Ptr<cv::xfeatures2d::BriefDescriptorExtractor> brief =
            cv::xfeatures2d::BriefDescriptorExtractor::create(32); // bytes

        std::vector<cv::KeyPoint> keypoints;
        cv::Mat descriptors;
        fast->detect(image, keypoints);
        brief->compute(image, keypoints, descriptors);

        // 2. Match with BF + Hamming + Ratio test
        cv::BFMatcher matcher(cv::NORM_HAMMING);
        std::vector<std::vector<cv::DMatch>> knnMatches;
        matcher.knnMatch(desc1, desc2, knnMatches, 2);

        std::vector<cv::DMatch> goodMatches;
        for (auto& m : knnMatches) {
            if (m[0].distance < 0.8 * m[1].distance) {
                goodMatches.push_back(m[0]);
            }
        }

        // 3. Find homography with RANSAC
        std::vector<cv::Point2f> srcPoints, dstPoints;
        // ... extract points from matches
        cv::Mat H = cv::findHomography(srcPoints, dstPoints, cv::RANSAC, 5.0);

        // 4. Warp and blend
        cv::Mat warped;
        cv::warpPerspective(src, warped, H, cv::Size(width, height));

        // 5. Custom seam finding (DP-based) remains manual
        // OpenCV's graph cut seam finder: cv::detail::GraphCutSeamFinder
    }
};
```

### OpenCV Stitcher Module Comparison

The `cv::Stitcher` class implements a complete pipeline:

- **Feature detection**: SURF (default), ORB, AKAZE
- **Matching**: FLANN or Brute-force
- **Homography estimation**: RANSAC
- **Bundle adjustment**: Optimizes all homographies jointly
- **Wave correction**: Reduces wavy artifacts
- **Exposure compensation**: Multi-band blending
- **Seam finding**: Graph cut or Voronoi
- **Blending**: Multi-band or feathering

**Advantages of OpenCV's implementation:**

- Optimized assembly/SIMD kernels
- GPU acceleration (CUDA) available
- Well-tested, handles edge cases
- Automatic camera parameter estimation

**Advantages of Custom Implementation:**

- Full control over each step
- Custom FAST9 + BRIEF for speed
- Content-adaptive feathering
- Better handling of specific use cases
- Easier to tune for specific hardware

### Performance Considerations

OpenCV implementations are generally faster due to:

1. **Intel IPP integration**: Hand-optimized image processing primitives
2. **OpenCL/CUDA support**: GPU acceleration for warp/blend operations
3. **SIMD optimization**: SSE/AVX instructions for pixel operations
4. **Memory pooling**: Reduced allocation overhead

However, the custom Rust implementation offers:

1. **Parallelism**: Rayon provides excellent data parallelism
2. **Zero-cost abstractions**: Rust's optimization produces efficient code
3. **Memory safety**: No undefined behavior or memory leaks
4. **Determinism**: Fixed random seeds for reproducibility

---

## Summary

The RapidRAW panorama stitcher implements a sophisticated multi-phase pipeline that combines:

1. **FAST9 corner detection** for efficient feature localization
2. **BRIEF binary descriptors** for fast matching with Hamming distance
3. **RANSAC geometric verification** for robust homography estimation
4. **MST-based image ordering** for optimal global alignment
5. **Dynamic programming seam finding** for minimal energy seams
6. **Content-adaptive feathering** for smooth blending

The C++ port should leverage:

- **Eigen3** for matrix operations
- **OpenCV** for image processing primitives
- **Custom implementations** for BRIEF (or use contrib module) and DP seam finding
- **Intel TBB** for parallelism

OpenCV provides excellent building blocks for most operations, particularly `cv::findHomography` with RANSAC and the Stitcher module for quick prototyping. However, the content-adaptive seam finding and blending strategy in RapidRAW provides superior quality for challenging panoramas and is worth preserving in a C++ port.
