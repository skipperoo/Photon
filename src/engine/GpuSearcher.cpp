#include "GpuSearcher.h"

#include <private/qshader_p.h>

#include <QCoreApplication>
#include <QFile>
#include <QMatrix4x4>

namespace photon {

GpuSearcher::GpuSearcher(QRhi* rhi, QObject* parent)
    : QObject(parent), m_rhi(rhi) {}

GpuSearcher::~GpuSearcher() {
  // Resources are smart pointers, they will be destroyed now
}

void GpuSearcher::initResources(int w, int h) {
  if (m_lumaTex && m_lumaTex->pixelSize() == QSize(w, h)) return;

  m_lumaTex.reset(m_rhi->newTexture(QRhiTexture::R32F, QSize(w, h), 1,
                                    QRhiTexture::UsedAsTransferSource));
  m_lumaTex->create();

  m_resultTex.reset(m_rhi->newTexture(
      QRhiTexture::RGBA32F, QSize(w, h), 1,
      QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
  m_resultTex->create();

  QRhiTextureRenderTargetDescription rtDesc;
  rtDesc.setColorAttachments({{m_resultTex.get()}});
  m_rt.reset(m_rhi->newTextureRenderTarget(rtDesc));
  m_rp.reset(m_rt->newCompatibleRenderPassDescriptor());
  m_rt->setRenderPassDescriptor(m_rp.get());
  m_rt->create();

  m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                64 + 16));
  m_ubuf->create();

  m_srb.reset(m_rhi->newShaderResourceBindings());

  // Create and store sampler
  m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                    QRhiSampler::None, QRhiSampler::ClampToEdge,
                                    QRhiSampler::ClampToEdge));
  m_sampler->create();

  using Binding = QRhiShaderResourceBinding;
  std::vector<Binding> bindings;
  bindings.push_back(Binding::uniformBuffer(
      0, Binding::VertexStage | Binding::FragmentStage, m_ubuf.get()));
  bindings.push_back(Binding::sampledTexture(1, Binding::FragmentStage,
                                             m_lumaTex.get(), m_sampler.get()));

  m_srb->setBindings(bindings.begin(), bindings.end());
  m_srb->create();

  m_pipeline.reset(m_rhi->newGraphicsPipeline());
  m_pipeline->setShaderResourceBindings(m_srb.get());
  m_pipeline->setRenderPassDescriptor(m_rp.get());

  // Load baked shaders from resources
  QFile vertFile(":/Main/shaders/PatchSearch.vert.qsb");
  if (vertFile.open(QIODevice::ReadOnly)) {
    QShader vert = QShader::fromSerialized(vertFile.readAll());
    QShader frag = QShader::fromSerialized(
        QFile(":/Main/shaders/PatchSearch.frag.qsb").readAll());
    m_pipeline->setShaderStages(
        {{QRhiShaderStage::Vertex, vert}, {QRhiShaderStage::Fragment, frag}});
  }

  // Minimal pipeline state
  m_pipeline->setDepthTest(false);
  m_pipeline->setDepthWrite(false);
  m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);

  m_pipeline->create();
}

std::vector<GpuSearcher::SearchResult> GpuSearcher::runSearch(
    const float* luma, int width, int height, int searchWindow) {
  if (!m_rhi) return {};
  initResources(width, height);

  QRhiResourceUpdateBatch* u = m_rhi->nextResourceUpdateBatch();

  // Upload raw float data
  QRhiTextureSubresourceUploadDescription subDesc(
      QByteArray((const char*)luma, width * height * sizeof(float)));
  QRhiTextureUploadEntry entry(0, 0, subDesc);

  QRhiTextureUploadDescription desc;
  desc.setEntries({entry});
  u->uploadTexture(m_lumaTex.get(), desc);

  // Update Uniforms
  QMatrix4x4 matrix;  // Identity for full screen quad
  u->updateDynamicBuffer(m_ubuf.get(), 0, 64, matrix.constData());
  float size[2] = {(float)width, (float)height};
  u->updateDynamicBuffer(m_ubuf.get(), 64, 8, size);
  u->updateDynamicBuffer(m_ubuf.get(), 72, 4, &searchWindow);

  QRhiCommandBuffer* cb;
  m_rhi->beginOffscreenFrame(&cb);
  cb->beginPass(m_rt.get(), Qt::transparent, {1.0f, 0}, u);
  cb->setGraphicsPipeline(m_pipeline.get());
  cb->setViewport({0, 0, (float)width, (float)height});
  cb->setShaderResources();
  cb->draw(4);
  cb->endPass();

  // Readback result
  QRhiReadbackResult readback;
  bool completed = false;
  readback.completed = [&completed]() { completed = true; };

  u = m_rhi->nextResourceUpdateBatch();
  u->readBackTexture({m_resultTex.get()}, &readback);
  cb->resourceUpdate(u);

  m_rhi->endOffscreenFrame();

  // Wait for the result
  m_rhi->finish();

  if (readback.data.isEmpty()) return {};

  const float* data = reinterpret_cast<const float*>(readback.data.constData());
  std::vector<SearchResult> results;
  results.reserve(width * height);

  for (int i = 0; i < width * height; ++i) {
    // Decode from RGBA (0-1 range)
    float dx = data[i * 4] * 255.0f - 128.0f;
    float dy = data[i * 4 + 1] * 255.0f - 128.0f;
    float ssd = data[i * 4 + 2];
    results.push_back({(int)dx, (int)dy, ssd});
  }

  return results;
}

}  // namespace photon
