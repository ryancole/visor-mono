#include "wm/config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

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

// rgb(rrggbb), rgba(rrggbbaa) or 0xaarrggbb. Hyprland borders can be
// gradients ("rgba(...) rgba(...) 45deg"); Windows draws one colour, the first.
bool parseColor(const QString &value, quint32 *out)
{
    const QString first = value.section(QLatin1Char(' '), 0, 0, QString::SectionSkipEmpty).toLower();
    bool ok = false;
    if (first.startsWith(QLatin1String("rgb(")) && first.endsWith(QLatin1Char(')')) && first.size() == 11) {
        const quint32 rgb = first.mid(4, 6).toUInt(&ok, 16);
        if (ok)
            *out = rgb;
    } else if (first.startsWith(QLatin1String("rgba(")) && first.endsWith(QLatin1Char(')')) && first.size() == 14) {
        const quint32 rgb = first.mid(5, 6).toUInt(&ok, 16);
        if (ok)
            *out = rgb;
    } else if (first.startsWith(QLatin1String("0x")) && first.size() == 10) {
        const quint32 argb = first.mid(2).toUInt(&ok, 16);
        if (ok)
            *out = argb & 0xffffff;
    }
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

} // namespace

bool WindowRule::matches(const QString &cls, const QString &windowTitle, const QString &exeName) const
{
    const auto test = [](const QRegularExpression &re, const QString &s) {
        return re.pattern().isEmpty() || re.match(s).hasMatch();
    };
    return test(windowClass, cls) && test(title, windowTitle) && test(exe, exeName);
}

Config Config::parse(const QString &text)
{
    Config config;
    QHash<QString, QString> variables;
    QStringList sections;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (qsizetype n = 0; n < lines.size(); ++n) {
        const auto fail = [&](const QString &message) {
            config.errors.append(QStringLiteral("line %1: %2").arg(n + 1).arg(message));
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
        if (line.isEmpty())
            continue;

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
        const QString value = substitute(line.mid(eq + 1).trimmed(), variables);

        if (key.startsWith(QLatin1Char('$'))) {
            variables.insert(key.mid(1), value);
            continue;
        }
        key = key.toLower();
        if (!sections.isEmpty())
            key = sections.join(QLatin1Char(':')) + QLatin1Char(':') + key;

        bool ok = true;
        if (key == QLatin1String("general:gaps_in")) {
            ok = parseInt(value, &config.gapsIn);
        } else if (key == QLatin1String("general:gaps_out")) {
            ok = parseInt(value, &config.gapsOut);
        } else if (key == QLatin1String("general:border_size")) {
            ok = parseInt(value, &config.borderSize);
        } else if (key == QLatin1String("general:col.active_border")) {
            ok = parseColor(value, &config.activeBorder);
        } else if (key == QLatin1String("general:col.inactive_border")) {
            ok = parseColor(value, &config.inactiveBorder);
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
        config.errors.append(QStringLiteral("unclosed section %1").arg(sections.last()));
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
    return parse(QString::fromUtf8(file.readAll()));
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
