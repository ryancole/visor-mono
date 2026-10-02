#pragma once

#include "wm/layout.h"

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace visor::wm {

// A window rule: `windowrule = float, class:^(Notepad)$, title:..., exe:...`.
// Every given field must match (regexes, searched anywhere unless anchored);
// exe is the process's file name, e.g. notepad.exe. When several rules match
// a window, the last one wins.
struct WindowRule
{
    enum Action { Float, Tile };

    Action action = Float;
    QRegularExpression windowClass;
    QRegularExpression title;
    QRegularExpression exe;

    bool matches(const QString &windowClass, const QString &title, const QString &exe) const;
};

// visor-wm's settings, read from a hyprland.conf-style file (wm.conf).
// Supported:
//   # comments (## is a literal #)
//   $name = value              variables, used as $name in later values
//   section { key = value }     the same as section:key = value
//   general:gaps_in, gaps_out, border_size, col.active_border, col.inactive_border
//   dwindle:default_split_ratio, preserve_split, force_split
//   windowrule = float|tile, class:<regex>, title:<regex>, exe:<regex>
// Unknown keys are reported, not fatal.
struct Config
{
    int gapsIn = 5;
    int gapsOut = 10;
    // 0 turns border colouring off. Windows always draws 1 px borders, so
    // other sizes only mean "on".
    int borderSize = 2;
    quint32 activeBorder = 0x33ccff;   // 0xRRGGBB
    quint32 inactiveBorder = 0x595959; // 0xRRGGBB
    DwindleLayout::Options dwindle;
    QList<WindowRule> rules;

    QStringList errors; // "line N: message"

    static Config parse(const QString &text);
    static Config load(const QString &path);
};

// Lookup order: `explicitPath`, %VISOR_WM_CONFIG%, ~/.config/visor/wm.conf,
// the in-repo config (debug builds), then config/wm.conf next to the exe.
// Empty when none exists (the defaults apply).
QString resolveConfigPath(const QString &explicitPath);

} // namespace visor::wm
