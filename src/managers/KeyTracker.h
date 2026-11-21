#pragma once

#include <QObject>
#include <QGuiApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QtQml/qqml.h>

class KeyTracker : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool altPressed READ altPressed NOTIFY altPressedChanged)

public:
    explicit KeyTracker(QObject *parent = nullptr)
        : QObject(parent)
    {
        if (qApp)
            qApp->installEventFilter(this);
    }

    bool altPressed() const { return m_keyAlt || m_pointerAlt; }

signals:
    void altPressedChanged();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Alt) {
                // Releasing the key also ends the pointer's Alt (the moves
                // made while it was held carried it).
                if (event->type() == QEvent::KeyRelease) update(m_pointerAlt, false);
                update(m_keyAlt, event->type() == QEvent::KeyPress);
            }
            break;
        // Alt can also arrive only as a modifier of the pointer events, with
        // no key event (the remote toolbox's Alt): held while the press or
        // drag carries it, released with the button.
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
            update(m_pointerAlt, (static_cast<QMouseEvent *>(event)->modifiers() &
                                  Qt::AltModifier) != 0);
            break;
        case QEvent::MouseButtonRelease:
            update(m_pointerAlt, false);
            break;
        default:
            break;
        }
        return QObject::eventFilter(obj, event);
    }

private:
    void update(bool &flag, bool value)
    {
        if (flag == value) return;
        const bool before = altPressed();
        flag = value;
        if (altPressed() != before) emit altPressedChanged();
    }

    bool m_keyAlt = false;
    bool m_pointerAlt = false;
};
