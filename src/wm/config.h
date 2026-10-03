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

// A key binding: `bind = SUPER SHIFT, left, swapwindow, l`.
//   bind[flags] = MODS, key, dispatcher[, argument]
//   bindd[flags] = MODS, key, description, dispatcher[, argument]
// Flags: e repeats while the key is held; r fires when the key is released,
// and only if nothing else was pressed in between (`bindr = SUPER, SUPER_L,
// ...` is a bare Win press); l is accepted and ignored. Modifiers: SUPER (or
// WIN), SHIFT, CTRL, ALT, in any order and separated any way, or none
// (`bindel = , XF86AudioRaiseVolume, ...`). Keys use Hyprland's (xkb) names:
// a-z, 0-9, F1-F24, Return, space, Tab, Escape, left, right, up, down, minus,
// equal, comma, period, slash, SUPER_L, SUPER_R, XF86AudioRaiseVolume, ...
// Dispatchers (as in Hyprland):
//   exec <command line>    killactive          togglefloating
//   visor <name>           (ours: tells Visor to run its `name` command, e.g.
//                           launcher, menu, keys, run)
//   fullscreen [0|1]       (0: whole monitor, over the bar; 1: maximise)
//   movefocus l|r|u|d      swapwindow l|r|u|d  togglesplit
//   resizeactive <dx> <dy> (pixels; grows/shrinks the window's split)
// Desktops (Windows 11's virtual desktops; Hyprland calls them workspaces):
//   workspace new|e+1|e-1|N            create / next / previous / Nth
//   movetoworkspace e+1|e-1|N|new      move the window there and follow it
//   movetoworkspacesilent ...          move the window, stay here
//   closeworkspace                     its windows go to the desktop on the left
struct Binding
{
    quint32 modifiers = 0; // MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN
    quint32 key = 0;       // virtual-key code
    bool repeat = false;
    bool release = false;
    QString dispatcher;
    QString argument;
    QString description;
    QString name;          // e.g. "SUPER+SHIFT+left", for the log
    // Bindings separated by a blank or comment line in the config are in
    // different groups; the cheat sheet in Visor shows them that way.
    int group = 0;
};

// visor-wm's settings, read from a hyprland.conf-style file (wm.conf).
// Supported:
//   # comments (## is a literal #)
//   $name = value              variables, used as $name in later values
//   source = file               reads another file here (~ and %VAR% expand;
//                               relative to this file; skipped if missing)
//   section { key = value }     the same as section:key = value
//   general:gaps_in, gaps_out, border_size, col.active_border, col.inactive_border
//     (colours as rgb(rrggbb), rgba(rrggbbaa), 0xaarrggbb, or accent)
//   dwindle:default_split_ratio, preserve_split, force_split, split_width_multiplier
//   windowrule = float|tile, class:<regex>, title:<regex>, exe:<regex>
//   bind, bindd, binde, ... (see Binding)
// Unknown keys are reported, not fatal.
struct Config
{
    int gapsIn = 5;
    int gapsOut = 10;
    // 0 turns border colouring off. Windows always draws 1 px borders, so
    // other sizes only mean "on".
    int borderSize = 2;
    // A border colour is 0xRRGGBB, or `accent`: Windows' accent colour,
    // followed as it changes (the default for the focused window, as
    // Windows' own "show accent colour on borders" draws it).
    struct BorderColor
    {
        bool accent = false;
        quint32 rgb = 0x595959;
    };
    BorderColor activeBorder{true, 0x0078d4};
    BorderColor inactiveBorder{false, 0x595959};
    DwindleLayout::Options dwindle;
    QList<WindowRule> rules;
    QList<Binding> bindings;

    QStringList errors;  // "line N: message"
    QStringList sources; // every `source` file, present or not, for watching

    // baseDir resolves relative `source` paths.
    static Config parse(const QString &text, const QString &baseDir = {});
    static Config load(const QString &path);
};

// Lookup order: `explicitPath`, %VISOR_WM_CONFIG%, ~/.config/visor/wm.conf,
// the in-repo config (debug builds), then config/wm.conf next to the exe.
// Empty when none exists (the defaults apply).
QString resolveConfigPath(const QString &explicitPath);

} // namespace visor::wm
