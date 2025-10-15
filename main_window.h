#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QTimer>
#include <QMenu>
#include <QThread>
#include <QDateTime>
#include <QMainWindow>
#include <QCloseEvent>
#include <QSystemTrayIcon>
#include <QStackedWidget>
#include <QToolBar>
#include <QActionGroup>
#include <QGraphicsSimpleTextItem>

#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegendMarker>

#include "data_collector.h"
#include "database_manager.h"
#include "draggable_chart_view.h"
#include "dns_collector.h"
#include "dns_page.h"

QT_USE_NAMESPACE

struct interface_series
{
    QLineSeries* upload = nullptr;
    QLineSeries* download = nullptr;
    QLegendMarker* marker = nullptr;
    interface_stats last_stats;
};

class main_window : public QMainWindow
{
    Q_OBJECT

   public:
    explicit main_window(QWidget* parent = nullptr);
    ~main_window() override;

   protected:
    void closeEvent(QCloseEvent* event) override;

   signals:
    void initial_data_load_requested();
    void request_add_snapshots(const QList<interface_stats>& stats_list, const QDateTime& timestamp);
    void request_snapshots_in_range(quint64 request_id, const QString& interface_name, const QDateTime& start, const QDateTime& end);
    void start_collector_timer(int interval_ms);

    void request_add_dns_log(const dns_query_info& info);
    void start_dns_capture();
    void request_qps_stats_from_db(quint64 request_id, const QDateTime& start, const QDateTime& end, int interval_secs);
    void request_all_domains_from_db(quint64 request_id, const QDateTime& start, const QDateTime& end);
    void request_dns_details_from_db(quint64 request_id, const QString& domain, const QDateTime& start, const QDateTime& end);

   private slots:
    void handle_stats_collected(const QList<interface_stats>& stats, const QDateTime& timestamp);
    void handle_snapshots_loaded(quint64 request_id, const QString& interface_name, const QList<traffic_point>& snapshots);
    void handle_series_hovered(const QPointF& point, bool state);
    void handle_dns_packet_collected(const dns_query_info& info);

    void handle_dns_page_qps_request(quint64 request_id, const QDateTime& start, const QDateTime& end, int interval_secs);
    void handle_dns_page_all_domains_request(quint64 request_id, const QDateTime& start, const QDateTime& end);
    void handle_dns_page_details_request(quint64 request_id, const QString& domain, const QDateTime& start, const QDateTime& end);

    void toggle_series_visibility(const QString& name);
    void snap_back_to_live_view();
    void process_new_interfaces();
    void on_interaction_started();
    void on_interaction_finished();
    void on_tray_icon_activated(QSystemTrayIcon::ActivationReason reason);
    void quit_application();
    void on_view_changed(QAction* action);
    void on_database_ready();

   private:
    void setup_chart();
    void setup_toolbar();
    void setup_workers();
    void add_series_for_interface(const QString& interface_name);
    void update_x_axis(const QDateTime& start, const QDateTime& end);
    void rescale_y_axis();
    void update_all_visuals();
    void load_data_for_display(const QDateTime& start, const QDateTime& end);
    void setup_tray_icon();
    void process_loaded_data_batch();
    void append_live_data_point(const interface_stats& current_stats, const QDateTime& timestamp);
    void transition_to_live_view();

   private:
    QStackedWidget* central_stacked_widget_ = nullptr;
    dns_page* dns_page_ = nullptr;
    QToolBar* main_toolbar_ = nullptr;
    QAction* net_action_ = nullptr;
    QAction* dns_action_ = nullptr;
    QActionGroup* view_action_group_ = nullptr;
    QChart* chart_ = nullptr;
    draggable_chartview* chart_view_ = nullptr;
    QDateTimeAxis* axis_x_ = nullptr;
    QValueAxis* axis_y_ = nullptr;
    QMap<QString, interface_series> series_map_;
    QList<QColor> color_palette_;
    int color_index_ = 0;
    QGraphicsSimpleTextItem* tooltip_ = nullptr;
    QDateTime first_timestamp_;
    QString isolated_interface_name_;
    QTimer* snap_back_timer_ = nullptr;
    bool is_manual_view_active_ = false;
    QStringList new_interfaces_queue_;
    QThread* data_collector_thread_ = nullptr;
    QThread* db_manager_thread_ = nullptr;
    QThread* dns_collector_thread_ = nullptr;
    data_collector* data_collector_ = nullptr;
    database_manager* db_manager_ = nullptr;
    dns_collector* dns_collector_ = nullptr;
    quint64 current_load_request_id_ = 0;
    qint64 pending_queries_count_ = 0;
    QDateTime loaded_data_start_time_;
    QSystemTrayIcon* tray_icon_ = nullptr;
    QMenu* tray_menu_ = nullptr;
    QAction* show_hide_action_ = nullptr;
    QAction* quit_action_ = nullptr;
};

#endif
