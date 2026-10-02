#include "services/keybindings.h"

#include <QHash>
#include <QVariantMap>

KeyBindings::KeyBindings(QObject *parent)
    : QAbstractListModel(parent)
{
    ShellLink *link = ShellLink::instance();
    if (!link)
        return;
    reset();
    connect(link, &ShellLink::bindingsChanged, this, &KeyBindings::reset);
}

void KeyBindings::reset()
{
    beginResetModel();
    m_bindings = ShellLink::instance()->bindings();
    m_groups.clear();
    for (const ShellLink::Binding &b : std::as_const(m_bindings)) {
        if (!m_groups.contains(b.group))
            m_groups.append(b.group);
    }
    endResetModel();
    emit changed();
}

QVariantList KeyBindings::group(int n) const
{
    QVariantList list;
    if (n < 0 || n >= m_groups.size())
        return list;
    for (int row = 0; row < m_bindings.size(); ++row) {
        if (m_bindings[row].group == m_groups[n]) {
            list.append(QVariantMap{{QStringLiteral("label"), data(index(row), LabelRole)},
                                    {QStringLiteral("description"), data(index(row), DescriptionRole)}});
        }
    }
    return list;
}

int KeyBindings::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_bindings.size());
}

QVariant KeyBindings::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_bindings.size())
        return {};
    const ShellLink::Binding &b = m_bindings[index.row()];
    switch (role) {
    case KeysRole:
        return b.keys;
    case LabelRole:
        return label(b.keys);
    case DescriptionRole:
        // Undescribed `bind` lines show what they do instead.
        return b.description.isEmpty() ? (b.dispatcher + QLatin1Char(' ') + b.argument).trimmed() : b.description;
    case DispatcherRole:
        return b.dispatcher;
    case ArgumentRole:
        return b.argument;
    case GroupRole:
        return b.group;
    default:
        return {};
    }
}

QHash<int, QByteArray> KeyBindings::roleNames() const
{
    return {
        {KeysRole, "keys"},         {LabelRole, "label"},       {DescriptionRole, "description"},
        {DispatcherRole, "dispatcher"}, {ArgumentRole, "argument"}, {GroupRole, "group"},
    };
}

QString KeyBindings::label(const QString &keys)
{
    static const QHash<QString, QString> names{
        {"super", "Win"},    {"win", "Win"},       {"mod4", "Win"},     {"meta", "Win"},
        {"ctrl", "Ctrl"},    {"control", "Ctrl"},  {"shift", "Shift"},  {"alt", "Alt"},
        {"mod1", "Alt"},     {"return", "Enter"},  {"enter", "Enter"},  {"space", "Space"},
        {"escape", "Esc"},   {"backspace", "Backspace"}, {"delete", "Del"}, {"tab", "Tab"},
        {"left", "←"},  {"right", "→"},  {"up", "↑"},    {"down", "↓"},
        {"minus", "-"},      {"equal", "="},       {"comma", ","},      {"period", "."},
        {"slash", "/"},      {"grave", "`"},       {"semicolon", ";"},  {"apostrophe", "'"},
        {"bracketleft", "["}, {"bracketright", "]"}, {"backslash", "\\"}, {"prior", "PgUp"},
        {"page_up", "PgUp"}, {"next", "PgDn"},     {"page_down", "PgDn"}, {"home", "Home"},
        {"end", "End"},      {"insert", "Ins"},    {"print", "PrtSc"},
        // The media keys, by their xkb names.
        {"xf86audioraisevolume", "Volume up"}, {"xf86audiolowervolume", "Volume down"},
        {"xf86audiomute", "Mute"}, {"xf86audioplay", "Play/Pause"}, {"xf86audionext", "Next track"},
        {"xf86audioprev", "Previous track"}, {"xf86audiostop", "Stop"},
    };
    QStringList parts;
    const QStringList tokens = keys.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        const QString lower = token.trimmed().toLower();
        if (lower.isEmpty())
            continue;
        // A bare Win press is written "SUPER, SUPER_L": just "Win".
        if (lower == QLatin1String("super_l") || lower == QLatin1String("super_r")) {
            if (!parts.contains(QStringLiteral("Win")))
                parts.append(QStringLiteral("Win"));
            continue;
        }
        if (const auto it = names.constFind(lower); it != names.cend()) {
            if (!parts.contains(*it))
                parts.append(*it);
        } else {
            parts.append(lower.size() == 1 ? lower.toUpper() : lower.left(1).toUpper() + lower.mid(1));
        }
    }
    return parts.join(QStringLiteral(" + "));
}
