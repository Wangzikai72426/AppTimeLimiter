#include "HistoryDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDate>
#include <QFont>
#include <QColor>
#include <QPainter>
#include <algorithm>
#include <QPair>
#include <QStringLiteral>

#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QtCharts/QChartView>
#include <QEasingCurve>
#include <cmath>

// ==================== Helpers ====================

static QString formatTime(int secs)
{
    int hh = secs / 3600;
    int mm = (secs % 3600) / 60;
    int ss = secs % 60;
    return (hh > 0)
        ? QStringLiteral("%1\u65f6%2\u5206").arg(hh).arg(mm)
        : QStringLiteral("%1\u5206%2\u79d2").arg(mm).arg(ss);
}

// ==================== Custom pie view with label collision avoidance ====================
//
// Qt Charts' built-in LabelOutside does NOT avoid overlaps: tiny adjacent slices
// get their labels stacked on top of each other. This view hides the built-in
// labels and draws its own, using the classic "two side columns" layout:
//   - each slice's label is pushed to the left/right column based on its angle
//   - labels on the same side are sorted by their pie-edge Y and forced to keep
//     a minimum vertical gap, so they never overlap.
// Redrawn on every paintEvent, so it stays correct after resize.

class PieLabelChartView : public QChartView {
public:
    explicit PieLabelChartView(QChart* chart, QWidget* parent = nullptr)
        : QChartView(chart, parent) {}

protected:
    void paintEvent(QPaintEvent* event) override {
        QChartView::paintEvent(event);
        drawLabels();
    }

private:
    void drawLabels() {
        QChart* ch = chart();
        if (!ch || ch->series().isEmpty())
            return;
        QPieSeries* ps = qobject_cast<QPieSeries*>(ch->series().first());
        if (!ps || ps->count() == 0)
            return;

        QRectF pa = ch->plotArea();
        if (pa.isEmpty())
            return;

        static const double PI = 3.14159265358979323846;
        const qreal R = (qMin(pa.width(), pa.height()) / 2.0) * ps->pieSize();
        const QPointF C(pa.x() + pa.width() * ps->horizontalPosition(),
                        pa.y() + pa.height() * ps->verticalPosition());

        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing);

        QFontMetricsF fm(painter.font());
        const qreal lineH = fm.height();
        const qreal minGap = lineH * 2 + 6;   // two-line labels + padding
        const qreal arm = 18;

        struct Item { qreal ey; QPointF edge; QColor color; QString label; };
        QList<Item> left, right;

        const QPointF Cv = mapFromScene(C);
        for (QPieSlice* s : ps->slices()) {
            const qreal a = s->startAngle() + s->angleSpan() / 2.0;   // degrees, 0=top, CW
            const double rad = a * PI / 180.0;
            const QPointF eScene = C + QPointF(R * std::sin(rad), -R * std::cos(rad));
            const QPointF e = mapFromScene(eScene);
            Item it;
            it.ey = e.y();
            it.edge = e;
            it.color = s->color();
            it.label = s->label();
            (e.x() < Cv.x() ? left : right).append(it);
        }
        std::sort(left.begin(), left.end(),
                  [](const Item& x, const Item& y) { return x.ey < y.ey; });
        std::sort(right.begin(), right.end(),
                  [](const Item& x, const Item& y) { return x.ey < y.ey; });

        const QPointF Rv = mapFromScene(C + QPointF(R, 0));
        const qreal Rview = QLineF(Cv, Rv).length();
        const qreal top = Cv.y() - Rview;
        const qreal bot = Cv.y() + Rview;

