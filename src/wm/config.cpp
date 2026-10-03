#include "wm/config.h"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <windows.h>

#include <algorithm>

namespace visor::wm {

namespace {

bool parseBool(const QString &value, bool *out)
{
    const QString v = value.toLower();
    if (v == QLatin1String("true") || v == QLatin1String("yes") || v == QLatin1String("on") || v == QLatin1String("1")) {
        *out = true;
        return true;
    }
    if (v == QLatin1String("false") || v == QLatin1String("no") || v == QLatin1String("off") || v == QLatin1String("0")) {
        *out = false;
        return true;
    }
    return false;
}

// Hyprland allows CSS-style "5" or "5,10,5,10" (top, right, bottom, left)
// for gaps; we use one value for every side, the first.
bool parseInt(const QString &value, int *out)
{
    bool ok = false;
    const int n = value.section(QLatin1Char(','), 0, 0).trimmed().toInt(&ok);
    if (ok)
        *out = n;
    return ok;
}

QString substitute(QString value, const QHash<QString, QString> &variables)
{
    // Longest names first, so $term doesn't eat the start of $terminal.
    QStringList names = variables.keys();
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    for (const QString &name : names)
        value.replace(QLatin1Char('$') + name, variables.value(name));
    return value;
}

bool parseRule(const QString &value, WindowRule *rule, QString *error)
{
    const QStringList parts = value.split(QLatin1Char(','));
    const QString action = parts.value(0).trimmed().toLower();
    if (action == QLatin1String("float")) {
        rule->action = WindowRule::Float;
    } else if (action == QLatin1String("tile")) {
        rule->action = WindowRule::Tile;
    } else {
        *error = QStringLiteral("unsupported window rule \"%1\" (supported: float, tile)").arg(action);
        return false;
    }

    bool any = false;
    for (qsizetype i = 1; i < parts.size(); ++i) {
        const QString part = parts[i].trimmed();
        const qsizetype colon = part.indexOf(QLatin1Char(':'));
        const QString field = part.left(colon).trimmed().toLower();
        const QString pattern = part.mid(colon + 1).trimmed();
        QRegularExpression *target = nullptr;
        if (field == QLatin1String("class"))
            target = &rule->windowClass;
        else if (field == QLatin1String("title"))
            target = &rule->title;
        else if (field == QLatin1String("exe"))
            target = &rule->exe;
        if (colon < 0 || !target) {
            *error = QStringLiteral("unknown window rule field \"%1\" (supported: class, title, exe)").arg(part);
            return false;
        }
        const QRegularExpression re(pattern, field == QLatin1String("exe")
                                                 ? QRegularExpression::CaseInsensitiveOption
                                                 : QRegularExpression::NoPatternOption);
        if (!re.isValid()) {
            *error = QStringLiteral("bad regex \"%1\": %2").arg(pattern, re.errorString());
            return false;
        }
        *target = re;
        any = true;
    }
    if (!any) {
        *error = QStringLiteral("window rule matches nothing (give class:, title: or exe:)");
        return false;
    }
    return true;
}

// Hyprland's modifier names; like Hyprland, matched as substrings so
// "SUPER SHIFT", "SUPER_SHIFT" and "SUPERSHIFT" all work.
bool parseModifiers(const QString &text, quint32 *out)
{
    QString rest = text.toUpper();
    quint32 mods = 0;
    const struct
    {
        const char *name;
        quint32 flag;
    } names[] = {
        {"SUPER", MOD_WIN},     {"WIN", MOD_WIN},      {"MOD4", MOD_WIN}, {"META", MOD_WIN}, {"SHIFT", MOD_SHIFT},
        {"CONTROL", MOD_CONTROL}, {"CTRL", MOD_CONTROL}, {"ALT", MOD_ALT}, {"MOD1", MOD_ALT},
    };
    for (const auto &n : names) {
        const QString name = QString::fromLatin1(n.name);
        if (rest.contains(name)) {
            mods |= n.flag;
            rest.remove(name);
        }
    }
    // Whatever is left must be separators.
    for (QChar ch : std::as_const(rest)) {
        if (!ch.isSpace() && ch != QLatin1Char('_') && ch != QLatin1Char('+'))
            return false;
    }
    *out = mods;
    return true;
}

bool parseKey(const QString &text, quint32 *out)
{
    const QString key = text.trimmed().toLower();
    if (key.size() == 1) {
        const QChar ch = key[0];
        if ((ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))) {
            *out = ch.toUpper().unicode();
            return true;
        }
    }
    if (key.size() >= 2 && key[0] == QLatin1Char('f')) {
        bool ok = false;
        const int n = key.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24) {
            *out = VK_F1 + n - 1;
            return true;
        }
    }
    static const QHash<QString, quint32> names{
        {QStringLiteral("return"), VK_RETURN},      {QStringLiteral("enter"), VK_RETURN},
        {QStringLiteral("space"), VK_SPACE},        {QStringLiteral("tab"), VK_TAB},
        {QStringLiteral("escape"), VK_ESCAPE},      {QStringLiteral("backspace"), VK_BACK},
        {QStringLiteral("delete"), VK_DELETE},      {QStringLiteral("insert"), VK_INSERT},
        {QStringLiteral("home"), VK_HOME},          {QStringLiteral("end"), VK_END},
        {QStringLiteral("prior"), VK_PRIOR},        {QStringLiteral("page_up"), VK_PRIOR},
        {QStringLiteral("next"), VK_NEXT},          {QStringLiteral("page_down"), VK_NEXT},
        {QStringLiteral("left"), VK_LEFT},          {QStringLiteral("right"), VK_RIGHT},
        {QStringLiteral("up"), VK_UP},              {QStringLiteral("down"), VK_DOWN},
        {QStringLiteral("print"), VK_SNAPSHOT},     {QStringLiteral("minus"), VK_OEM_MINUS},
        {QStringLiteral("equal"), VK_OEM_PLUS},     {QStringLiteral("comma"), VK_OEM_COMMA},
        {QStringLiteral("period"), VK_OEM_PERIOD},  {QStringLiteral("slash"), VK_OEM_2},
        {QStringLiteral("grave"), VK_OEM_3},        {QStringLiteral("semicolon"), VK_OEM_1},
        {QStringLiteral("apostrophe"), VK_OEM_7},   {QStringLiteral("bracketleft"), VK_OEM_4},
        {QStringLiteral("bracketright"), VK_OEM_6}, {QStringLiteral("backslash"), VK_OEM_5},
        {QStringLiteral("super_l"), VK_LWIN},       {QStringLiteral("super_r"), VK_RWIN},
        {QStringLiteral("super"), VK_LWIN},
        // The media keys, by their xkb names (as in Omarchy's bindings).
        // Windows has no key code for brightness: the OS acts on those
        // keys itself, before any app sees them.
        {QStringLiteral("xf86audioraisevolume"), VK_VOLUME_UP},
        {QStringLiteral("xf86audiolowervolume"), VK_VOLUME_DOWN},
        {QStringLiteral("xf86audiomute"), VK_VOLUME_MUTE},
        {QStringLiteral("xf86audioplay"), VK_MEDIA_PLAY_PAUSE},
        {QStringLiteral("xf86audionext"), VK_MEDIA_NEXT_TRACK},
        {QStringLiteral("xf86audioprev"), VK_MEDIA_PREV_TRACK},
        {QStringLiteral("xf86audiostop"), VK_MEDIA_STOP},
    };
    const auto it = names.constFind(key);
    if (it == names.cend())
        return false;
    *out = *it;
    return true;
}

