#include "bridge/library_model.h"

#include <QVariantMap>

#include "lpc/patch_library.h"

namespace jad {

namespace {
bool kindOf(const QString& name, lpc::TrackKind* out) {
    if (name == "audio") *out = lpc::TrackKind::Audio;
    else if (name == "instrument") *out = lpc::TrackKind::Instrument;
    else if (name == "aux") *out = lpc::TrackKind::Aux;
    else if (name == "bus") *out = lpc::TrackKind::Bus;
    else return false;
    return true;
}
QVariantMap entry(const lpc::Patch& p) {
    return {{"id", QString::fromStdString(p.id)}, {"name", QString::fromStdString(p.name)}, {"category", QString::fromStdString(p.category)}};
}
}  // namespace

void LibraryModel::setLibrary(const lpc::PatchLibrary* library) {
    library_ = library;
    rebuild();
}

void LibraryModel::setTrack(const QString& kind, const QString& patchId) {
    if (kind == kind_ && patchId == patchId_) return;
    const bool kindChanged = kind != kind_;
    kind_ = kind;
    patchId_ = patchId;
    if (library_) {
        lpc::TrackKind k;
        if (const lpc::Patch* p = library_->find(patchId.toStdString()); p && kindOf(kind, &k) && p->kind == k)
            category_ = QString::fromStdString(p->category);  // open on the category of the track's patch
        else if (kindChanged)
            category_.clear();
    }
    rebuild();
}

void LibraryModel::setCategory(const QString& category) {
    if (category == category_) return;
    category_ = category;
    rebuild();
}

void LibraryModel::setSearch(const QString& text) {
    if (text == search_) return;
    search_ = text;
    rebuild();
}

void LibraryModel::rebuild() {
    QStringList categories;
    QVariantList patches;
    lpc::TrackKind k;
    if (library_ && kindOf(kind_, &k)) {
        for (const std::string& c : library_->categories(k)) categories.append(QString::fromStdString(c));
        if (!categories.contains(category_)) category_ = categories.isEmpty() ? QString() : categories.first();
        if (search_.isEmpty()) {
            for (const lpc::Patch* p : library_->inCategory(k, category_.toStdString())) patches.append(entry(*p));
        } else {
            for (const lpc::Patch& p : library_->patches()) {
                if (p.kind != k) continue;
                const QString name = QString::fromStdString(p.name), category = QString::fromStdString(p.category);
                if (name.contains(search_, Qt::CaseInsensitive) || category.contains(search_, Qt::CaseInsensitive)) patches.append(entry(p));
            }
        }
    } else {
        category_.clear();
    }
    categories_ = categories;
    patches_ = patches;
    emit changed();
}

QStringList LibraryModel::problems() const {
    QStringList out;
    if (library_)
        for (const lpc::PatchProblem& p : library_->problems()) out.append(QString::fromStdString(p.where + ": " + p.message));
    return out;
}

int LibraryModel::patchCount() const { return library_ ? static_cast<int>(library_->patches().size()) : 0; }

QString LibraryModel::neighbour(int step) const {
    if (patches_.isEmpty()) return {};
    int at = -1;
    for (int i = 0; i < patches_.size(); ++i)
        if (patches_[i].toMap().value("id").toString() == patchId_) at = i;
    int next;
    if (at < 0) next = step > 0 ? 0 : static_cast<int>(patches_.size()) - 1;
    else next = qBound(0, at + step, static_cast<int>(patches_.size()) - 1);
    return patches_[next].toMap().value("id").toString();
}

}  // namespace jad
