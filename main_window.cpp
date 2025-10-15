#include <QLegendMarker>
#include <QGraphicsLayout>
#include <QDir>
#include <QAction>
#include <QApplication>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QStackedWidget>
#include <QToolBar>
#include <QLabel>

#include <algorithm>

#include "log.h"
#include "main_window.h"

static constexpr int kMaxDataGapSeconds = 5;
static constexpr int kDataBufferFactor = 2;
static constexpr int kVisibleWindowMinutes = 15L;
static constexpr int kSnapBackTimeoutMs = 5000;
static constexpr int kCollectionIntervalMs = 1000;
static QPair<double, double> calculate_traffic_speeds(qint64 prev_timestamp_ms,
                                                      quint64 prev_bytes_sent,
                                                      quint64 prev_bytes_received,
                                                      qint64 curr_timestamp_ms,
                                                      quint64 curr_bytes_sent,
                                                      quint64 curr_bytes_received)
{
    double interval_seconds = static_cast<double>(curr_timestamp_ms - prev_timestamp_ms) / 1000.0;
    if (interval_seconds <= 0)
    {
        return {0.0, 0.0};
    }

    quint64 sent_diff = (curr_bytes_sent >= prev_bytes_sent) ? (curr_bytes_sent - prev_bytes_sent) : curr_bytes_sent;
    quint64 recv_diff = (curr_bytes_received >= prev_bytes_received) ? (curr_bytes_received - prev_bytes_received) : curr_bytes_received;

    double upload_speed_kb = (static_cast<double>(sent_diff) / interval_seconds) / 1024.0;
    double download_speed_kb = (static_cast<double>(recv_diff) / interval_seconds) / 1024.0;

    return {upload_speed_kb, download_speed_kb};
}
main_window::main_window(QWidget* parent) : QMainWindow(parent), snap_back_timer_(new QTimer(this))
{
    setup_chart();
    setup_toolbar();

    dns_page_ = new dns_page(this);
    connect(dns_page_, &dns_page::request_qps_stats, this, &main_window::handle_dns_page_qps_request);
    connect(dns_page_, &dns_page::request_all_domains, this, &main_window::handle_dns_page_all_domains_request);
    connect(dns_page_, &dns_page::request_dns_details_for_domain, this, &main_window::handle_dns_page_details_request);
    connect(this, &main_window::initial_data_load_requested, dns_page_, &dns_page::trigger_initial_load);

    central_stacked_widget_ = new QStackedWidget(this);
    central_stacked_widget_->addWidget(chart_view_);
    central_stacked_widget_->addWidget(dns_page_);

    setCentralWidget(central_stacked_widget_);
    setWindowTitle("网络与DNS监视器");
    resize(1024, 768);

    setup_workers();
    setup_tray_icon();
    tray_icon_->show();

    snap_back_timer_->setSingleShot(true);
    connect(snap_back_timer_, &QTimer::timeout, this, &main_window::snap_back_to_live_view);

    LOG_INFO("connecting to draggable chartview signals");
    connect(chart_view_, &draggable_chartview::interaction_started, this, &main_window::on_interaction_started);
    connect(chart_view_, &draggable_chartview::view_changed_by_drag, this, &main_window::on_interaction_finished);

    color_palette_ << Qt::blue << Qt::red << Qt::green << Qt::magenta << Qt::cyan << Qt::yellow;

    tooltip_ = new QGraphicsSimpleTextItem(chart_);
    tooltip_->setZValue(10);
    tooltip_->setBrush(Qt::white);
    tooltip_->setPen(QPen(Qt::black));
    tooltip_->hide();
}

main_window::~main_window()
{
    LOG_INFO("main window destructor called cleaning up threads");
    if (dns_collector_thread_ != nullptr && dns_collector_thread_->isRunning())
    {
        dns_collector_thread_->requestInterruption();
        dns_collector_thread_->quit();
        dns_collector_thread_->wait(1000);
    }
    if (data_collector_thread_ != nullptr && data_collector_thread_->isRunning())
    {
        data_collector_thread_->quit();
        data_collector_thread_->wait(1000);
    }
    if (db_manager_thread_ != nullptr && db_manager_thread_->isRunning())
    {
        db_manager_thread_->quit();
        db_manager_thread_->wait(1000);
    }
    LOG_INFO("threads cleanup finished");
}

