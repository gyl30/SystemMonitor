#ifndef DNS_PAGE_H
#define DNS_PAGE_H

#include <QWidget>
#include <QList>
#include <QPointF>
#include <QPair>
#include <QMap>
#include <QDateTime>
#include <QTableView>
#include <QSplitter>
#include <QModelIndex>
#include "draggable_chart_view.h"
#include "dns_query_info.h"

class QChart;
class QTimer;
class QLineSeries;
class QDateTimeAxis;
class QValueAxis;
class QStandardItemModel;
class QGraphicsSimpleTextItem;

class dns_page : public QWidget
{
    Q_OBJECT

   public:
    explicit dns_page(QWidget* parent = nullptr);

   signals:
    void request_qps_stats(quint64 request_id, const QDateTime& start, const QDateTime& end, int interval_secs);
    void request_all_domains(quint64 request_id, const QDateTime& start, const QDateTime& end);
    void request_dns_details_for_domain(quint64 request_id, const QString& domain, const QDateTime& start, const QDateTime& end);

   public slots:
    void handle_qps_stats_ready(quint64 request_id, const QList<QPointF>& data);
    void handle_all_domains_ready(quint64 request_id, const QStringList& domains);
    void handle_dns_details_ready(quint64 request_id, const QList<dns_query_info>& details);
    void trigger_initial_load();

   private slots:
    void on_refresh_timer_timeout();
    void on_interaction_started();
    void on_interaction_finished();
    void snap_back_to_live_view();
    void handle_series_hovered(const QPointF& point, bool state);
    void on_domain_selected(const QModelIndex& current, const QModelIndex& previous);

   private:
    void setup_ui();
    void setup_chart();
    void update_chart_axes(const QDateTime& start, const QDateTime& end);
    void request_data_for_current_view();

   private:
    draggable_chartview* chart_view_ = nullptr;
    QChart* chart_ = nullptr;
    QLineSeries* qps_series_ = nullptr;
    QDateTimeAxis* axis_x_ = nullptr;
    QValueAxis* axis_y_ = nullptr;
    QGraphicsSimpleTextItem* tooltip_ = nullptr;

    QSplitter* splitter_ = nullptr;

    QTableView* all_domains_view_ = nullptr;
    QStandardItemModel* all_domains_model_ = nullptr;

    QTableView* domain_details_view_ = nullptr;
    QStandardItemModel* domain_details_model_ = nullptr;

    QTimer* refresh_timer_ = nullptr;
    QTimer* snap_back_timer_ = nullptr;
    quint64 current_request_id_ = 0;
    quint64 current_details_request_id_ = 0;
    bool drag_enabled_ = false;
    bool is_manual_view_active_ = false;
    QDateTime first_timestamp_;
};
#endif
