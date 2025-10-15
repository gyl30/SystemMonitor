#include <QVBoxLayout>
#include <QHeaderView>
#include <QTimer>
#include <QTableView>
#include <QMap>
#include <QPen>
#include <QDateTime>
#include <QtCharts/QChart>
#include <QStandardItemModel>
#include <QGraphicsSimpleTextItem>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegend>
#include <algorithm>

#include "log.h"
#include "dns_page.h"

static constexpr auto kRefreshIntervalSecs = 10;
static constexpr auto kChartIntervalSecs = 10L;
static constexpr auto kHistoryDurationSecs = 180;
static constexpr auto kSnapBackTimeoutMs = 5000;

enum class dns_details_column : uint8_t
{
    kTimestamp,
    kDirection,
    kQueryType,
    kResponseCode,
    kResponseData,
    kResolverIp,
    kColumnCount
};

dns_page::dns_page(QWidget* parent) : QWidget(parent)
{
    setup_chart();
    setup_ui();

    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, &dns_page::on_refresh_timer_timeout);

    snap_back_timer_ = new QTimer(this);
    snap_back_timer_->setSingleShot(true);
    connect(snap_back_timer_, &QTimer::timeout, this, &dns_page::snap_back_to_live_view);

    connect(chart_view_, &draggable_chartview::interaction_started, this, &dns_page::on_interaction_started);
    connect(chart_view_, &draggable_chartview::view_changed_by_drag, this, &dns_page::on_interaction_finished);

    connect(all_domains_view_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &dns_page::on_domain_selected);

    request_data_for_current_view();
}
void dns_page::trigger_initial_load()
{
    LOG_INFO("initial load triggered");
    if (!refresh_timer_->isActive())
    {
        on_refresh_timer_timeout();
        refresh_timer_->start(kRefreshIntervalSecs * 1000);
    }
}
void dns_page::setup_ui()
{
    chart_view_ = new draggable_chartview(chart_, this);
    chart_view_->setRenderHint(QPainter::Antialiasing);

    splitter_ = new QSplitter(Qt::Horizontal, this);

    all_domains_model_ = new QStandardItemModel(0, 1, this);
    all_domains_model_->setHorizontalHeaderLabels({"域名"});
    all_domains_view_ = new QTableView(this);
    all_domains_view_->setModel(all_domains_model_);
    all_domains_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    all_domains_view_->verticalHeader()->hide();
    all_domains_view_->horizontalHeader()->setStretchLastSection(true);
    all_domains_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    all_domains_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    all_domains_view_->setSortingEnabled(true);

    domain_details_model_ = new QStandardItemModel(0, static_cast<int>(dns_details_column::kColumnCount), this);

    domain_details_model_->setHorizontalHeaderLabels({"时间", "方向", "类型", "响应码", "响应数据", "解析器 IP"});
    domain_details_view_ = new QTableView(this);
    domain_details_view_->setModel(domain_details_model_);
    domain_details_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    domain_details_view_->verticalHeader()->hide();
    domain_details_view_->setSelectionBehavior(QAbstractItemView::SelectRows);

    domain_details_view_->horizontalHeader()->setStretchLastSection(true);

    splitter_->addWidget(all_domains_view_);
    splitter_->addWidget(domain_details_view_);
    splitter_->setSizes({300, 700});

    auto* main_layout = new QVBoxLayout(this);
    main_layout->addWidget(chart_view_, 3);
    main_layout->addWidget(splitter_, 2);
    setLayout(main_layout);
}

void dns_page::setup_chart()
{
    chart_ = new QChart();
    chart_->setTitle("DNS 查询率");

    qps_series_ = new QLineSeries();
    qps_series_->setName("DNS 请求数");
    chart_->addSeries(qps_series_);
    connect(qps_series_, &QLineSeries::hovered, this, &dns_page::handle_series_hovered);

    axis_x_ = new QDateTimeAxis;
    axis_x_->setFormat("hh:mm");
    axis_x_->setTitleText("时间");
    chart_->addAxis(axis_x_, Qt::AlignBottom);
    qps_series_->attachAxis(axis_x_);

    axis_y_ = new QValueAxis;
    axis_y_->setTitleText("查询数");
    axis_y_->setMin(0);
    chart_->addAxis(axis_y_, Qt::AlignLeft);
    qps_series_->attachAxis(axis_y_);

    chart_->legend()->setVisible(true);
    chart_->legend()->setAlignment(Qt::AlignBottom);

    tooltip_ = new QGraphicsSimpleTextItem(chart_);
    tooltip_->setZValue(10);
    tooltip_->setBrush(Qt::white);
    tooltip_->setPen(QPen(Qt::black));
    tooltip_->hide();
}

