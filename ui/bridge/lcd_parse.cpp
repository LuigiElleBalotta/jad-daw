#include "bridge/lcd_parse.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace jad::lcd {

namespace {

constexpr int kMaxText = 64;

QString clean(const QString& text) { return text.trimmed(); }

// Only plain ASCII digits count: QString::toInt would also accept other scripts.
std::optional<long long> asciiInt(const QString& s) {
    static const QRegularExpression re("^[0-9]{1,7}$");
    if (!re.match(s).hasMatch()) return std::nullopt;
    return s.toLongLong();
}

}  // namespace

std::optional<double> parseBpm(const QString& text) {
    const QString t = clean(text);
    static const QRegularExpression re("^[0-9]{1,4}([.,][0-9]{1,3})?$");
    if (t.size() > kMaxText || !re.match(t).hasMatch()) return std::nullopt;
    QString normalised = t;
    normalised.replace(',', '.');
    const double v = normalised.toDouble();
    if (!(v >= 20.0 && v <= 999.0)) return std::nullopt;
    return v;
}

std::optional<Signature> parseSignature(const QString& text) {
    static const QRegularExpression re("^\\s*([0-9]{1,2})\\s*/\\s*([0-9]{1,2})\\s*$");
    const auto m = re.match(text);
    if (text.size() > kMaxText || !m.hasMatch()) return std::nullopt;
    const int num = m.captured(1).toInt(), den = m.captured(2).toInt();
    const bool denOk = den == 1 || den == 2 || den == 4 || den == 8 || den == 16 || den == 32;
    if (num < 1 || num > 32 || !denOk) return std::nullopt;
    return Signature{num, den};
}

std::optional<double> parsePositionBeats(const QString& text, int beatsPerBar) {
    const QString t = clean(text);
    if (t.isEmpty() || t.size() > kMaxText || beatsPerBar < 1) return std::nullopt;
    static const QRegularExpression splitter("[ .]+");
    const QStringList parts = t.split(splitter, Qt::KeepEmptyParts);
    if (parts.size() < 1 || parts.size() > 4) return std::nullopt;
    long long v[4] = {1, 1, 1, 1};
    for (int i = 0; i < parts.size(); ++i) {
        const auto n = asciiInt(parts[i]);
        if (!n) return std::nullopt;
        v[i] = *n;
    }
    const long long bar = v[0], beat = v[1], division = v[2], tick = v[3];
    if (bar < 1 || beat < 1 || beat > beatsPerBar || division < 1 || division > 4 || tick < 1 || tick > 240) return std::nullopt;
    return static_cast<double>((bar - 1) * beatsPerBar + (beat - 1)) + static_cast<double>((division - 1) * 240 + (tick - 1)) / 960.0;
}

std::optional<double> parseTimeSeconds(const QString& text) {
    const QString t = clean(text);
    static const QRegularExpression re("^[0-9]+(:[0-9]{1,2}){0,2}(\\.[0-9]{1,3})?$");
    if (t.isEmpty() || t.size() > 32 || !re.match(t).hasMatch()) return std::nullopt;
    const QStringList parts = t.split(':');
    double total = 0.0;
    for (int i = 0; i < parts.size(); ++i) {
        const double v = parts[i].toDouble();
        if (i > 0 && v >= 60.0) return std::nullopt;  // minutes and seconds are below 60 when a bigger unit leads
        total = total * 60.0 + v;
    }
    if (!(total >= 0.0 && total <= 86400.0)) return std::nullopt;
    return total;
}

QString formatPosition(double beats, int beatsPerBar) {
    if (!(beats > 0.0) || !std::isfinite(beats)) beats = 0.0;
    const int bpb = std::max(1, beatsPerBar);
    const long long total = std::llround(std::min(beats, 1e7) * 960.0);
    const long long barTicks = static_cast<long long>(bpb) * 960;
    const long long bar = total / barTicks + 1;
    const long long rem = total % barTicks;
    const long long beat = rem / 960 + 1;
    const long long rem2 = rem % 960;
    const long long division = rem2 / 240 + 1;
    const long long tick = rem2 % 240 + 1;
    return QStringLiteral("%1 %2 %3 %4").arg(bar).arg(beat).arg(division).arg(tick);
}

QString formatTime(double seconds) {
    if (!(seconds > 0.0) || !std::isfinite(seconds)) seconds = 0.0;
    const long long tenths = static_cast<long long>(std::floor(std::min(seconds, 86400.0) * 10.0 + 1e-9));
    const long long whole = tenths / 10;
    return QStringLiteral("%1:%2.%3").arg(whole / 60, 2, 10, QLatin1Char('0')).arg(whole % 60, 2, 10, QLatin1Char('0')).arg(tenths % 10);
}

}  // namespace jad::lcd

namespace jad {

double LcdParser::bpm(const QString& text) const {
    const auto v = lcd::parseBpm(text);
    return v ? *v : -1.0;
}
QVariantMap LcdParser::signature(const QString& text) const {
    const auto v = lcd::parseSignature(text);
    if (!v) return {};
    return {{"numerator", v->numerator}, {"denominator", v->denominator}};
}
double LcdParser::positionBeats(const QString& text, int beatsPerBar) const {
    const auto v = lcd::parsePositionBeats(text, beatsPerBar);
    return v ? *v : -1.0;
}
double LcdParser::timeSeconds(const QString& text) const {
    const auto v = lcd::parseTimeSeconds(text);
    return v ? *v : -1.0;
}

}  // namespace jad
