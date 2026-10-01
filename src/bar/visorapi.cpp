#include "visorapi.h"

#include "app.h"

namespace {

VisorApi::Renderer toApi(Settings::Renderer r)
{
    return r == Settings::Renderer::Gpu ? VisorApi::Gpu : VisorApi::Cpu;
}

} // namespace

VisorApi::VisorApi(QObject *parent)
    : QObject(parent)
{
    if (App *app = App::instance())
        connect(app, &App::preferredRendererChanged, this, &VisorApi::rendererChanged);
}

QString VisorApi::version() const
{
    return QStringLiteral(VISOR_VERSION);
}

QString VisorApi::configPath() const
{
    return App::instance() ? App::instance()->configPath() : QString();
}

VisorApi::Renderer VisorApi::renderer() const
{
    return App::instance() ? toApi(App::instance()->preferredRenderer()) : Cpu;
}

void VisorApi::setRenderer(Renderer renderer)
{
    if (App *app = App::instance())
        app->setPreferredRenderer(renderer == Gpu ? Settings::Renderer::Gpu : Settings::Renderer::Software);
}

VisorApi::Renderer VisorApi::activeRenderer() const
{
    return App::instance() ? toApi(App::instance()->activeRenderer()) : Cpu;
}

void VisorApi::reload()
{
    if (App *app = App::instance())
        app->reload();
}

void VisorApi::restart()
{
    if (App *app = App::instance())
        app->restart();
}

void VisorApi::quit()
{
    if (App *app = App::instance())
        app->quit();
}

void VisorApi::openConfigFolder()
{
    if (App *app = App::instance())
        app->openConfigFolder();
}
