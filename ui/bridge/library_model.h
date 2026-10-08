#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace lpc {
class PatchLibrary;
}

namespace jad {

// The Library panel: the categories and patches of the selected track's kind, a name search, the current patch.
class LibraryModel : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QStringList categories READ categories NOTIFY changed)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY changed)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY changed)
    Q_PROPERTY(QVariantList patches READ patches NOTIFY changed)
    Q_PROPERTY(QString currentPatchId READ currentPatchId NOTIFY changed)
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QStringList problems READ problems NOTIFY changed)
    Q_PROPERTY(int patchCount READ patchCount NOTIFY changed)
public:
    explicit LibraryModel(QObject* parent = nullptr) : QObject(parent) {}
    void setLibrary(const lpc::PatchLibrary* library);  // not owned; must outlive the model
    void setTrack(const QString& kind, const QString& patchId);
    QStringList categories() const { return categories_; }
    QString category() const { return category_; }
    void setCategory(const QString& category);
    QString search() const { return search_; }
    void setSearch(const QString& text);
    QVariantList patches() const { return patches_; }
    QString currentPatchId() const { return patchId_; }
    QString kind() const { return kind_; }
    QStringList problems() const;
    int patchCount() const;
    // The id of the patch `step` places after (or before) the current one in the shown list, without wrapping; with
    // no current patch in the list: the first (step > 0) or the last. Empty when the list is empty.
    Q_INVOKABLE QString neighbour(int step) const;

signals:
    void changed();

private:
    void rebuild();
    const lpc::PatchLibrary* library_ = nullptr;
    QString kind_, patchId_, category_, search_;
    QStringList categories_;
    QVariantList patches_;
};

}  // namespace jad