void main_window::setup_toolbar()
{
    main_toolbar_ = new QToolBar(this);
    main_toolbar_->setMovable(false);

    net_action_ = new QAction("NET", this);
    net_action_->setCheckable(true);

    dns_action_ = new QAction("DNS", this);
    dns_action_->setCheckable(true);

    view_action_group_ = new QActionGroup(this);
    view_action_group_->addAction(net_action_);
    view_action_group_->addAction(dns_action_);

    net_action_->setChecked(true);

    main_toolbar_->addAction(net_action_);
    main_toolbar_->addAction(dns_action_);

    addToolBar(main_toolbar_);

    connect(view_action_group_, &QActionGroup::triggered, this, &main_window::on_view_changed);
}

void main_window::on_view_changed(QAction* action)
{
    if (action == net_action_)
    {
        central_stacked_widget_->setCurrentWidget(chart_view_);
        LOG_INFO("switched to NET view");
    }
    else if (action == dns_action_)
    {
        central_stacked_widget_->setCurrentWidget(dns_page_);
        LOG_INFO("switched to DNS view");
    }
}

void main_window::setup_workers()
{
    db_manager_thread_ = new QThread(this);
    QString db_path = QDir(QApplication::applicationDirPath()).filePath("network_monitor.db");
    db_manager_ = new database_manager(db_path);
    db_manager_->moveToThread(db_manager_thread_);
    connect(this, &main_window::request_add_snapshots, db_manager_, &database_manager::add_snapshots);
    connect(this, &main_window::request_snapshots_in_range, db_manager_, &database_manager::get_snapshots_in_range);
    connect(db_manager_, &database_manager::snapshots_ready, this, &main_window::handle_snapshots_loaded);
    connect(this, &main_window::request_add_dns_log, db_manager_, &database_manager::add_dns_log);
    connect(this, &main_window::request_qps_stats_from_db, db_manager_, &database_manager::get_qps_stats);
    connect(this, &main_window::request_all_domains_from_db, db_manager_, &database_manager::get_all_domains);
    connect(this, &main_window::request_dns_details_from_db, db_manager_, &database_manager::get_dns_details_for_domain);
    connect(db_manager_, &database_manager::qps_stats_ready, dns_page_, &dns_page::handle_qps_stats_ready);
    connect(db_manager_, &database_manager::all_domains_ready, dns_page_, &dns_page::handle_all_domains_ready);
    connect(db_manager_, &database_manager::dns_details_ready, dns_page_, &dns_page::handle_dns_details_ready);
    connect(db_manager_thread_, &QThread::started, db_manager_, &database_manager::initialize);
    connect(db_manager_,
            &database_manager::initialization_failed,
            this,
            [this]()
            {
                QMessageBox::critical(this, "database error", "failed to initialize the database the application will close.");
                QApplication::quit();
            });
    connect(db_manager_, &database_manager::database_ready, this, &main_window::on_database_ready, Qt::QueuedConnection);
    connect(db_manager_thread_, &QThread::finished, db_manager_, &QObject::deleteLater);

    data_collector_thread_ = new QThread(this);
    data_collector_ = new data_collector();
    data_collector_->moveToThread(data_collector_thread_);
    connect(data_collector_, &data_collector::stats_collected, this, &main_window::handle_stats_collected);
    connect(this, &main_window::start_collector_timer, data_collector_, &data_collector::start_collection);
    connect(data_collector_thread_, &QThread::finished, data_collector_, &QObject::deleteLater);

    dns_collector_thread_ = new QThread(this);
    dns_collector_ = new dns_collector();
    dns_collector_->moveToThread(dns_collector_thread_);
    connect(this, &main_window::start_dns_capture, dns_collector_, &dns_collector::start_capture);
    connect(dns_collector_, &dns_collector::dns_packet_collected, this, &main_window::handle_dns_packet_collected, Qt::QueuedConnection);
    connect(dns_collector_thread_, &QThread::finished, dns_collector_, &QObject::deleteLater);

    db_manager_thread_->start();
    data_collector_thread_->start();
    dns_collector_thread_->start();

    emit start_collector_timer(kCollectionIntervalMs);
    emit start_dns_capture();

    LOG_INFO("worker threads started");
}
void main_window::on_database_ready()
{
    LOG_INFO("performing initial data load for live view");
    transition_to_live_view();

    LOG_INFO("received database_ready signal requesting initial data load");
    emit initial_data_load_requested();
}
void main_window::handle_dns_packet_collected(const dns_query_info& info)
{
    LOG_DEBUG("received dns_packet_collected signal for {} forwarding to db manager", info.query_domain.toStdString());
    emit request_add_dns_log(info);
}

