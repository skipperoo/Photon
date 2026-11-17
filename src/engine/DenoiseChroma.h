#pragma once

#include <QImage>
#include <atomic>

namespace photon {

// Colour-only denoise (see Denoiser::denoiseChroma). Declared separately so
// translation units that include ncnn (which ships its own Vulkan
// declarations) do not pull in the system Vulkan headers via Denoiser.h.
// `exposureGain`: see DenoiseParams::exposureGain.
QImage denoiseChromaOnly(const QImage& input, float strength,
                         std::atomic<bool>* abort, float exposureGain);

}  // namespace photon
