#include <QLegendMarker>
#include <QGraphicsLayout>
#include <QElapsedTimer>
#include <algorithm>
#include <QDir>
#include <QApplication>
#include <QMouseEvent>
#include "log.h"
#include "main_window.h"
#include "network_info.h"

static constexpr int kVisibleWindowMinutes = 15L;
static constexpr int kSnapBackTimeoutMs = 5000;
static constexpr int kCollectionIntervalMs = 1000;

main_window::main_window(QWidget* parent)
    : QMainWindow(parent),
      chart_(nullptr),
      chart_view_(nullptr),
      axis_x_(nullptr),
      axis_y_(nullptr),
      color_index_(0),
      tooltip_(nullptr),
      collection_timer_(new QTimer(this)),
      snap_back_timer_(new QTimer(this)),
      is_manual_view_active_(false)
{
    QString db_path = QDir(QApplication::applicationDirPath()).filePath("network_speed.db");
    db_manager_ = std::make_unique<database_manager>(db_path);
    db_manager_->prune_old_data(30);

    setup_chart();
    setCentralWidget(chart_view_);
    setWindowTitle("Network Speed Monitor");
    resize(800, 600);

    snap_back_timer_->setSingleShot(true);
    connect(snap_back_timer_, &QTimer::timeout, this, &main_window::snap_back_to_live_view);

    LOG_INFO("Connecting to DraggableChartView signals...");
    connect(chart_view_, &draggable_chartview::interaction_started, this, &main_window::onInteractionStarted);
    connect(chart_view_, &draggable_chartview::view_changed_by_drag, this, &main_window::onInteractionFinished);

    connect(collection_timer_, &QTimer::timeout, this, &main_window::perform_update_tick);

    color_palette_ << Qt::blue << Qt::red << Qt::green << Qt::magenta << Qt::cyan << Qt::yellow;

    tooltip_ = new QGraphicsSimpleTextItem(chart_);
    tooltip_->setZValue(10);
    tooltip_->setBrush(Qt::white);
    tooltip_->setPen(QPen(Qt::black));
    tooltip_->hide();

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 visible_window_msecs = kVisibleWindowMinutes * 60L * 1000;
    const QDateTime start_time = now.addMSecs(-visible_window_msecs);
    load_data_for_display(start_time, now);
    collection_timer_->start(kCollectionIntervalMs);
}

main_window::~main_window() = default;

void main_window::perform_update_tick()
{
    LOG_TRACE("perform update tick");
    const QDateTime now = QDateTime::currentDateTime();
    QList<interface_stats> stats = network_info::get_all_stats();
    db_manager_->add_snapshots(stats, now);

    bool new_interface_detected = false;
    for (const auto& stat : stats)
    {
        if (!series_map_.contains(stat.name) && !new_interfaces_queue_.contains(stat.name))
        {
            new_interfaces_queue_.append(stat.name);
            new_interface_detected = true;
        }
    }

    if (new_interface_detected)
    {
        QTimer::singleShot(0, this, &main_window::process_new_interfaces);
    }

    if (is_manual_view_active_)
    {
        LOG_TRACE("in manual mode skipping live scroll");
    }
    else
    {
        const qint64 visible_window_msecs = kVisibleWindowMinutes * 60L * 1000;
        QDateTime query_start_time = now.addMSecs(-visible_window_msecs);
        load_data_for_display(query_start_time, now);
    }

    if (!chart_view_->property("dragEnabled").toBool() && first_timestamp_.isValid())
    {
        const qint64 total_duration_seconds = first_timestamp_.secsTo(now);
        const qint64 visible_window_seconds = kVisibleWindowMinutes * 60L;
        if (total_duration_seconds > visible_window_seconds)
        {
            chart_view_->set_drag_enabled(true);
            chart_view_->setProperty("dragEnabled", true);
        }
    }
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

    if (!is_manual_view_active_)
    {
        perform_update_tick();
    }
}

void main_window::onInteractionStarted()
{
    LOG_INFO("interaction started pausing live updates");
    if (!is_manual_view_active_)
    {
        is_manual_view_active_ = true;
        chart_->setTitle("Network Speed (Historical View)");
    }
    snap_back_timer_->start(kSnapBackTimeoutMs);
}

void main_window::onInteractionFinished()
{
    LOG_INFO("interaction finished loading data for the new view range");

    load_data_for_display(axis_x_->min(), axis_x_->max());

    snap_back_timer_->start(kSnapBackTimeoutMs);
}

void main_window::snap_back_to_live_view()
{
    LOG_INFO("snapback timer fired resetting to live view");
    is_manual_view_active_ = false;
    chart_->setTitle("Real-time Network Speed");
    perform_update_tick();
}

