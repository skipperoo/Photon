# RawNIND denoise model (bundled with Photon)

Photon's **AI denoise** uses the *UtNet2* raw denoiser trained on the
**Raw Natural Image Noise Dataset (RawNIND)**.

- Authors: Benoit Brummer and Christophe De Vleeschouwer (UCLouvain)
- Paper: "Learning Joint Denoising, Demosaicing, and Compression from the Raw
  Natural Image Noise Dataset", arXiv:2501.08924
- Source code: <https://github.com/trougnouf/rawnind_jddc>
- Distribution used: darktable-ai `rawdenoise-nind.dtmodel`
  (release-5.6.0, file `model_bayer.onnx`,
  SHA-256 `da27509dab6a2915da67e988acd86cf71f9d5bbc8d1aa0ed32933578a887b901`),
  <https://github.com/darktable-org/darktable-ai/tree/master/models/rawdenoise-nind>
- Training data: RawNIND, real noisy/clean photo pairs published on Wikimedia
  Commons and the UCLouvain Dataverse (per-image CC BY 4.0 / CC0).

## Licence

The model weights are licensed under the **GNU General Public License,
version 3** (see `RAWNIND-LICENSE-GPL-3.0.txt`). Photon itself is licensed
under the GNU Affero General Public License, version 3; GPL-3.0 section 13
permits combining the two.

## Modification

The files `rawnind_bayer.ncnn.param` and `rawnind_bayer.ncnn.bin` are a format
conversion of `model_bayer.onnx` to the ncnn format, produced with pnnx
(release 20261020) by `cmake/AiDenoiseModel.cmake`
(`pnnx rawnind_bayer.onnx inputshape=[1,4,512,512]`). The conversion stores the
weights as 16-bit floats; the network itself is unchanged. The corresponding
source (the original ONNX model) is available from the darktable-ai link above,
and the conversion script is part of Photon's source.