bool isDirection(const QString &arg)
{
    return arg == QLatin1String("l") || arg == QLatin1String("r") || arg == QLatin1String("u")
           || arg == QLatin1String("d");
}

bool isInt(const QString &s)
{
    bool ok = false;
    s.toInt(&ok);
    return ok;
}

// `flags` is what follows "bind" in the key: d (description), e (repeat),
// r (release), l (Hyprland's "works on the lock screen": accepted, and
// meaningless here, since Windows owns its lock screen).
bool parseBinding(const QString &flags, const QString &value, Binding *binding, QString *error)
{
    for (QChar f : flags) {
        if (f != QLatin1Char('d') && f != QLatin1Char('e') && f != QLatin1Char('r') && f != QLatin1Char('l')) {
            *error = QStringLiteral("unsupported bind flag '%1' (supported: d, e, r, l)").arg(f);
            return false;
        }
    }
    const bool described = flags.contains(QLatin1Char('d'));
    binding->repeat = flags.contains(QLatin1Char('e'));
    binding->release = flags.contains(QLatin1Char('r'));

    // The argument is everything after the dispatcher, commas included
    // (exec command lines may contain them).
    const qsizetype fixed = described ? 4 : 3;
    QStringList parts = value.split(QLatin1Char(','));
    if (parts.size() < fixed) {
        *error = described ? QStringLiteral("expected bindd = MODS, key, description, dispatcher[, arg]")
                           : QStringLiteral("expected bind = MODS, key, dispatcher[, arg]");
        return false;
    }
    const QString argument = parts.mid(fixed).join(QLatin1Char(',')).trimmed();
    parts = parts.mid(0, fixed);
    for (QString &p : parts)
        p = p.trimmed();

    if (!parseModifiers(parts[0], &binding->modifiers)) {
        *error = QStringLiteral("unknown modifiers \"%1\"").arg(parts[0]);
        return false;
    }
    if (!parseKey(parts[1], &binding->key)) {
        *error = QStringLiteral("unknown key \"%1\"").arg(parts[1]);
        return false;
    }
    if (described)
        binding->description = parts[2];
    binding->dispatcher = parts[fixed - 1].toLower();
    binding->argument = argument;
    const QString mods = parts[0].simplified().replace(QLatin1Char(' '), QLatin1Char('+'));
    binding->name = mods.isEmpty() ? parts[1] : mods + QLatin1Char('+') + parts[1];

    const QString &d = binding->dispatcher;
    const QString arg = argument.toLower();
    bool ok = true;
    if (d == QLatin1String("exec") || d == QLatin1String("visor")) {
        ok = !argument.isEmpty();
    } else if (d == QLatin1String("killactive") || d == QLatin1String("togglefloating")
               || d == QLatin1String("togglesplit")) {
        ok = true;
    } else if (d == QLatin1String("fullscreen")) {
        ok = arg.isEmpty() || arg == QLatin1String("0") || arg == QLatin1String("1");
    } else if (d == QLatin1String("movefocus") || d == QLatin1String("swapwindow")
               || d == QLatin1String("movewindow")) {
        ok = isDirection(arg);
    } else if (d == QLatin1String("resizeactive")) {
        const QStringList xy = arg.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        ok = xy.size() == 2 && isInt(xy[0]) && isInt(xy[1]);
    } else if (d == QLatin1String("workspace") || d == QLatin1String("movetoworkspace")
               || d == QLatin1String("movetoworkspacesilent")) {
        ok = arg == QLatin1String("new") || arg == QLatin1String("e+1") || arg == QLatin1String("e-1")
             || arg == QLatin1String("+1") || arg == QLatin1String("-1") || (isInt(arg) && arg.toInt() >= 1);
    } else if (d == QLatin1String("closeworkspace")) {
        ok = true;
    } else {
        *error = QStringLiteral("unknown dispatcher \"%1\"").arg(d);
        return false;
    }
    if (!ok) {
        *error = QStringLiteral("bad argument for %1: \"%2\"").arg(d, argument);
        return false;
    }
    return true;
}

