#pragma once

#include <QQuickItem>
#include <QSGNode>
#include <QSGSimpleTextureNode>
#include <QQuickWindow>
#include <memory>
#include "RawEngine.h"

class RawViewport : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(float exposure READ exposure WRITE setExposure NOTIFY exposureChanged)
    QML_ELEMENT

public:
    explicit RawViewport(QQuickItem *parent = nullptr);

    QString source() const { return m_engine.source(); }
    void setSource(const QString &source);

    float exposure() const { return m_engine.exposure(); }
    void setExposure(float ev);

signals:
    void sourceChanged();
    void exposureChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private slots:
    void onImageLoaded();

private:
    RawEngine m_engine;
    bool m_imageDirty = false;
};
