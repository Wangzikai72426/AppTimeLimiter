#ifndef HISTORYDIALOG_H
#define HISTORYDIALOG_H

#include <QDialog>
#include <QWidget>
#include <QMap>
#include <QString>
#include <QDate>
#include <QLabel>
#include <QPushButton>
#include "AppMonitor.h"

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QPieSeries>

// History dialog: shows a daily usage pie chart (Qt Charts).
// Per-slice details (app name + percentage + time) are drawn on the slices
// themselves; at most 8 slices are shown, the remainder is folded into "其他".
class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HistoryDialog(AppMonitor* monitor, QWidget* parent = nullptr);

private slots:
    void onPrevDay();
    void onNextDay();

private:
    void setupUI();
    void loadDay(const QDate& date);
    QPieSeries* buildSeries(const QMap<QString, int>& data);

    AppMonitor* m_monitor;
    QDate m_currentDate;

    QLabel* m_dateLabel;
    QLabel* m_totalLabel;
    QChartView* m_chartView;
    QChart* m_chart;
    QPushButton* m_prevBtn;
    QPushButton* m_nextBtn;
};

#endif // HISTORYDIALOG_H
