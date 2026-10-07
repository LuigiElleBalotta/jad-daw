#include "shortcuts/shortcut_map.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>

namespace jad {

namespace {

// The normalised portable text of a sequence, or an empty string when it is not a valid single sequence.
QString normalised(const QString& text) {
    const QKeySequence seq(text, QKeySequence::PortableText);
    if (seq.isEmpty() || seq.count() != 1) return {};
    return seq.toString(QKeySequence::PortableText);
}

bool parseObject(const QString& json, QJsonObject* out, QString* error) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = parseError.errorString();
        return false;
    }
    if (!doc.isObject()) {
        *error = "the top level must be an object";
        return false;
    }
    *out = doc.object();
    return true;
}

}  // namespace

ShortcutMap::Parsed ShortcutMap::parse(const QString& defaultsJson, const QString& userJson) {
    Parsed result;

    QJsonObject defaults;
    QString error;
    if (!parseObject(defaultsJson, &defaults, &error)) {
        result.problems << QString("default shortcuts unreadable: %1").arg(error);
    } else {
        for (auto it = defaults.begin(); it != defaults.end(); ++it) {
            const QString seq = it.value().isString() ? normalised(it.value().toString()) : QString();
            if (seq.isEmpty()) {
                result.problems << QString("default shortcut for %1 is not a valid sequence").arg(it.key());
                continue;
            }
            result.sequences.insert(it.key(), seq);
        }
    }

    QJsonObject user;
    if (userJson.trimmed().isEmpty()) return result;  // no user file is not a problem
    if (!parseObject(userJson, &user, &error)) {
        result.problems << QString("shortcuts file ignored: %1").arg(error);
        return result;
    }
    for (auto it = user.begin(); it != user.end(); ++it) {
        const QString& id = it.key();
        if (!result.sequences.contains(id)) {
            result.problems << QString("unknown action %1").arg(id);
            continue;
        }
        const QString seq = it.value().isString() ? normalised(it.value().toString()) : QString();
        if (seq.isEmpty()) {
            result.problems << QString("invalid sequence for %1").arg(id);
            continue;
        }
        QString owner;
        for (auto other = result.sequences.constBegin(); other != result.sequences.constEnd(); ++other)
            if (other.key() != id && other.value() == seq) owner = other.key();
        if (!owner.isEmpty()) {
            result.problems << QString("%1 for %2 is already used by %3").arg(seq, id, owner);
            continue;
        }
        result.sequences.insert(id, seq);
    }
    return result;
}

ShortcutMap ShortcutMap::fromFiles(const QString& defaultsJson, const QString& userJson, QStringList* problems) {
    Parsed parsed = parse(defaultsJson, userJson);
    if (problems) *problems = parsed.problems;
    return ShortcutMap(std::move(parsed));
}

}  // namespace jad
