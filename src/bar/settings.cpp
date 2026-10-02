#include "settings.h"

#include <QSettings>

namespace Settings {

namespace {

QSettings store()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "visor", "visor");
}

} // namespace

Renderer renderer()
{
    Renderer r = Renderer::Software;
    parseRenderer(store().value("renderer").toString(), &r);
    return r;
}

void setRenderer(Renderer renderer)
{
    QSettings s = store();
    s.setValue("renderer", rendererName(renderer));
    s.sync();
}

QString rendererName(Renderer renderer)
{
    return renderer == Renderer::Gpu ? QStringLiteral("gpu") : QStringLiteral("cpu");
}

bool parseRenderer(const QString &name, Renderer *out)
{
    const QString n = name.trimmed().toLower();
    if (n == "cpu" || n == "software") {
        *out = Renderer::Software;
        return true;
    }
    if (n == "gpu" || n == "d3d11" || n == "direct3d") {
        *out = Renderer::Gpu;
        return true;
    }
    return false;
}

QString filePath()
{
    return store().fileName();
}

} // namespace Settings
