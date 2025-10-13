#include <QThread>
#include "log.h"
#include "data_collector.h"

data_collector::data_collector(QObject* parent) : QObject(parent) {}

void data_collector::start_collection(int interval_ms)
{
    if (collection_timer_ == nullptr)
    {
        LOG_INFO("creating collector timer in thread {}", QThread::currentThreadId());
        collection_timer_ = new QTimer(this);
        connect(collection_timer_, &QTimer::timeout, this, &data_collector::collect_and_emit_stats);
    }

    LOG_INFO("data collector starting with interval {}ms in thread {}", interval_ms, QThread::currentThreadId());
    if (!collection_timer_->isActive())
    {
        collection_timer_->start(interval_ms);
    }
}

void data_collector::stop_collection()
{
    LOG_INFO("data collector stopping");
    if (collection_timer_ != nullptr)
    {
        collection_timer_->stop();
    }
}

void data_collector::collect_and_emit_stats()
{
    LOG_TRACE("collecting network stats");
    QList<interface_stats> stats = network_info::get_all_stats();
    emit stats_collected(stats, QDateTime::currentDateTime());
}
