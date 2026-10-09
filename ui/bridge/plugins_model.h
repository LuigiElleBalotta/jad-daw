#pragma once
#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <vector>

namespace jad {

struct PluginRow {
    QString id, name, vendor, status, path, reason;  // status: "ok" or "failed"
};

// The plug-in catalogue as the UI sees it: the Plug-in Manager table, the insert menu and the "is it installed" check.
class PluginsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QStringList knownIds READ knownIds NOTIFY changed)
    Q_PROPERTY(QVariantList menu READ menu NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(QString scanText READ scanText NOTIFY changed)
    Q_PROPERTY(bool supported READ supported NOTIFY changed)
public:
    explicit PluginsModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList knownIds() const { return knownIds_; }
    QVariantList menu() const { return menu_; }
    bool scanning() const { return scanning_; }
    QString scanText() const;
    bool supported() const { return supported_; }

    void setRows(std::vector<PluginRow> rows);
    void setScan(bool running, int done, int total);
    void setSupported(bool supported);

    Q_INVOKABLE void rescanNew() { emit rescanRequested(0); }
    Q_INVOKABLE void rescanFailed() { emit rescanRequested(1); }
    Q_INVOKABLE void rescanAll() { emit rescanRequested(2); }

signals:
    void changed();
    void rescanRequested(int mode);  // 0 new and changed, 1 failed, 2 all

private:
    enum Role { IdRole = Qt::UserRole + 1, NameRole, VendorRole, StatusRole, PathRole, ReasonRole };
    void rebuild();

    std::vector<PluginRow> rows_;
    QStringList knownIds_;
    QVariantList menu_;
    bool scanning_ = false;
    bool supported_ = false;
    int done_ = 0, total_ = 0;
};

}  // namespace jad
