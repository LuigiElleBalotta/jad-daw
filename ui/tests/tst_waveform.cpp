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
    void peaksAreCachedOnDiskAndInvalidatedWhenTheFileChanges() {
        TempDir dir;
        const auto wav = dir.path() / "a.wav";
        const auto cacheDir = dir.path() / "cache";
        lpc::writeWav(wav, 48000, 2, std::vector<float>(2 * 4800, 0.5f), lpc::WavFormat::Float32);
        jad::WaveformCache cache(cacheDir);
        const auto first = cache.peaks(wav, 8);
        QCOMPARE(int(first.size()), 8);
        QVERIFY(qFuzzyCompare(first[0], 0.5f));

        std::filesystem::path stored;
        for (const auto& e : std::filesystem::directory_iterator(cacheDir)) stored = e.path();
        QVERIFY(!stored.empty());
        QCOMPARE(stored.extension().string(), std::string(".peaks"));

        // overwrite the cache entry with a sentinel: the next call must read it instead of the wav
        {
            QFile f(QString::fromStdU16String(stored.u16string()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            const std::vector<float> sentinel(8, 0.25f);
            f.write(reinterpret_cast<const char*>(sentinel.data()), qint64(sentinel.size() * sizeof(float)));
        }
        const auto cached = cache.peaks(wav, 8);
        QCOMPARE(int(cached.size()), 8);
        QVERIFY(qFuzzyCompare(cached[0], 0.25f));

        // the wav changes (different length): the stale entry is not used
        lpc::writeWav(wav, 48000, 2, std::vector<float>(2 * 9600, 0.75f), lpc::WavFormat::Float32);
        const auto fresh = cache.peaks(wav, 8);
        QVERIFY(qFuzzyCompare(fresh[0], 0.75f));

        // a damaged cache entry (wrong size) is ignored too
        for (const auto& e : std::filesystem::directory_iterator(cacheDir)) {
            QFile f(QString::fromStdU16String(e.path().u16string()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("xx");
        }
        const auto again = cache.peaks(wav, 8);
        QCOMPARE(int(again.size()), 8);
        QVERIFY(qFuzzyCompare(again[0], 0.75f));
    }
};
QTEST_MAIN(WaveformTest)
#include "tst_waveform.moc"