void main_window::handle_dns_page_qps_request(quint64 request_id, const QDateTime& start, const QDateTime& end, int interval_secs)
{
    LOG_DEBUG("received request for qps stats from dns_page id {} forwarding to db manager", request_id);
    emit request_qps_stats_from_db(request_id, start, end, interval_secs);
}

void main_window::handle_dns_page_all_domains_request(quint64 request_id, const QDateTime& start, const QDateTime& end)
{
    LOG_DEBUG("received request for all domains from dns_page id {} forwarding to db manager", request_id);
    emit request_all_domains_from_db(request_id, start, end);
}

void main_window::handle_dns_page_details_request(quint64 request_id, const QString& domain, const QDateTime& start, const QDateTime& end)
{
    LOG_DEBUG("received request for dns details for {} from dns_page id {} forwarding to db manager", domain.toStdString(), request_id);
    emit request_dns_details_from_db(request_id, domain, start, end);
}

void main_window::handle_stats_collected(const QList<interface_stats>& stats, const QDateTime& timestamp)
{
    LOG_TRACE("received stats from collector");
    emit request_add_snapshots(stats, timestamp);

    bool new_interface_detected = false;
    for (const auto& stat : stats)
    {
        if (series_map_.contains(stat.name))
        {
            continue;
        }

        if (new_interfaces_queue_.contains(stat.name))
        {
            continue;
        }

        new_interfaces_queue_.append(stat.name);
        new_interface_detected = true;
    }

    if (new_interface_detected)
    {
        QTimer::singleShot(0, this, &main_window::process_new_interfaces);
    }

    if (is_manual_view_active_)
    {
        return;
    }
    for (const auto& stat : stats)
    {
        if (series_map_.contains(stat.name))
        {
            append_live_data_point(stat, timestamp);
        }
    }

    const qint64 visible_window_msecs = kVisibleWindowMinutes * 60L * 1000;
    const QDateTime& end_time = timestamp;
    QDateTime start_time = end_time.addMSecs(-visible_window_msecs);
    update_x_axis(start_time, end_time);
    rescale_y_axis();

    if (!chart_view_->property("dragEnabled").toBool() && first_timestamp_.isValid())
    {
        const qint64 total_duration_seconds = first_timestamp_.secsTo(timestamp);
        const qint64 visible_window_seconds = kVisibleWindowMinutes * 60L;
        if (total_duration_seconds > visible_window_seconds)
        {
            LOG_INFO("sufficient data collected enabling chart dragging");
            chart_view_->set_drag_enabled(true);
            chart_view_->setProperty("dragEnabled", true);
        }
    }
}

