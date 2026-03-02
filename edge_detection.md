# Technical Specification: Selective Sharpening Masking Pipeline

This document outlines the industry-standard "Masking" pipeline used in high-end RAW processors (e.g., Adobe Lightroom) to apply sharpening selectively to edges while protecting flat areas and noise.

---

## 1. Objective

Generate a grayscale **Edge Mask** where high-contrast edges are white (apply 100% sharpening) and flat surfaces/noise are black (apply 0% sharpening).

## 2. The Pipeline Steps

### Step 1: Luminance Extraction

Convert the RGB image (or the current working buffer) to a single-channel **Luminance (Y)** map.

- **Formula:** $Y = 0.299R + 0.587G + 0.114B$
- _Note: Perform this on linear data if possible to ensure edge detection is physically accurate._

### Step 2: Pre-Blur (Low-pass Filter)

Apply a very slight Gaussian blur to the Luminance map.

- **Radius:** $0.5$ to $1.0$ pixels.
- **Purpose:** To prevent sensor grain or high-frequency noise from being detected as "edges." This ensures the mask targets actual scene structures rather than noise.

### Step 3: Gradient Detection (Sobel or Scharr)

Run a horizontal ($G_x$) and vertical ($G_y$) derivative filter. The **Scharr operator** is preferred over Sobel for better rotational symmetry.

**3x3 Scharr Kernels:**
$$G_x = \begin{bmatrix} -3 & 0 & 3 \\ -10 & 0 & 10 \\ -3 & 0 & 3 \end{bmatrix}, \quad G_y = \begin{bmatrix} -3 & -10 & -3 \\ 0 & 0 & 0 \\ 3 & 10 & 3 \end{bmatrix}$$

**Calculate Magnitude:**
$$G = \sqrt{G_x^2 + G_y^2}$$

### Step 4: Selective Thresholding

This step converts the raw gradient map into a usable mask.

1. **Threshold ($T$):** Values in $G$ below threshold $T$ are set to $0$.
2. **Scaling:** Rescale the remaining values to the $[0.0, 1.0]$ range.

- **Logic:** `mask = clamp((G - threshold) * gain, 0.0, 1.0)`
- Increasing the threshold effectively "tightens" the mask around only the most prominent edges.

### Step 5: Post-Blur (Softening)

Apply a Gaussian blur to the resulting mask.

- **Radius:** Match the sharpening radius (usually $1.0$ to $2.0$).
- **Purpose:** Softens the transitions. Without this, the boundary between sharpened and unsharpened pixels will create "crunchy" or "jagged" aliasing artifacts.

---

## 3. Application Logic

Once the `mask` is generated, apply the sharpening effect as follows:

$$Output = Original + (SharpeningDelta \times mask)$$

Where `SharpeningDelta` is the high-frequency detail extracted via Unsharp Masking or High-Pass filtering.

## 4. Why This Works

Unlike a **Canny Detector**, which produces binary (1 or 0) single-pixel lines, the **Sobel/Scharr** approach provides a **grayscale gradient**. This allows for a "natural fall-off," where the center of an edge is sharpened intensely, but the sharpening strength tapers off smoothly as you move toward flat textures.
