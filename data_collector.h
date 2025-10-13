#ifndef DATA_COLLECTOR_H
#define DATA_COLLECTOR_H

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include "network_info.h"

class data_collector : public QObject
{
    Q_OBJECT

   public:
    explicit data_collector(QObject* parent = nullptr);

   public slots:
    void start_collection(int interval_ms);
    void stop_collection();

   private slots:
    void collect_and_emit_stats();

   signals:
    void stats_collected(const QList<interface_stats>& stats, const QDateTime& timestamp);

   private:
    QTimer* collection_timer_ = nullptr;
};

#endif