void main_window::append_live_data_point(const interface_stats& current_stats, const QDateTime& timestamp)
{
    interface_series& series_pair = series_map_[current_stats.name];
    const interface_stats& previous_stats = series_pair.last_stats;

    if (previous_stats.name.isEmpty())
    {
        series_pair.last_stats = current_stats;
        series_pair.last_stats.timestamp = timestamp;
        return;
    }

    QPair<double, double> speeds = calculate_traffic_speeds(previous_stats.timestamp.toMSecsSinceEpoch(),
                                                            previous_stats.bytes_sent,
                                                            previous_stats.bytes_received,
                                                            timestamp.toMSecsSinceEpoch(),
                                                            current_stats.bytes_sent,
                                                            current_stats.bytes_received);

    double upload_speed_kb = speeds.first;
    double download_speed_kb = speeds.second;

    series_pair.upload->append(static_cast<double>(timestamp.toMSecsSinceEpoch()), upload_speed_kb);
    series_pair.download->append(static_cast<double>(timestamp.toMSecsSinceEpoch()), download_speed_kb);

    const qint64 cutoff_ms = timestamp.addSecs(-kVisibleWindowMinutes * 60L * kDataBufferFactor).toMSecsSinceEpoch();
    while (!series_pair.upload->points().isEmpty() && series_pair.upload->points().first().x() < static_cast<qreal>(cutoff_ms))
    {
        series_pair.upload->remove(0);
    }
    while (!series_pair.download->points().isEmpty() && series_pair.download->points().first().x() < static_cast<qreal>(cutoff_ms))
    {
        series_pair.download->remove(0);
    }

    series_pair.last_stats = current_stats;
    series_pair.last_stats.timestamp = timestamp;
}
void main_window::transition_to_live_view()
{
    if (!is_manual_view_active_ && chart_->title() == "实时网络速度")
    {
        return;
    }
    LOG_INFO("Transitioning to live view mode.");
    is_manual_view_active_ = false;
    chart_->setTitle("实时网络速度");

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 visible_window_msecs = kVisibleWindowMinutes * 60L * 1000;
    const QDateTime start_time = now.addMSecs(-visible_window_msecs);

    load_data_for_display(start_time, now);
}

void main_window::process_new_interfaces()
{
    if (new_interfaces_queue_.isEmpty())
    {
        return;
    }

    for (const QString& interface_name : new_interfaces_queue_)
    {
        add_series_for_interface(interface_name);
    }
    new_interfaces_queue_.clear();

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 visible_window_msecs = kVisibleWindowMinutes * 60L * 1000;
    const QDateTime start_time = now.addMSecs(-visible_window_msecs);
    load_data_for_display(start_time, now);
}

void main_window::on_interaction_started()
{
    LOG_INFO("interaction started pausing live updates");
    if (!is_manual_view_active_)
    {
        is_manual_view_active_ = true;
        chart_->setTitle("网络速度历史视图");
    }
    snap_back_timer_->start(kSnapBackTimeoutMs);
}

void main_window::on_interaction_finished()
{
    LOG_INFO("interaction finished loading data for the new view range");
    load_data_for_display(axis_x_->min(), axis_x_->max());
    snap_back_timer_->start(kSnapBackTimeoutMs);
}

void main_window::snap_back_to_live_view()
{
    LOG_INFO("snapback timer fired resetting to live view");
    transition_to_live_view();
}

void main_window::setup_chart()
{
    chart_ = new QChart();
    chart_->setAnimationOptions(QChart::NoAnimation);
    chart_->layout()->setContentsMargins(0, 0, 0, 0);
    chart_->setBackgroundRoundness(0);
    chart_->setTitle("实时网络速度");
    chart_->legend()->setVisible(true);
    chart_->legend()->setAlignment(Qt::AlignBottom);

    chart_view_ = new draggable_chartview(chart_);

    chart_view_->setRenderHint(QPainter::Antialiasing);
    axis_x_ = new QDateTimeAxis;
    axis_x_->setFormat("hh:mm:ss");
    axis_x_->setTitleText("时间");
    chart_->addAxis(axis_x_, Qt::AlignBottom);
    axis_y_ = new QValueAxis;
    axis_y_->setLabelFormat("%.1f KB/s");
    axis_y_->setTitleText("速度");
    axis_y_->setMinorTickCount(4);
    axis_y_->setRange(0, 100.0);
    chart_->addAxis(axis_y_, Qt::AlignLeft);
}

