#pragma once

#include <QString>

// App-level preferences that must be known before QML loads (so they can't
// live in the QML config). Stored in %APPDATA%\visor\visor.ini.
namespace Settings {

enum class Renderer { Software, Gpu };

Renderer renderer();
void setRenderer(Renderer renderer);

QString rendererName(Renderer renderer);
bool parseRenderer(const QString &name, Renderer *out);

QString filePath();

} // namespace Settings