void main_window::setup_chart()
{
    chart_ = new QChart();
    chart_->setAnimationOptions(QChart::NoAnimation);
    chart_->layout()->setContentsMargins(0, 0, 0, 0);
    chart_->setBackgroundRoundness(0);
    chart_->setTitle("Real-time Network Speed");
    chart_->legend()->setVisible(true);
    chart_->legend()->setAlignment(Qt::AlignBottom);

    chart_view_ = new draggable_chartview(chart_);

    chart_view_->setRenderHint(QPainter::Antialiasing);
    axis_x_ = new QDateTimeAxis;
    axis_x_->setFormat("hh:mm:ss");
    axis_x_->setTitleText("Time");
    chart_->addAxis(axis_x_, Qt::AlignBottom);
    axis_y_ = new QValueAxis;
    axis_y_->setLabelFormat("%.1f KB/s");
    axis_y_->setTitleText("Speed");
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

    auto* upload_series = new QLineSeries();
    auto* download_series = new QLineSeries();
    download_series->setName(interface_name);
    upload_series->setName(interface_name + " Upload");
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
    if (db_manager_ == nullptr)
    {
        return;
    }
    LOG_DEBUG("loading data for range {} {}", start.toString("hh:mm:ss").toStdString(), end.toString("hh:mm:ss").toStdString());

    QDateTime actual_start_time = end;
    bool has_any_data = false;
    const auto interface_names = series_map_.keys();
    for (const QString& name : interface_names)
    {
        QList<traffic_point> snapshots = db_manager_->get_snapshots_in_range(name, start, end);
        std::sort(snapshots.begin(), snapshots.end(), [](const traffic_point& a, const traffic_point& b) { return a.timestamp_ms < b.timestamp_ms; });
        QVector<QPointF> upload_points;
        QVector<QPointF> download_points;
        if (snapshots.size() < 2)
        {
            series_map_[name].upload->replace(upload_points);
            series_map_[name].download->replace(download_points);
            continue;
        }
        has_any_data = true;
        QDateTime series_start_time = QDateTime::fromMSecsSinceEpoch(snapshots[1].timestamp_ms);
        if (series_start_time < actual_start_time)
        {
            actual_start_time = series_start_time;
        }
        if (!first_timestamp_.isValid())
        {
            first_timestamp_ = series_start_time;
        }
        upload_points.reserve(snapshots.size() - 1);
        download_points.reserve(snapshots.size() - 1);
        for (int i = 1; i < snapshots.size(); ++i)
        {
            const auto& current = snapshots[i];
            const auto& previous = snapshots[i - 1];
            double interval_seconds = static_cast<double>(current.timestamp_ms - previous.timestamp_ms) / 1000.0;
            if (interval_seconds <= 0)
            {
                continue;
            }
            quint64 sent_diff = (current.bytes_sent >= previous.bytes_sent) ? (current.bytes_sent - previous.bytes_sent) : current.bytes_sent;
            quint64 recv_diff =
                (current.bytes_received >= previous.bytes_received) ? (current.bytes_received - previous.bytes_received) : current.bytes_received;
            double upload_speed_kb = (static_cast<double>(sent_diff) / interval_seconds) / 1024.0;
            double download_speed_kb = (static_cast<double>(recv_diff) / interval_seconds) / 1024.0;
            upload_points.append(QPointF(static_cast<double>(current.timestamp_ms), upload_speed_kb));
            download_points.append(QPointF(static_cast<double>(current.timestamp_ms), download_speed_kb));
        }
        series_map_[name].upload->replace(upload_points);
        series_map_[name].download->replace(download_points);
    }

    if (has_any_data && !is_manual_view_active_)
    {
        update_x_axis(actual_start_time, end);
    }
    rescale_y_axis();
    update_all_visuals();
}

void main_window::update_x_axis(const QDateTime& start, const QDateTime& end)
{
    const qint64 duration_seconds = start.secsTo(end);
    int tick_count = 2;
    const int two_minutes_in_seconds = 2 * 60;
    if (duration_seconds < 1)
    {
        tick_count = 1;
    }
    else if (duration_seconds <= two_minutes_in_seconds)
    {
        axis_x_->setFormat("hh:mm:ss");
        tick_count = qBound(2, static_cast<int>(duration_seconds / 15) + 2, 8);
    }
    else
    {
        axis_x_->setFormat("hh:mm");
        tick_count = qBound(3, static_cast<int>(duration_seconds / 60) + 2, 11);
    }
    if (duration_seconds >= (kVisibleWindowMinutes * 60L))
    {
        tick_count = 10;
        axis_x_->setFormat("hh:mm");
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
    if (axis_y_->max() != new_max_y)
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
            series_type_name = "Upload";
            break;
        }
        if (series == series_pair.download)
        {
            series_type_name = "Download";
            break;
        }
    }
    if (series_type_name.isEmpty())
    {
        return;
    }
    QString tooltip_text = QString("%1: %2 KB/s\nTime: %3")
                               .arg(series_type_name)
                               .arg(point.y(), 0, 'f', 2)
                               .arg(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(point.x())).toString("hh:mm:ss"));
    tooltip_->setText(tooltip_text);
    QPointF scene_pos = chart_->mapToPosition(point, series);
    tooltip_->setPos(scene_pos.x() + 10, scene_pos.y() - 30);
    tooltip_->show();
}