// Parsing state shared across a file and the files it sources: variables
// carry over, and binding groups continue.
struct ParseState
{
    QHash<QString, QString> variables;
    int group = 0;
    bool inGroup = false; // the previous line was a binding
    int depth = 0;        // of `source` nesting
};

// `source = file` (Hyprland's include): ~ and %VAR% expanded, relative to the
// sourcing file. A missing file is skipped quietly, so wm.conf can source
// current/wm.conf before any theme has been applied.
QString resolveSource(QString value, const QString &baseDir)
{
    if (value.startsWith(QLatin1String("~/")) || value == QLatin1String("~"))
        value = QDir::home().filePath(value.mid(2));
    static const QRegularExpression envVar(QStringLiteral("%([^%]+)%"));
    QRegularExpressionMatch m;
    while ((m = envVar.match(value)).hasMatch())
        value.replace(m.capturedStart(), m.capturedLength(), qEnvironmentVariable(m.captured(1).toUtf8().constData()));
    const QFileInfo info(value);
    if (info.isRelative() && !baseDir.isEmpty())
        return QDir::cleanPath(QDir(baseDir).absoluteFilePath(value));
    return QDir::cleanPath(info.absoluteFilePath());
}

void parseInto(Config &config, ParseState &state, const QString &text, const QString &baseDir, const QString &label)
{
    QStringList sections;
    int &group = state.group;
    bool &inGroup = state.inGroup;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (qsizetype n = 0; n < lines.size(); ++n) {
        const auto fail = [&](const QString &message) {
            config.errors.append(label + QStringLiteral("line %1: %2").arg(n + 1).arg(message));
        };

        // '#' starts a comment; '##' is a literal '#' (as in Hyprland), e.g.
        // for the dialog class #32770.
        QString line;
        const QString &raw = lines[n];
        for (qsizetype i = 0; i < raw.size(); ++i) {
            if (raw[i] == QLatin1Char('#')) {
                if (i + 1 < raw.size() && raw[i + 1] == QLatin1Char('#')) {
                    line += QLatin1Char('#');
                    ++i;
                    continue;
                }
                break;
            }
            line += raw[i];
        }
        line = line.trimmed();
        if (line.isEmpty()) {
            // A gap after some bindings starts a new group.
            if (inGroup)
                ++group;
            inGroup = false;
            continue;
        }

        if (line == QLatin1String("}")) {
            if (sections.isEmpty())
                fail(QStringLiteral("unmatched }"));
            else
                sections.removeLast();
            continue;
        }
        if (line.endsWith(QLatin1Char('{'))) {
            sections.append(line.chopped(1).trimmed().toLower());
            continue;
        }

        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq < 0) {
            fail(QStringLiteral("expected key = value"));
            continue;
        }
        QString key = line.left(eq).trimmed();
        const QString value = substitute(line.mid(eq + 1).trimmed(), state.variables);

        if (key.startsWith(QLatin1Char('$'))) {
            state.variables.insert(key.mid(1), value);
            continue;
        }
        key = key.toLower();
        if (key == QLatin1String("source") && sections.isEmpty()) {
            const QString path = resolveSource(value, baseDir);
            config.sources.append(path);
            if (state.depth >= 8) {
                fail(QStringLiteral("source nested too deeply: %1").arg(path));
                continue;
            }
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue; // not there (yet)
            ++state.depth;
            parseInto(config, state, QString::fromUtf8(file.readAll()), QFileInfo(path).absolutePath(),
                      QDir::toNativeSeparators(path) + QStringLiteral(": "));
            --state.depth;
            continue;
        }
        if (!sections.isEmpty())
            key = sections.join(QLatin1Char(':')) + QLatin1Char(':') + key;

        bool ok = true;
        if (key == QLatin1String("general:gaps_in")) {
            ok = parseInt(value, &config.gapsIn);
        } else if (key == QLatin1String("general:gaps_out")) {
            ok = parseInt(value, &config.gapsOut);
        } else if (key == QLatin1String("general:layout")) {
            ok = value == QLatin1String("dwindle");
        } else if (key == QLatin1String("dwindle:default_split_ratio")) {
            const double ratio = value.toDouble(&ok);
            ok = ok && ratio >= 0.1 && ratio <= 1.9;
            if (ok)
                config.dwindle.splitRatio = ratio;
        } else if (key == QLatin1String("dwindle:preserve_split")) {
            ok = parseBool(value, &config.dwindle.preserveSplit);
        } else if (key == QLatin1String("dwindle:force_split")) {
            int force = 0;
            ok = parseInt(value, &force) && force >= 0 && force <= 2;
            if (ok)
                config.dwindle.forceSplit = force;
        } else if (key == QLatin1String("dwindle:split_width_multiplier")) {
            const double multiplier = value.toDouble(&ok);
            ok = ok && multiplier > 0;
            if (ok)
                config.dwindle.splitWidthMultiplier = multiplier;
        } else if (key.startsWith(QLatin1String("bind"))) {
            Binding binding;
            QString error;
            binding.group = group;
            inGroup = true;
            if (parseBinding(key.mid(4), value, &binding, &error))
                config.bindings.append(binding);
            else
                fail(error);
            continue;
        } else if (key == QLatin1String("windowrule") || key == QLatin1String("windowrulev2")) {
            WindowRule rule;
            QString error;
            if (parseRule(value, &rule, &error))
                config.rules.append(rule);
            else
                fail(error);
            continue;
        } else {
            fail(QStringLiteral("unknown setting %1").arg(key));
            continue;
        }
        if (!ok)
            fail(QStringLiteral("bad value for %1: %2").arg(key, value));
    }
    if (!sections.isEmpty())
        config.errors.append(label + QStringLiteral("unclosed section %1").arg(sections.last()));
}

} // namespace