void main_window::add_series_for_interface(const QString& interface_name)
{
    if (series_map_.contains(interface_name))
    {
        return;
    }
    LOG_INFO("adding new series for interface {}", interface_name.toStdString());

    auto* upload_series = new QLineSeries();
    auto* download_series = new QLineSeries();
    download_series->setName(interface_name);
    upload_series->setName(interface_name + " 上传");
    QColor base_color = color_palette_[color_index_ % color_palette_.size()];
    color_index_++;
    QPen download_pen(base_color);
    download_pen.setWidth(2);
    download_series->setPen(download_pen);
    QPen upload_pen(base_color.lighter(130));
    upload_pen.setWidth(2);
    upload_pen.setStyle(Qt::DashLine);
    upload_series->setPen(upload_pen);

    chart_->addSeries(upload_series);
    chart_->addSeries(download_series);

    upload_series->attachAxis(axis_x_);
    upload_series->attachAxis(axis_y_);
    download_series->attachAxis(axis_x_);
    download_series->attachAxis(axis_y_);

    series_map_[interface_name].upload = upload_series;
    series_map_[interface_name].download = download_series;
    auto* legend = chart_->legend();
    for (QLegendMarker* marker : legend->markers(upload_series))
    {
        marker->setVisible(false);
    }
    for (QLegendMarker* marker : legend->markers(download_series))
    {
        series_map_[interface_name].marker = marker;
        connect(marker, &QLegendMarker::clicked, this, [this, interface_name]() { toggle_series_visibility(interface_name); });
    }
    connect(upload_series, &QLineSeries::clicked, this, [this, interface_name]() { toggle_series_visibility(interface_name); });
    connect(download_series, &QLineSeries::clicked, this, [this, interface_name]() { toggle_series_visibility(interface_name); });
    connect(upload_series, &QLineSeries::hovered, this, &main_window::handle_series_hovered);
    connect(download_series, &QLineSeries::hovered, this, &main_window::handle_series_hovered);
}

void main_window::load_data_for_display(const QDateTime& start, const QDateTime& end)
{
    if (series_map_.isEmpty())
    {
        if (new_interfaces_queue_.isEmpty() && first_timestamp_.isNull())
        {
        }
        else if (series_map_.isEmpty())
        {
            return;
        }
    }

    current_load_request_id_++;
    LOG_DEBUG("requesting data load with id {} for range {} {}",
              current_load_request_id_,
              start.toString("hh:mm:ss").toStdString(),
              end.toString("hh:mm:ss").toStdString());

    pending_queries_count_ = series_map_.size();
    if (pending_queries_count_ == 0)
    {
        return;
    }

    loaded_data_start_time_ = end;

    for (const QString& name : series_map_.keys())
    {
        emit request_snapshots_in_range(current_load_request_id_, name, start, end);
    }
}

