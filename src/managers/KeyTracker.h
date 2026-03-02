#pragma once

#include <QObject>
#include <QGuiApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QtQml/qqml.h>

class KeyTracker : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool altPressed READ altPressed NOTIFY altPressedChanged)

public:
    explicit KeyTracker(QObject *parent = nullptr)
        : QObject(parent), m_altPressed(false)
    {
        if (qApp)
            qApp->installEventFilter(this);
    }

    bool altPressed() const { return m_altPressed; }

signals:
    void altPressedChanged();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Alt && !m_altPressed) {
                m_altPressed = true;
                emit altPressedChanged();
            }
        } else if (event->type() == QEvent::KeyRelease) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Alt && m_altPressed) {
                m_altPressed = false;
                emit altPressedChanged();
            }
        }
        return QObject::eventFilter(obj, event);
    }

private:
    bool m_altPressed;
};