bool WindowRule::matches(const QString &cls, const QString &windowTitle, const QString &exeName) const
{
    const auto test = [](const QRegularExpression &re, const QString &s) {
        return re.pattern().isEmpty() || re.match(s).hasMatch();
    };
    return test(windowClass, cls) && test(title, windowTitle) && test(exe, exeName);
}

Config Config::parse(const QString &text, const QString &baseDir)
{
    Config config;
    ParseState state;
    parseInto(config, state, text, baseDir, QString());
    return config;
}

Config Config::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Config config;
        config.errors.append(QStringLiteral("cannot read %1: %2").arg(path, file.errorString()));
        return config;
    }
    return parse(QString::fromUtf8(file.readAll()), QFileInfo(path).absolutePath());
}

QString resolveConfigPath(const QString &explicitPath)
{
    if (!explicitPath.isEmpty())
        return QFileInfo(explicitPath).absoluteFilePath();

    const QString env = qEnvironmentVariable("VISOR_WM_CONFIG");
    if (!env.isEmpty())
        return QFileInfo(env).absoluteFilePath();

    QStringList candidates{QDir::home().filePath(QStringLiteral(".config/visor/wm.conf"))};
#ifdef VISOR_DEV_CONFIG_DIR
    candidates << QStringLiteral(VISOR_DEV_CONFIG_DIR "/wm.conf");
#endif
    candidates << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config/wm.conf"));
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path))
            return QFileInfo(path).absoluteFilePath();
    }
    return {};
}

} // namespace visor::wm