void main_window::handle_snapshots_loaded(quint64 request_id, const QString& interface_name, const QList<traffic_point>& snapshots)
{
    if (request_id != current_load_request_id_)
    {
        LOG_DEBUG("ignoring stale data req id {} for interface {} for current req id {}",
                  request_id,
                  interface_name.toStdString(),
                  current_load_request_id_);
        return;
    }

    if (!series_map_.contains(interface_name))
    {
        pending_queries_count_--;
        if (pending_queries_count_ <= 0)
        {
            process_loaded_data_batch();
        }
        return;
    }
    LOG_TRACE("received snapshot data for {}", interface_name.toStdString());

    QList<traffic_point> sorted_snapshots = snapshots;
    std::sort(sorted_snapshots.begin(),
              sorted_snapshots.end(),
              [](const traffic_point& a, const traffic_point& b) { return a.timestamp_ms < b.timestamp_ms; });

    QVector<QPointF> upload_points;
    QVector<QPointF> download_points;
    if (sorted_snapshots.size() >= 2)
    {
        if (!first_timestamp_.isValid())
        {
            first_timestamp_ = QDateTime::fromMSecsSinceEpoch(sorted_snapshots[1].timestamp_ms);
        }

        upload_points.reserve(sorted_snapshots.size() * 2);
        download_points.reserve(sorted_snapshots.size() * 2);

        for (int i = 1; i < sorted_snapshots.size(); ++i)
        {
            const auto& current = sorted_snapshots[i];
            const auto& previous = sorted_snapshots[i - 1];
            double interval_seconds = static_cast<double>(current.timestamp_ms - previous.timestamp_ms) / 1000.0;

            if (interval_seconds > kMaxDataGapSeconds)
            {
                upload_points.append(QPointF(static_cast<double>(previous.timestamp_ms + 1), 0.0));
                download_points.append(QPointF(static_cast<double>(previous.timestamp_ms + 1), 0.0));
                upload_points.append(QPointF(static_cast<double>(current.timestamp_ms - 1), 0.0));
                download_points.append(QPointF(static_cast<double>(current.timestamp_ms - 1), 0.0));
            }
            if (interval_seconds <= 0)
            {
                continue;
            }

            QPair<double, double> speeds = calculate_traffic_speeds(previous.timestamp_ms,
                                                                    previous.bytes_sent,
                                                                    previous.bytes_received,
                                                                    current.timestamp_ms,
                                                                    current.bytes_sent,
                                                                    current.bytes_received);
            upload_points.append(QPointF(static_cast<double>(current.timestamp_ms), speeds.first));
            download_points.append(QPointF(static_cast<double>(current.timestamp_ms), speeds.second));
        }

        const auto& last_snapshot = sorted_snapshots.last();
        series_map_[interface_name].last_stats = {
            interface_name, last_snapshot.bytes_received, last_snapshot.bytes_sent, QDateTime::fromMSecsSinceEpoch(last_snapshot.timestamp_ms)};
    }
    series_map_[interface_name].upload->replace(upload_points);
    series_map_[interface_name].download->replace(download_points);

    pending_queries_count_--;
    if (pending_queries_count_ <= 0)
    {
        process_loaded_data_batch();
    }
}

void main_window::process_loaded_data_batch()
{
    LOG_TRACE("all pending queries finished for request id {} processing batch", current_load_request_id_);

    if (!is_manual_view_active_)
    {
        const QDateTime end_time = QDateTime::currentDateTime();
        const qint64 visible_window_msecs = kVisibleWindowMinutes * 60L * 1000;
        const QDateTime start_time = end_time.addMSecs(-visible_window_msecs);
        update_x_axis(start_time, end_time);
    }
    rescale_y_axis();
    update_all_visuals();
}

void main_window::update_x_axis(const QDateTime& start, const QDateTime& end)
{
    const qint64 duration_seconds = start.secsTo(end);
    int tick_count;
    if (duration_seconds < 1)
    {
        tick_count = 2;
        axis_x_->setFormat("hh:mm:ss");
    }
    else if (duration_seconds <= 2L * 60)
    {
        axis_x_->setFormat("hh:mm:ss");
        tick_count = qBound(2, static_cast<int>(duration_seconds / 15) + 1, 8);
    }
    else
    {
        axis_x_->setFormat("hh:mm");
        tick_count = qBound(3, static_cast<int>(duration_seconds / (60L * 2)) + 1, 11);
    }
    axis_x_->setTickCount(tick_count);
    axis_x_->setRange(start, end);
}

void main_window::rescale_y_axis()
{
    double max_visible_speed = 0.0;
    qint64 min_x_ms = axis_x_->min().toMSecsSinceEpoch();
    qint64 max_x_ms = axis_x_->max().toMSecsSinceEpoch();
    for (const auto& series_pair : series_map_.values())
    {
        for (const auto* series : {series_pair.upload, series_pair.download})
        {
            if (!series->isVisible())
            {
                continue;
            }
            for (const auto& point : series->points())
            {
                if (point.x() >= static_cast<double>(min_x_ms) && point.x() <= static_cast<double>(max_x_ms))
                {
                    max_visible_speed = qMax(max_visible_speed, point.y());
                }
            }
        }
    }
    constexpr double min_y_range = 100.0;
    double new_max_y = qMax(min_y_range, max_visible_speed * 1.2);
    if (qAbs(axis_y_->max() - new_max_y) > 0.1)
    {
        axis_y_->setRange(0, new_max_y);
    }
}