void dns_page::on_refresh_timer_timeout()
{
    if (is_manual_view_active_)
    {
        return;
    }
    request_data_for_current_view();
}

void dns_page::request_data_for_current_view()
{
    current_request_id_++;
    LOG_DEBUG("dns_page: requesting new data with id {}.", current_request_id_);

    QDateTime start_time;
    QDateTime end_time;

    if (is_manual_view_active_)
    {
        start_time = axis_x_->min();
        end_time = axis_x_->max();
    }
    else
    {
        end_time = QDateTime::currentDateTime();
        start_time = end_time.addSecs(-kHistoryDurationSecs);
    }

    emit request_qps_stats(current_request_id_, start_time, end_time, kChartIntervalSecs);
    emit request_all_domains(current_request_id_, start_time, end_time);
}

void dns_page::handle_qps_stats_ready(quint64 request_id, const QList<QPointF>& data)
{
    if (request_id != current_request_id_)
    {
        LOG_DEBUG("ignoring stale qps data request id {} current id {}", request_id, current_request_id_);
        return;
    }
    LOG_DEBUG("received qps stats ready for id {} data points from db {}", request_id, data.size());

    if (!drag_enabled_ && !data.isEmpty())
    {
        if (!first_timestamp_.isValid())
        {
            first_timestamp_ = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(data.first().x()));
            LOG_INFO("first timestamp recorded {}", first_timestamp_.toString("hh:mm:ss").toStdString());
        }

        qint64 total_duration = first_timestamp_.secsTo(QDateTime::currentDateTime());
        if (total_duration > kHistoryDurationSecs)
        {
            LOG_INFO("sufficient data collected {}s > {}s enabling chart dragging", total_duration, kHistoryDurationSecs);
            chart_view_->set_drag_enabled(true);
            drag_enabled_ = true;
        }
    }

    QMap<qint64, qreal> data_map;
    for (const auto& point : data)
    {
        data_map.insert(static_cast<qint64>(point.x()), point.y());
    }

    QList<QPointF> full_data;
    QDateTime start_interval = is_manual_view_active_ ? axis_x_->min() : QDateTime::currentDateTime().addSecs(-kHistoryDurationSecs);
    QDateTime end_interval = is_manual_view_active_ ? axis_x_->max() : QDateTime::currentDateTime();

    qint64 start_msecs = start_interval.toMSecsSinceEpoch();
    qint64 interval_msecs = kChartIntervalSecs * 1000;
    start_msecs = (start_msecs / interval_msecs) * interval_msecs;

    QDateTime current_interval = QDateTime::fromMSecsSinceEpoch(start_msecs);

    while (current_interval <= end_interval)
    {
        qint64 current_msecs = current_interval.toMSecsSinceEpoch();
        full_data.append(QPointF(static_cast<qreal>(current_msecs), data_map.value(current_msecs, 0)));
        current_interval = current_interval.addSecs(kChartIntervalSecs);
    }

    qps_series_->replace(full_data);

    if (!is_manual_view_active_)
    {
        update_chart_axes(start_interval, end_interval);
    }
}

void dns_page::handle_all_domains_ready(quint64 request_id, const QStringList& domains)
{
    if (request_id != current_request_id_)
    {
        return;
    }
    LOG_DEBUG("received all domains ready for id {} domains found {}", request_id, domains.size());

    QString previously_selected_domain;
    if (all_domains_view_->selectionModel()->hasSelection())
    {
        previously_selected_domain = all_domains_view_->selectionModel()->currentIndex().data().toString();
    }

    all_domains_model_->removeRows(0, all_domains_model_->rowCount());
    if (previously_selected_domain.isEmpty())
    {
        domain_details_model_->removeRows(0, domain_details_model_->rowCount());
    }

    for (const QString& domain : domains)
    {
        all_domains_model_->appendRow(new QStandardItem(domain));
    }

    if (!previously_selected_domain.isEmpty())
    {
        QModelIndexList matches = all_domains_model_->match(
            all_domains_model_->index(0, 0), Qt::DisplayRole, QVariant::fromValue(previously_selected_domain), 1, Qt::MatchExactly);
        if (!matches.isEmpty())
        {
            all_domains_view_->setCurrentIndex(matches.first());
        }
        else
        {
            domain_details_model_->removeRows(0, domain_details_model_->rowCount());
        }
    }
}