        auto place = [&](QList<Item>& items, bool isLeft) {
            qreal lastY = -1e9;
            for (Item& it : items) {
                qreal ly = it.ey;
                if (ly < lastY + minGap) ly = lastY + minGap;   // enforce spacing
                lastY = ly;
                if (ly < top) ly = top;
                if (ly > bot) ly = bot;

                const qreal anchorX = isLeft ? (Cv.x() - Rview - arm) : (Cv.x() + Rview + arm);
                const QPointF anchor(anchorX, ly);

                painter.setPen(QPen(it.color, 1.2));
                painter.drawLine(it.edge, anchor);

                QStringList lines = it.label.split('\n');
                const qreal blockH = lineH * lines.size();
                const qreal y0 = ly - blockH / 2.0;
                painter.setPen(QColor("#333333"));
                for (int i = 0; i < lines.size(); ++i) {
                    const qreal ty = y0 + i * lineH;
                    const qreal w = fm.horizontalAdvance(lines[i]);
                    if (isLeft)
                        painter.drawText(QRectF(anchorX - w, ty, w, lineH),
                                        Qt::AlignRight | Qt::AlignVCenter, lines[i]);
                    else
                        painter.drawText(QRectF(anchorX, ty, w, lineH),
                                        Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
                }
            }
        };

        place(left, true);
        place(right, false);
    }
};

// ==================== HistoryDialog ====================

HistoryDialog::HistoryDialog(AppMonitor* monitor, QWidget* parent)
    : QDialog(parent)
    , m_monitor(monitor)
    , m_currentDate(QDate::currentDate())
{
    setupUI();
    loadDay(m_currentDate);
}

void HistoryDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u5386\u53f2\u8bb0\u5f55"));
    resize(720, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // Top two lines: date + total usage time
    m_dateLabel = new QLabel(this);
    m_dateLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    m_totalLabel = new QLabel(this);
    m_totalLabel->setStyleSheet("font-size: 14px; color: #555;");

    mainLayout->addWidget(m_dateLabel);
    mainLayout->addWidget(m_totalLabel);

    // Middle: left arrow + pie chart + right arrow
    QHBoxLayout* navLayout = new QHBoxLayout();
    navLayout->setSpacing(8);

    m_prevBtn = new QPushButton(QStringLiteral("\u25c0"), this);
    m_prevBtn->setFixedSize(40, 80);
    m_prevBtn->setStyleSheet("font-size: 20px;");
    connect(m_prevBtn, &QPushButton::clicked, this, &HistoryDialog::onPrevDay);

    m_chart = new QChart();
    // SeriesAnimations: when a new day's series is added (on open / prev / next)
    // the slices animate outward from the center. One-line change, no extra code.
    m_chart->setAnimationOptions(QChart::SeriesAnimations);
    // OutBounce: slices "bounce" into place like a ball landing. Requires the
    // full QEasingCurve definition (hence the #include <QEasingCurve> above).
    m_chart->setAnimationDuration(800);
    m_chart->setAnimationEasingCurve(QEasingCurve::OutBack);
    m_chart->legend()->hide();
    m_chartView = new PieLabelChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setMinimumHeight(420);

    m_nextBtn = new QPushButton(QStringLiteral("\u25b6"), this);
    m_nextBtn->setFixedSize(40, 80);
    m_nextBtn->setStyleSheet("font-size: 20px;");
    connect(m_nextBtn, &QPushButton::clicked, this, &HistoryDialog::onNextDay);

    navLayout->addWidget(m_prevBtn);
    navLayout->addWidget(m_chartView, 1);
    navLayout->addWidget(m_nextBtn);
    mainLayout->addLayout(navLayout);

    // Bottom close button
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton* closeBtn = new QPushButton(QStringLiteral("\u5173\u95ed"), this);
    closeBtn->setFixedWidth(100);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);
}