void main_window::toggle_series_visibility(const QString& name)
{
    if (isolated_interface_name_ == name)
    {
        isolated_interface_name_.clear();
    }
    else
    {
        isolated_interface_name_ = name;
    }
    update_all_visuals();
    rescale_y_axis();
}

void main_window::update_all_visuals()
{
    bool is_isolated_mode = !isolated_interface_name_.isEmpty();
    auto* legend = chart_->legend();
    for (auto it = series_map_.constBegin(); it != series_map_.constEnd(); ++it)
    {
        const QString& interface_name = it.key();
        const interface_series& series_pair = it.value();
        bool is_target_interface = (interface_name == isolated_interface_name_);
        bool should_be_visible = !is_isolated_mode || is_target_interface;
        series_pair.upload->setVisible(should_be_visible);
        series_pair.download->setVisible(should_be_visible);
        for (auto* marker : legend->markers(series_pair.upload))
        {
            marker->setVisible(false);
        }
        if (series_pair.marker != nullptr)
        {
            series_pair.marker->setVisible(should_be_visible);
            if (is_isolated_mode && !is_target_interface)
            {
                series_pair.marker->setLabelBrush(Qt::lightGray);
                series_pair.marker->setBrush(Qt::lightGray);
            }
            else
            {
                series_pair.marker->setLabelBrush(chart_->legend()->labelColor());
                series_pair.marker->setBrush(series_pair.download->pen().color());
            }
        }
    }
}

void main_window::handle_series_hovered(const QPointF& point, bool state)
{
    if (!state || tooltip_ == nullptr)
    {
        tooltip_->hide();
        return;
    }
    auto* series = qobject_cast<QLineSeries*>(sender());
    if (series == nullptr)
    {
        return;
    }
    QString series_type_name;
    for (const auto& series_pair : series_map_.values())
    {
        if (series == series_pair.upload)
        {
            series_type_name = "上传";
            break;
        }
        if (series == series_pair.download)
        {
            series_type_name = "下载";
            break;
        }
    }
    if (series_type_name.isEmpty())
    {
        return;
    }
    QString tooltip_text = QString("%1: %2 KB/s\n时间: %3")
                               .arg(series_type_name)
                               .arg(point.y(), 0, 'f', 2)
                               .arg(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(point.x())).toString("hh:mm:ss"));
    tooltip_->setText(tooltip_text);
    QPointF scene_pos = chart_->mapToPosition(point, series);
    tooltip_->setPos(scene_pos.x() + 10, scene_pos.y() - 30);
    tooltip_->show();
}

void main_window::closeEvent(QCloseEvent* event)
{
    if (tray_icon_->isVisible())
    {
        hide();
        event->ignore();
    }
    else
    {
        event->accept();
    }
}

void main_window::quit_application()
{
    LOG_INFO("quitting application via tray menu");
    hide();
    QApplication::quit();
}

void main_window::setup_tray_icon()
{
    tray_icon_ = new QSystemTrayIcon(this);
    tray_menu_ = new QMenu(this);

    show_hide_action_ = new QAction("显示/隐藏", this);
    connect(show_hide_action_, &QAction::triggered, this, [this]() { isVisible() ? hide() : show(); });

    quit_action_ = new QAction("退出", this);
    connect(quit_action_, &QAction::triggered, this, &main_window::quit_application);

    tray_menu_->addAction(show_hide_action_);
    tray_menu_->addAction(quit_action_);

    tray_icon_->setContextMenu(tray_menu_);
    tray_icon_->setIcon(QApplication::windowIcon());
    tray_icon_->setToolTip("网络速度监视器");
    connect(tray_icon_, &QSystemTrayIcon::activated, this, &main_window::on_tray_icon_activated);
}

void main_window::on_tray_icon_activated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
    {
        isVisible() ? hide() : show();
    }
}
