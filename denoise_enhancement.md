This document provides technical instructions for implementing a **Multi-Scale Guided Filter** to eliminate chrominance noise. This approach addresses the "color blotchiness" that standard BM3D often misses by using the denoised luminance channel as a structural guide for the color channels.

## ---

**1\. Objective**

Modify the photon::Denoiser class to decouple luminance and chrominance processing. You will implement a **Guided Filter**—an edge-preserving smoothing operator—to aggressively denoise color while preserving the sharpness found in the luminance channel.

## **2\. Technical Strategy**

- **Color Space:** Shift from RGB to **YCbCr** or **CIE Lab**.
- **Luminance Denoising:** Use the existing BM3D implementation for the **Y** channel to preserve high-frequency texture.
- **Chroma Denoising:** Apply a **Multi-Scale Guided Filter** to **Cb** and **Cr** using the denoised **Y** as the "guide" image.
- **Scaling:** Use multiple iterations with increasing radii to target different frequencies of color noise (low-frequency blotches).

## ---

**3\. Implementation Steps**

### **Step 1: SIMD-Optimized Box Filter**

The Guided Filter relies heavily on mean calculations over a local window (Box Blur). Since the existing codebase uses AVX2, implement a fast sliding-window box filter.  
**Requirements:**

- Input: float\* buffer, int width, int height, int radius.
- Output: float\* blurred_buffer.
- **Optimization:** Use a horizontal pass followed by a vertical pass (separability) to achieve $O(1)$ complexity per pixel relative to the radius.

### **Step 2: The Guided Filter Kernel**

Implement the Guided Filter math where the output $q$ is a linear transform of the guide $I$ in a local window $w\_k$:

$$q\_i \= a\_k I\_i \+ b\_k$$  
.  
**Logic Flow:**

1. Compute mean_I (guide) and mean_p (noisy chroma) using the Box Filter.
2. Compute mean_II (guide squared) and mean_Ip (guide $\\times$ chroma).
3. Calculate Variance: $var\\\_I \= mean\\\_II \- mean\\\_I^2$.
4. Calculate Covariance: $cov\\\_Ip \= mean\\\_Ip \- mean\\\_I \\cdot mean\\\_p$.
5. Solve for coefficients:
   - $a \= cov\\\_Ip / (var\\\_I \+ \\epsilon)$
   - $b \= mean\\\_p \- a \\cdot mean\\\_I$
6. Compute mean_a and mean_b using the Box Filter.
7. Final Output: $Chroma\_{out} \= mean\\\_a \\cdot Guide \+ mean\\\_b$.

### **Step 3: Multi-Scale Integration**

Integrate this into Denoiser::denoiseCpu.

1. **Transform:** Convert the input channels from RGB to YCbCr.
2. **Guide Generation:** Run bm3d_process_joint on the **Y** channel only to create a clean structural guide.
3. **Iterative Smoothing:** Apply the Guided Filter to **Cb** and **Cr** at three scales:
   - **Scale 1 (Fine):** Radius \= 2, $\\epsilon \= 0.01$.
   - **Scale 2 (Medium):** Radius \= 4, $\\epsilon \= 0.04$.
   - **Scale 3 (Coarse):** Radius \= 8, $\\epsilon \= 0.1$.
4. **Reconstruct:** Convert the denoised YCbCr back to RGB before returning the QImage.

## ---

**4\. Modified Logic for Denoiser.cpp**

Update the denoiseCpu function to follow this pipeline:

| Task               | Action                                                                          |
| :----------------- | :------------------------------------------------------------------------------ |
| **Pre-process**    | Convert RGB vectors to Y, Cb, and Cr vectors.                                   |
| **Denoise Y**      | Call bm3d_process_joint passing only the Y channel.                             |
| **Denoise Chroma** | Run applyGuidedFilter (Step 2\) on Cb and Cr using the denoised Y as the guide. |
| **Post-process**   | Convert YCbCr back to RGB and clamp values to \[0, 255\].                       |

## ---

**5\. Key Constants for Tuning**

- **$\\epsilon$ (Regularization):** Controls the "edge-preservation" threshold. A higher $\\epsilon$ allows more smoothing near edges.
- **Radius:** Controls the size of the noise blotches being targeted. Increase the radius to remove larger "purple/green" clumps.
