#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// Wall-clock time that updates exactly on second/minute/hour boundaries. The
// timer sleeps until the next boundary rather than ticking, so a minute clock
// wakes the process once a minute. `time` is the time as Windows' taskbar
// writes it: the short time format from Settings > Time & language >
// Language & region (the long one, with seconds, at Seconds precision), so
// 12- or 24-hour follows Windows, re-read on every tick.
//
//   SystemClock { id: clock; precision: SystemClock.Minutes }
//   Text { text: Qt.formatDateTime(clock.date, "ddd d MMM") + "  " + clock.time }
class SystemClock : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Precision precision READ precision WRITE setPrecision NOTIFY precisionChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QDateTime date READ date NOTIFY dateChanged)
    Q_PROPERTY(QString time READ time NOTIFY timeChanged)
    Q_PROPERTY(int hours READ hours NOTIFY dateChanged)
    Q_PROPERTY(int minutes READ minutes NOTIFY dateChanged)
    Q_PROPERTY(int seconds READ seconds NOTIFY dateChanged)

public:
    enum Precision { Seconds, Minutes, Hours };
    Q_ENUM(Precision)

    explicit SystemClock(QObject *parent = nullptr);

    Precision precision() const { return m_precision; }
    void setPrecision(Precision precision);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    QDateTime date() const { return m_date; }
    QString time() const { return m_time; }
    int hours() const { return m_date.time().hour(); }
    int minutes() const { return m_date.time().minute(); }
    int seconds() const { return m_date.time().second(); }

signals:
    void precisionChanged();
    void enabledChanged();
    void dateChanged();
    void timeChanged();

private:
    void tick();

    QTimer m_timer;
    QDateTime m_date;
    QString m_time;
    Precision m_precision = Seconds;
    bool m_enabled = true;
};
