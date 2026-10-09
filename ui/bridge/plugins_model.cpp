#include "plugins_model.h"

#include <QMap>
#include <algorithm>

namespace jad {

QHash<int, QByteArray> PluginsModel::roleNames() const {
    return {{IdRole, "id"}, {NameRole, "name"}, {VendorRole, "vendor"}, {StatusRole, "status"}, {PathRole, "path"}, {ReasonRole, "reason"}};
}

QVariant PluginsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const PluginRow& r = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case IdRole: return r.id;
        case NameRole: return r.name;
        case VendorRole: return r.vendor;
        case StatusRole: return r.status;
        case PathRole: return r.path;
        case ReasonRole: return r.reason;
        default: return {};
    }
}

QString PluginsModel::scanText() const { return scanning_ ? QString("Scanning %1 of %2").arg(done_).arg(total_) : QString(); }

void PluginsModel::setRows(std::vector<PluginRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    rebuild();
    endResetModel();
    emit changed();
}

void PluginsModel::setScan(bool running, int done, int total) {
    if (running == scanning_ && done == done_ && total == total_) return;
    scanning_ = running;
    done_ = done;
    total_ = total;
    emit changed();
}

void PluginsModel::setSupported(bool supported) {
    if (supported == supported_) return;
    supported_ = supported;
    emit changed();
}

void PluginsModel::rebuild() {
    knownIds_.clear();
    QMap<QString, QList<PluginRow>> byVendor;  // QMap keeps the vendors sorted
    for (const PluginRow& r : rows_) {
        if (r.status != "ok") continue;
        knownIds_.push_back(r.id);
        byVendor[r.vendor.isEmpty() ? QString("Other") : r.vendor].append(r);
    }
    menu_.clear();
    for (auto it = byVendor.begin(); it != byVendor.end(); ++it) {
        QList<PluginRow> list = it.value();
        std::sort(list.begin(), list.end(), [](const PluginRow& a, const PluginRow& b) { return a.name.toLower() < b.name.toLower(); });
        QVariantList plugins;
        for (const PluginRow& p : list) plugins.append(QVariantMap{{"id", p.id}, {"name", p.name}});
        menu_.append(QVariantMap{{"vendor", it.key()}, {"plugins", plugins}});
    }
}

}  // namespace jad