void dns_page::handle_dns_details_ready(quint64 request_id, const QList<dns_query_info>& details)
{
    if (request_id != current_details_request_id_)
    {
        return;
    }
    LOG_DEBUG("received dns details ready for id {} records found {}", request_id, details.size());
    domain_details_model_->removeRows(0, domain_details_model_->rowCount());
    domain_details_view_->clearSpans();

    for (int i = 0; i < details.size(); ++i)
    {
        const auto& info = details[i];
        QList<QStandardItem*> row_items;
        row_items.append(new QStandardItem(info.timestamp.toString("yyyy-MM-dd hh:mm:ss.zzz")));

        bool is_request = (info.direction == dns_query_info::packet_direction::kRequest);
        row_items.append(new QStandardItem(is_request ? "请求" : "响应"));
        row_items.append(new QStandardItem(info.query_type));

        if (is_request)
        {
            auto* placeholder_item = new QStandardItem("—");
            placeholder_item->setTextAlignment(Qt::AlignCenter);
            row_items.append(placeholder_item);
            row_items.append(new QStandardItem());
            domain_details_model_->appendRow(row_items);

            const int start_col = static_cast<int>(dns_details_column::kResponseCode);
            const int col_span = 2;
            domain_details_view_->setSpan(i, start_col, 1, col_span);
        }
        else
        {
            row_items.append(new QStandardItem(info.response_code));
            row_items.append(new QStandardItem(info.response_data.join(", ")));
            domain_details_model_->appendRow(row_items);
        }

        const int resolver_col = static_cast<int>(dns_details_column::kResolverIp);
        domain_details_model_->setItem(i, resolver_col, new QStandardItem(info.resolver_ip));
    }
}

void dns_page::handle_series_hovered(const QPointF& point, bool state)
{
    if (!state || tooltip_ == nullptr)
    {
        tooltip_->hide();
        return;
    }

    QString tooltip_text =
        QString("时间: %1\n查询数: %2").arg(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(point.x())).toString("hh:mm:ss")).arg(point.y());

    tooltip_->setText(tooltip_text);
    QPointF scene_pos = chart_->mapToPosition(point, qps_series_);
    tooltip_->setPos(scene_pos.x() + 10, scene_pos.y() - 30);
    tooltip_->show();
}

void dns_page::update_chart_axes(const QDateTime& start, const QDateTime& end)
{
    axis_x_->setRange(start, end);

    double max_y = 0;
    const auto points = qps_series_->points();
    for (const auto& point : points)
    {
        max_y = std::max(point.y(), max_y);
    }
    axis_y_->setMax(qMax(10.0, max_y * 1.2));
}

void dns_page::on_interaction_started()
{
    if (!is_manual_view_active_)
    {
        LOG_INFO("interaction started entering manual view mode");
        is_manual_view_active_ = true;
        chart_->setTitle("DNS 查询率 (历史视图)");
    }
    snap_back_timer_->start(kSnapBackTimeoutMs);
}

void dns_page::on_interaction_finished()
{
    LOG_INFO("interaction finished loading data for new view range");
    request_data_for_current_view();
    snap_back_timer_->start(kSnapBackTimeoutMs);
}

void dns_page::snap_back_to_live_view()
{
    LOG_INFO("snapback timer fired resetting to live view");
    is_manual_view_active_ = false;
    chart_->setTitle("DNS 查询率");
    request_data_for_current_view();
}

void dns_page::on_domain_selected(const QModelIndex& current, const QModelIndex& previous)
{
    (void)previous;
    if (!current.isValid())
    {
        domain_details_model_->removeRows(0, domain_details_model_->rowCount());
        return;
    }
    QString domain = all_domains_model_->data(current).toString();
    current_details_request_id_++;
    LOG_DEBUG("requesting details for domain {} with id {}", domain.toStdString(), current_details_request_id_);

    QDateTime start_time;
    QDateTime end_time;

    if (is_manual_view_active_)
    {
        start_time = axis_x_->min();
        end_time = axis_x_->max();
    }
    else
    {
        end_time = QDateTime::currentDateTime();
        start_time = end_time.addSecs(-kHistoryDurationSecs);
    }

    emit request_dns_details_for_domain(current_details_request_id_, domain, start_time, end_time);
}