QPieSeries* HistoryDialog::buildSeries(const QMap<QString, int>& data)
{
    QPieSeries* series = new QPieSeries();

    // Sort by usage time descending
    QList<QPair<QString, int>> sorted;
    for (auto it = data.begin(); it != data.end(); ++it)
        sorted.append(qMakePair(it.key(), it.value()));
    std::sort(sorted.begin(), sorted.end(),
        [](const QPair<QString, int>& a, const QPair<QString, int>& b) {
            return a.second > b.second;
        });

    int n = sorted.size();
    if (n == 0) return series;

    int total = 0;
    for (const auto& p : sorted) total += p.second;
    if (total <= 0) return series;

    // Cap at 8 slices: when there are more than 8 apps, keep the top 7 and
    // fold everything else into a single "其他" slice (the 8th).
    const int MAX_SLICES = 8;
    int individual;
    bool hasOthers;
    if (n <= MAX_SLICES) {
        individual = n;
        hasOthers = false;
    } else {
        individual = MAX_SLICES - 1;
        hasOthers = true;
    }

    // Distinct, pleasant colors; cycle if there are more than colors.
    QStringList palette = QStringList()
        << "#5B8FF9" << "#61DDAA" << "#65789B" << "#F6BD16" << "#7262FD"
        << "#78D3F8" << "#9661BC" << "#F6903D" << "#008685" << "#F08BB4"
        << "#FF99C3" << "#3BA0FF" << "#CCE2FF" << "#C9CED6";

    int othersSum = 0;
    for (int i = 0; i < n; ++i) {
        const auto& p = sorted[i];
        if (i < individual) {
            QString name = m_monitor ? m_monitor->getFriendlyAppName(p.first) : p.first;
            double pct = (double)p.second / (double)total * 100.0;

            QPieSlice* slice = new QPieSlice();
            slice->setValue(p.second);
            // Label text is rendered by PieLabelChartView (custom leader-line layout
            // with collision avoidance); built-in labels are hidden.
            slice->setLabel(QStringLiteral("%1\n%2%  %3")
                .arg(name).arg(pct, 0, 'f', 1).arg(formatTime(p.second)));
            slice->setColor(QColor(palette[i % palette.size()]));
            series->append(slice);
        } else {
            othersSum += p.second;
        }
    }

    if (hasOthers && othersSum > 0) {
        double pct = (double)othersSum / (double)total * 100.0;
        QPieSlice* slice = new QPieSlice();
        slice->setValue(othersSum);
        slice->setLabel(QStringLiteral("\u5176\u4ed6\n%1%  %2")
            .arg(pct, 0, 'f', 1).arg(formatTime(othersSum)));
        slice->setColor(QColor("#C9CED6"));
        series->append(slice);
    }

    // Hide Qt's built-in labels; PieLabelChartView draws them with avoidance.
    series->setLabelsVisible(false);
    return series;
}

void HistoryDialog::loadDay(const QDate& date)
{
    QString dateStr = date.toString("yyyy-MM-dd");
    QMap<QString, int> data = m_monitor->getHistoryByDate(dateStr);

    // Total usage time
    int totalSecs = 0;
    for (auto it = data.begin(); it != data.end(); ++it) {
        totalSecs += it.value();
    }
    int hh = totalSecs / 3600;
    int mm = (totalSecs % 3600) / 60;
    int ss = totalSecs % 60;

    m_dateLabel->setText(QStringLiteral("\u65e5\u671f\uff1a%1").arg(dateStr));
    m_totalLabel->setText(QStringLiteral("\u603b\u4f7f\u7528\u65f6\u95f4\uff1a%1\u5c0f\u65f6%2\u5206%3\u79d2")
        .arg(hh).arg(mm).arg(ss));

    // Rebuild the chart contents (remove previous series first)
    m_chart->removeAllSeries();
    if (data.isEmpty() || totalSecs <= 0) {
        m_chart->setTitle(QStringLiteral("\u65e0\u4f7f\u7528\u8bb0\u5f55"));
    } else {
        m_chart->setTitle("");
        m_chart->addSeries(buildSeries(data));
    }

    // Navigation: disable next (cannot go to future); prev always enabled
    m_nextBtn->setEnabled(date < QDate::currentDate());
}

void HistoryDialog::onPrevDay()
{
    m_currentDate = m_currentDate.addDays(-1);
    loadDay(m_currentDate);
}

void HistoryDialog::onNextDay()
{
    QDate today = QDate::currentDate();
    if (m_currentDate < today) {
        m_currentDate = m_currentDate.addDays(1);
        loadDay(m_currentDate);
    }
}
