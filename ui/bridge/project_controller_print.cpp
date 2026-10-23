// File > Print… and Page Setup…: the project is printed to a PDF file (QPdfWriter, no printer dialog of the system needed): the arrangement (a bar for every region
// over the bars of the project), then a table of the tracks and the list of the markers.
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSettings>

#include <algorithm>
#include <cmath>

#include "bridge/project_controller.h"

namespace jad {
namespace {

QColor trackColor(const QString& name) {
    static const QMap<QString, QColor> colors{{"purple", "#8b5cf6"}, {"indigo", "#5b6bd6"}, {"blue", "#3b82f6"},  {"teal", "#14b8a6"},   {"green", "#30d158"},
                                             {"yellow", "#e6c229"}, {"orange", "#ff9f0a"}, {"red", "#ff453a"},    {"pink", "#ff5fa2"},   {"magenta", "#d946ef"}};
    if (name.startsWith(QLatin1Char('#'))) return QColor(name);
    return colors.value(name, QColor("#8b5cf6"));
}

QPageSize::PageSizeId pageSizeOf(const QString& name) {
    if (name == QLatin1String("Letter")) return QPageSize::Letter;
    if (name == QLatin1String("Legal")) return QPageSize::Legal;
    if (name == QLatin1String("A3")) return QPageSize::A3;
    return QPageSize::A4;
}

}  // namespace

QString ProjectController::printPaper() const { return QSettings().value("print/paper", "A4").toString(); }
bool ProjectController::printLandscape() const { return QSettings().value("print/landscape", true).toBool(); }

void ProjectController::setPrintPaper(const QString& paper) {
    if (paper != QLatin1String("A4") && paper != QLatin1String("A3") && paper != QLatin1String("Letter") && paper != QLatin1String("Legal")) return;
    QSettings().setValue("print/paper", paper);
    emit printSetupChanged();
}

void ProjectController::setPrintLandscape(bool on) {
    QSettings().setValue("print/landscape", on);
    emit printSetupChanged();
}

bool ProjectController::printProject(const QUrl& file) {
    if (!host_) {
        emit notice("There is no project to print");
        return false;
    }
    QString path = file.toLocalFile();
    if (!path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) path += QStringLiteral(".pdf");
    QPdfWriter writer(path);
    writer.setResolution(300);
    writer.setCreator(QStringLiteral("JAD Daw"));
    writer.setTitle(name_);
    writer.setPageSize(QPageSize(pageSizeOf(printPaper())));
    writer.setPageOrientation(printLandscape() ? QPageLayout::Landscape : QPageLayout::Portrait);
    writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
    QPainter p(&writer);
    if (!p.isActive()) {
        setError("Cannot write " + path);
        return false;
    }
    const QRectF page(0, 0, writer.width(), writer.height());
    const double mm = writer.resolution() / 25.4;
    const QString family = QFont().family();
    auto font = [&](double points, bool bold = false) {
        QFont f(family);
        f.setPointSizeF(points);
        f.setBold(bold);
        return f;
    };

    // the tracks that are shown, in order, and how long the project is
    std::vector<const TrackRow*> tracks;
    for (const TrackRow& t : allRows_)
        if (!t.master && t.showInTracks) tracks.push_back(&t);
    const double endBeats = std::max(16.0, std::ceil(projectEndBeats() / barBeats()) * barBeats());
    const double bar = barBeats();
    const int totalBars = static_cast<int>(std::ceil(endBeats / bar));

    int pageNumber = 0;
    auto header = [&] {
        ++pageNumber;
        p.setPen(Qt::black);
        p.setFont(font(20, true));
        p.drawText(QRectF(0, 0, page.width() * 0.7, 9 * mm), Qt::AlignLeft | Qt::AlignVCenter, name_.isEmpty() ? QStringLiteral("Untitled") : name_);
        p.setFont(font(9));
        p.setPen(QColor("#555555"));
        p.drawText(QRectF(page.width() * 0.6, 0, page.width() * 0.4, 9 * mm), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1 bpm   %2   %3   page %4").arg(bpm_, 0, 'f', 1).arg(signatureText(), key_).arg(pageNumber));
        p.setPen(QPen(QColor("#999999"), 1));
        p.drawLine(QPointF(0, 10 * mm), QPointF(page.width(), 10 * mm));
    };

    // the arrangement: names on the left, a bar for every region, the bar numbers over them
    const double nameWidth = 38 * mm, rowHeight = 9 * mm, top0 = 22 * mm;
    const double gridLeft = nameWidth, gridWidth = page.width() - nameWidth;
    const int rowsPerPage = std::max(1, static_cast<int>((page.height() - top0 - 6 * mm) / rowHeight));
    const int step = std::max(1, static_cast<int>(std::ceil(totalBars / (gridWidth / (14 * mm)))));  // a number every `step` bars, not closer than 14 mm
    for (std::size_t first = 0; first < std::max<std::size_t>(1, tracks.size()); first += static_cast<std::size_t>(rowsPerPage)) {
        if (first > 0) writer.newPage();
        header();
        p.setFont(font(8));
        p.setPen(QColor("#555555"));
        for (int b = 0; b <= totalBars; b += step) {
            const double x = gridLeft + gridWidth * (b * bar) / endBeats;
            p.drawText(QRectF(x - 6 * mm, top0 - 7 * mm, 12 * mm, 5 * mm), Qt::AlignCenter, QString::number(b + 1));
            p.setPen(QPen(QColor("#dddddd"), 1));
            p.drawLine(QPointF(x, top0 - 2 * mm), QPointF(x, top0 + std::min<std::size_t>(rowsPerPage, tracks.size() - first) * rowHeight));
            p.setPen(QColor("#555555"));
        }
        const std::size_t last = std::min(tracks.size(), first + static_cast<std::size_t>(rowsPerPage));
        for (std::size_t i = first; i < last; ++i) {
            const TrackRow& t = *tracks[i];
            const double y = top0 + static_cast<double>(i - first) * rowHeight;
            p.setPen(Qt::black);
            p.setFont(font(9));
            p.drawText(QRectF(0, y, nameWidth - 2 * mm, rowHeight), Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(t.name, Qt::ElideRight, static_cast<int>(nameWidth - 3 * mm)));
            p.setPen(QPen(QColor("#eeeeee"), 1));
            p.drawLine(QPointF(0, y + rowHeight), QPointF(page.width(), y + rowHeight));
            const QColor c = trackColor(t.color);
            for (const RegionRow& r : regionRows_) {
                if (r.trackId != t.id) continue;
                const double x0 = gridLeft + gridWidth * r.startBeats / endBeats, x1 = gridLeft + gridWidth * (r.startBeats + r.lengthBeats) / endBeats;
                p.setPen(QPen(c.darker(140), 1));
                QColor fill = c;
                fill.setAlphaF(r.muted ? 0.25 : 0.6);
                p.setBrush(fill);
                p.drawRoundedRect(QRectF(x0, y + 1.2 * mm, std::max(0.5 * mm, x1 - x0), rowHeight - 2.4 * mm), 1 * mm, 1 * mm);
                p.setBrush(Qt::NoBrush);
            }
        }
        // the markers over the grid
        p.setPen(QColor("#d4380d"));
        p.setFont(font(7));
        for (const MarkerRow& m : markerRows_) {
            const double x = gridLeft + gridWidth * m.beats / endBeats;
            p.drawLine(QPointF(x, top0 - 4 * mm), QPointF(x, top0 - 2 * mm));
            p.drawText(QPointF(x + 0.6 * mm, top0 - 9 * mm), m.name);
        }
    }

    // the table of the tracks
    writer.newPage();
    header();
    p.setPen(Qt::black);
    p.setFont(font(12, true));
    p.drawText(QPointF(0, 20 * mm), QStringLiteral("Tracks"));
    const QStringList heads{"Name", "Kind", "Regions", "Volume", "Pan", "Mode"};
    const std::array<double, 6> xs{0, 70, 105, 130, 155, 175};
    double y = 28 * mm;
    p.setFont(font(8, true));
    for (std::size_t c = 0; c < heads.size(); ++c) p.drawText(QPointF(xs[c] * mm, y), heads[static_cast<int>(c)]);
    p.setFont(font(8));
    for (const TrackRow* t : tracks) {
        y += 5.5 * mm;
        if (y > page.height() - 8 * mm) {
            writer.newPage();
            header();
            y = 22 * mm;
        }
        const QStringList cells{t->name, t->kind, QString::number(t->regionCount), QStringLiteral("%1 dB").arg(t->gainDb, 0, 'f', 1), QStringLiteral("%1").arg(t->pan, 0, 'f', 2), t->automationMode};
        for (std::size_t c = 0; c < cells.size(); ++c) p.drawText(QPointF(xs[c] * mm, y), c == 0 ? p.fontMetrics().elidedText(cells[static_cast<int>(c)], Qt::ElideRight, static_cast<int>(66 * mm)) : cells[static_cast<int>(c)]);
    }
    p.end();
    emit notice("Printed to " + path);
    return true;
}

}  // namespace jad
