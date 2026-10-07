#include <QtTest>
#include "bridge/waveform_cache.h"
#include "lpc/wav.h"
#include "tests_temp.h"

class WaveformTest : public QObject {
    Q_OBJECT
private slots:
    void peaksOfAKnownSignal() {
        TempDir dir;
        std::vector<float> s(2 * 48000, 0.0f);       // 1 s stereo: first half silent, second half 0.5
        for (int i = 48000 / 2; i < 48000; ++i) { s[size_t(i) * 2] = 0.5f; s[size_t(i) * 2 + 1] = 0.5f; }
        lpc::writeWav(dir.path() / "a.wav", 48000, 2, s, lpc::WavFormat::Float32);
        jad::WaveformCache cache;
        const auto p = cache.peaks(dir.path() / "a.wav", 10);
        QCOMPARE(int(p.size()), 10);
        QCOMPARE(p[0], 0.0f);
        QVERIFY(qFuzzyCompare(p[9], 0.5f));
    }
    void missingOrCorruptFileGivesEmpty() {
        TempDir dir;
        jad::WaveformCache cache;
        QVERIFY(cache.peaks(dir.path() / "nope.wav", 10).empty());
        QFile f(QString::fromStdU16String((dir.path() / "bad.wav").u16string()));
        QVERIFY(f.open(QIODevice::WriteOnly)); f.write("not a wav"); f.close();
        QVERIFY(cache.peaks(dir.path() / "bad.wav", 10).empty());
    }
};
QTEST_MAIN(WaveformTest)
#include "tst_waveform.moc"
