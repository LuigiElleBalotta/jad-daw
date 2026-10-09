#include <QSignalSpy>
#include <QtTest>

#include "bridge/plugins_model.h"
#include "bridge/row_maps.h"
#include "bridge/snapshot.h"

namespace {
jad::PluginRow row(const char* id, const char* name, const char* vendor, const char* status = "ok") {
    jad::PluginRow r;
    r.id = id;
    r.name = name;
    r.vendor = vendor;
    r.status = status;
    r.path = QString("C:/VST3/") + name + ".vst3";
    return r;
}
}  // namespace

class PluginsTest : public QObject {
    Q_OBJECT
private slots:
    void rowsAreExposedByRole() {
        jad::PluginsModel m;
        m.setRows({row("vst3:a", "Verb", "Acme"), row("vst3:b", "Broken", "Acme", "failed")});
        QCOMPARE(m.rowCount(), 2);
        const auto roles = m.roleNames();
        QCOMPARE(m.data(m.index(0), roles.key("name")).toString(), QString("Verb"));
        QCOMPARE(m.data(m.index(1), roles.key("status")).toString(), QString("failed"));
    }
    void onlyUsablePluginsAreKnownAndInTheMenu() {
        jad::PluginsModel m;
        m.setRows({row("vst3:a", "Verb", "Zed"), row("vst3:b", "Comp", "Acme"), row("vst3:c", "Alpha", "Acme"), row("vst3:d", "Bad", "Acme", "failed")});
        QCOMPARE(m.knownIds(), QStringList({"vst3:a", "vst3:b", "vst3:c"}));
        const QVariantList menu = m.menu();
        QCOMPARE(menu.size(), 2);
        QCOMPARE(menu[0].toMap().value("vendor").toString(), QString("Acme"));
        const QVariantList acme = menu[0].toMap().value("plugins").toList();
        QCOMPARE(acme.size(), 2);
        QCOMPARE(acme[0].toMap().value("name").toString(), QString("Alpha"));  // sorted by name
        QCOMPARE(acme[1].toMap().value("name").toString(), QString("Comp"));
        QCOMPARE(menu[1].toMap().value("vendor").toString(), QString("Zed"));
    }
    void changedIsEmittedWhenRowsChange() {
        jad::PluginsModel m;
        QSignalSpy spy(&m, &jad::PluginsModel::changed);
        m.setRows({row("vst3:a", "Verb", "Acme")});
        QCOMPARE(spy.count(), 1);
    }
    void scanStateText() {
        jad::PluginsModel m;
        QVERIFY(!m.scanning());
        QCOMPARE(m.scanText(), QString());
        m.setScan(true, 3, 12);
        QVERIFY(m.scanning());
        QCOMPARE(m.scanText(), QString("Scanning 3 of 12"));
        m.setScan(false, 12, 12);
        QCOMPARE(m.scanText(), QString());
    }
    void rescanInvokablesEmitTheMode() {
        jad::PluginsModel m;
        QSignalSpy spy(&m, &jad::PluginsModel::rescanRequested);
        m.rescanNew();
        m.rescanFailed();
        m.rescanAll();
        QCOMPARE(spy.count(), 3);
        QCOMPARE(spy.at(0).at(0).toInt(), 0);
        QCOMPARE(spy.at(1).at(0).toInt(), 1);
        QCOMPARE(spy.at(2).at(0).toInt(), 2);
    }
    void insertRowsCarryLabelAndPluginFlag() {
        jad::TrackRow t;
        t.id = "t";
        t.kind = "audio";
        t.name = "A";
        t.inserts.push_back({"builtin.gain", 2.0, "", false});
        t.inserts.push_back({"vst3:00112233445566778899aabbccddeeff", 0.0, "Verb", true});
        const QVariantMap map = jad::trackToMap(t);
        const QVariantList inserts = map.value("inserts").toList();
        QCOMPARE(inserts[0].toMap().value("plugin").toBool(), false);
        QCOMPARE(inserts[1].toMap().value("plugin").toBool(), true);
        QCOMPARE(inserts[1].toMap().value("label").toString(), QString("Verb"));
    }
};

QTEST_GUILESS_MAIN(PluginsTest)
#include "tst_plugins.moc"
