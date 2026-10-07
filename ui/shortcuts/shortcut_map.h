#pragma once
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

namespace jad {

// Action id -> key sequence (portable text, "Ctrl" is Cmd on macOS). Built from a defaults file and an optional
// user file that overrides single actions. Anything wrong in the user file is reported and ignored.
class ShortcutMap : public QObject {
    Q_OBJECT
public:
    // `problems` (optional) receives one message per ignored entry or unreadable file.
    static ShortcutMap fromFiles(const QString& defaultsJson, const QString& userJson, QStringList* problems);

    QString sequence(const QString& actionId) const { return sequences_.value(actionId); }
    QStringList problems() const { return problems_; }

private:
    struct Parsed {
        QHash<QString, QString> sequences;
        QStringList problems;
    };
    static Parsed parse(const QString& defaultsJson, const QString& userJson);
    explicit ShortcutMap(Parsed parsed) : sequences_(std::move(parsed.sequences)), problems_(std::move(parsed.problems)) {}

    QHash<QString, QString> sequences_;
    QStringList problems_;
};

}  // namespace jad
