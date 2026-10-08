#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace jad::lcd {

// Parsing and formatting of what the LCD shows and accepts. Everything returns nullopt (or an empty result) on
// anything it does not understand: the caller keeps the old value and tells the user.

struct Signature {
    int numerator = 4;
    int denominator = 4;
};

std::optional<double> parseBpm(const QString& text);        // 20 to 999, "." or "," as the decimal mark
std::optional<Signature> parseSignature(const QString& text);  // "3/4": numerator 1 to 32, denominator 1, 2, 4, 8, 16 or 32
// "bar beat division tick", separated by spaces or dots, all 1-based; missing parts are 1. Division 1 to 4 (sixteenths),
// tick 1 to 240. Returns beats from the start.
std::optional<double> parsePositionBeats(const QString& text, double barBeats);  // barBeats: quarter-note beats in a bar
std::optional<double> parseTimeSeconds(const QString& text);   // "83.5", "1:23.5" or "0:01:23.5"; up to 24 hours
QString formatPosition(double beats, double barBeats);         // "5 2 3 1"; negative input shows the start
QString formatTime(double seconds);                            // "01:23.5"

}  // namespace jad::lcd

namespace jad {

// The same functions for QML. Failure is -1 (numbers) or an empty map.
class LcdParser : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit LcdParser(QObject* parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE double bpm(const QString& text) const;
    Q_INVOKABLE QVariantMap signature(const QString& text) const;  // {numerator, denominator}
    Q_INVOKABLE double positionBeats(const QString& text, double barBeats) const;
    Q_INVOKABLE double timeSeconds(const QString& text) const;
    Q_INVOKABLE QString formatPosition(double beats, double barBeats) const { return lcd::formatPosition(beats, barBeats); }
    Q_INVOKABLE QString formatTime(double seconds) const { return lcd::formatTime(seconds); }
};

}  // namespace jad
