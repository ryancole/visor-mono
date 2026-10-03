#include "services/systemclock.h"

#include <windows.h>

namespace {

QDateTime truncate(const QDateTime &dt, SystemClock::Precision precision)
{
    const QTime t = dt.time();
    switch (precision) {
    case SystemClock::Seconds: return QDateTime(dt.date(), QTime(t.hour(), t.minute(), t.second()));
    case SystemClock::Minutes: return QDateTime(dt.date(), QTime(t.hour(), t.minute()));
    case SystemClock::Hours: return QDateTime(dt.date(), QTime(t.hour(), 0));
    }
    return dt;
}

qint64 periodMs(SystemClock::Precision precision)
{
    switch (precision) {
    case SystemClock::Seconds: return 1000;
    case SystemClock::Minutes: return 60 * 1000;
    case SystemClock::Hours: return 60 * 60 * 1000;
    }
    return 1000;
}

// The time in the user's Windows format, as the taskbar shows it: the short
// format, or the long one when seconds are wanted.
QString windowsTime(const QDateTime &dt, SystemClock::Precision precision)
{
    wchar_t format[80] = {};
    const bool longFormat = precision == SystemClock::Seconds;
    if (!longFormat && !GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTTIME, format, ARRAYSIZE(format)))
        return dt.toString(QStringLiteral("HH:mm"));

    const QTime t = dt.time();
    SYSTEMTIME st = {};
    st.wYear = WORD(dt.date().year());
    st.wMonth = WORD(dt.date().month());
    st.wDay = WORD(dt.date().day());
    st.wHour = WORD(t.hour());
    st.wMinute = WORD(t.minute());
    st.wSecond = WORD(t.second());

    wchar_t text[80] = {};
    if (!GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, longFormat ? nullptr : format, text, ARRAYSIZE(text)))
        return dt.toString(QStringLiteral("HH:mm"));
    return QString::fromWCharArray(text);
}

} // namespace

SystemClock::SystemClock(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &SystemClock::tick);
    tick();
}

void SystemClock::setPrecision(Precision precision)
{
    if (m_precision == precision)
        return;
    m_precision = precision;
    emit precisionChanged();
    tick();
}

void SystemClock::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
    if (m_enabled)
        tick();
    else
        m_timer.stop();
}

void SystemClock::tick()
{
    if (!m_enabled)
        return;

    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime truncated = truncate(now, m_precision);
    if (truncated != m_date) {
        m_date = truncated;
        emit dateChanged();
    }
    // Every tick, not only on a new date, so a format changed in Settings
    // shows by the next boundary.
    const QString time = windowsTime(truncated, m_precision);
    if (time != m_time) {
        m_time = time;
        emit timeChanged();
    }

    // Re-arm for the next boundary, recomputed from the real clock each time
    // so drift, sleep/resume and clock changes self-correct. +5ms lands us
    // safely past the boundary.
    const qint64 untilNext = periodMs(m_precision) - truncated.msecsTo(now);
    m_timer.start(int(untilNext + 5));
}
