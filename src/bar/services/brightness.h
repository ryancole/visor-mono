#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <memory>

// The built-in display's brightness, where the hardware has it (laptops;
// desktops and VMs don't, and `available` is false): the WMI classes
// Windows' own slider and brightness keys use. Those keys never reach a
// window (the OS acts on them itself), so the point of this is to show what
// they did (`levelChanged`), and to let a binding of your own change it.
//
//   Text { visible: Brightness.available; text: Math.round(Brightness.level * 100) + "%" }
//   ... Brightness.level += 0.1
class Brightness : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // 0.0 - 1.0, the same scale as Windows' slider.
    Q_PROPERTY(qreal level READ level WRITE setLevel NOTIFY levelChanged)

public:
    explicit Brightness(QObject *parent = nullptr);
    ~Brightness() override;

    bool available() const;
    qreal level() const { return m_percent < 0 ? 0 : m_percent / 100.0; }
    void setLevel(qreal level);

signals:
    void availableChanged();
    void levelChanged();

private:
    struct Impl;
    friend struct BrightnessSink;

    void applyLevel(int percent);

    std::unique_ptr<Impl> d;
    int m_percent = -1;
};
