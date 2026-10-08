#include <QtTest>

#include "bridge/lcd_parse.h"

using namespace jad::lcd;

class LcdTest : public QObject {
    Q_OBJECT
private slots:
    void bpm_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<double>("value");
        QTest::newRow("plain") << "120" << true << 120.0;
        QTest::newRow("decimal point") << " 93.5 " << true << 93.5;
        QTest::newRow("decimal comma") << "93,5" << true << 93.5;
        QTest::newRow("min") << "20" << true << 20.0;
        QTest::newRow("max") << "999" << true << 999.0;
        QTest::newRow("too low") << "19.9" << false << 0.0;
        QTest::newRow("too high") << "1000" << false << 0.0;
        QTest::newRow("zero") << "0" << false << 0.0;
        QTest::newRow("empty") << "" << false << 0.0;
        QTest::newRow("words") << "abc" << false << 0.0;
        QTest::newRow("trailing junk") << "120 bpm" << false << 0.0;
        QTest::newRow("nan") << "nan" << false << 0.0;
        QTest::newRow("unicode digits") << QString::fromUtf8("\xD9\xA1\xD9\xA2\xD9\xA0") << false << 0.0;  // Arabic-Indic digits
        QTest::newRow("very long") << QString(500, '9') << false << 0.0;
    }
    void bpm() {
        QFETCH(QString, text);
        QFETCH(bool, ok);
        QFETCH(double, value);
        const auto r = parseBpm(text);
        QCOMPARE(r.has_value(), ok);
        if (ok) QCOMPARE(*r, value);
    }
    void signature_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("num");
        QTest::addColumn<int>("den");
        QTest::newRow("4/4") << "4/4" << 4 << 4;
        QTest::newRow("spaces") << " 7 / 8 " << 7 << 8;
        QTest::newRow("max numerator") << "32/16" << 32 << 16;
        QTest::newRow("zero den") << "3/0" << 0 << 0;
        QTest::newRow("bad den") << "7/9" << 0 << 0;
        QTest::newRow("zero num") << "0/4" << 0 << 0;
        QTest::newRow("too big num") << "33/4" << 0 << 0;
        QTest::newRow("no slash") << "44" << 0 << 0;
        QTest::newRow("empty") << "" << 0 << 0;
        QTest::newRow("negative") << "-3/4" << 0 << 0;
    }
    void signature() {
        QFETCH(QString, text);
        QFETCH(int, num);
        QFETCH(int, den);
        const auto r = parseSignature(text);
        QCOMPARE(r.has_value(), num != 0);
        if (r) {
            QCOMPARE(r->numerator, num);
            QCOMPARE(r->denominator, den);
        }
    }
    void positionRoundTrip() {
        // bar 5 beat 2 division 3 tick 1 in 4/4: 4 bars + 1 beat + 2 sixteenths = 17.5 beats
        QCOMPARE(formatPosition(17.5, 4), QStringLiteral("5 2 3 1"));
        QCOMPARE(*parsePositionBeats("5 2 3 1", 4), 17.5);
        QCOMPARE(*parsePositionBeats("5.2.3.1", 4), 17.5);
        QCOMPARE(*parsePositionBeats("5", 4), 16.0);  // missing parts are 1
        QCOMPARE(*parsePositionBeats("1", 4), 0.0);
        QCOMPARE(formatPosition(0.0, 4), QStringLiteral("1 1 1 1"));
        QCOMPARE(formatPosition(-3.0, 4), QStringLiteral("1 1 1 1"));  // never negative
        for (double b : {0.0, 0.25, 3.999, 12.5, 100.1}) {
            const auto parsed = parsePositionBeats(formatPosition(b, 3), 3);
            QVERIFY(parsed.has_value());
            QVERIFY(qAbs(*parsed - b) <= 1.0 / 960 + 1e-9);  // one tick of resolution
        }
    }
    void badPositions() {
        for (const QString& t : {QString(""), QString("0"), QString("x"), QString("1 5 1 1"), QString("1 1 5 1"), QString("1 1 1 241"),
                                 QString("1 1 1 1 1"), QString("-2"), QString("1,2"), QString(300, '1')})
            QVERIFY2(!parsePositionBeats(t, 4).has_value(), qPrintable(t));
    }
    void timeParsing() {
        QCOMPARE(*parseTimeSeconds("83.5"), 83.5);
        QCOMPARE(*parseTimeSeconds("1:23.5"), 83.5);
        QCOMPARE(*parseTimeSeconds("0:01:23.5"), 83.5);
        QCOMPARE(formatTime(8.0), QStringLiteral("00:08.0"));
        QCOMPARE(formatTime(83.54), QStringLiteral("01:23.5"));
        QVERIFY(!parseTimeSeconds("").has_value());
        QVERIFY(!parseTimeSeconds("a:b").has_value());
        QVERIFY(!parseTimeSeconds("-5").has_value());
        QVERIFY(!parseTimeSeconds("1:75").has_value());          // the seconds part must be below 60
        QVERIFY(!parseTimeSeconds("99999999999").has_value());  // beyond the supported range
    }
};

QTEST_MAIN(LcdTest)
#include "tst_lcd.moc"
