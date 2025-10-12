#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <QObject>
#include <QtSql/QSqlDatabase>
#include <QDateTime>
#include <QList>
#include "network_info.h"

struct traffic_point
{
    qint64 timestamp_ms;
    quint64 bytes_received;
    quint64 bytes_sent;
};

class database_manager
{
   public:
    explicit database_manager(const QString& dbPath);
    ~database_manager();

    bool add_snapshots(const QList<interface_stats>& stats_list, const QDateTime& timestamp);
    QList<traffic_point> get_snapshots_in_range(const QString& interface_name, const QDateTime& start, const QDateTime& end);
    void prune_old_data(int days_to_keep);

   private:
    bool open_database();
    bool create_table();

    QSqlDatabase db_;
};

#endif
