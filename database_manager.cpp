#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QVariant>
#include <cstdlib>
#include "log.h"
#include "database_manager.h"

database_manager::database_manager(const QString& dbPath)
{
    db_ = QSqlDatabase::addDatabase("QSQLITE", "traffic_connection");
    db_.setDatabaseName(dbPath);
    if (!open_database() || !create_table())
    {
        LOG_ERROR("failed to initialize database aborting");
        exit(EXIT_FAILURE);
    }
}

database_manager::~database_manager()
{
    if (db_.isOpen())
    {
        db_.close();
    }
}

bool database_manager::open_database()
{
    if (!db_.open())
    {
        LOG_WARN("connection with database failed {}", db_.lastError().text().toStdString());
        return false;
    }
    db_.exec("PRAGMA journal_mode = WAL;");
    return true;
}

bool database_manager::create_table()
{
    QSqlQuery query(db_);
    bool success = query.exec(
        "CREATE TABLE IF NOT EXISTS traffic_snapshots ("
        "timestamp INTEGER NOT NULL, "
        "interface_name TEXT NOT NULL, "
        "bytes_received INTEGER NOT NULL, "
        "bytes_sent INTEGER NOT NULL, "
        "PRIMARY KEY (timestamp, interface_name)"
        ")");
    if (!success)
    {
        LOG_ERROR("create table traffic_snapshots failed {}", query.lastError().text().toStdString());
        return false;
    }

    success = query.exec("CREATE INDEX IF NOT EXISTS idx_snapshot_time ON traffic_snapshots (timestamp)");
    if (!success)
    {
        LOG_ERROR("create index on timestamp failed {}", query.lastError().text().toStdString());
    }
    return success;
}

bool database_manager::add_snapshots(const QList<interface_stats>& stats_list, const QDateTime& timestamp)
{
    if (stats_list.isEmpty())
    {
        return true;
    }

    db_.transaction();
    QSqlQuery query(db_);
    query.prepare(
        "INSERT OR REPLACE INTO traffic_snapshots (timestamp, interface_name, bytes_received, bytes_sent) "
        "VALUES (?, ?, ?, ?)");

    qint64 ts_msecs = timestamp.toMSecsSinceEpoch();

    for (const auto& stats : stats_list)
    {
        query.bindValue(0, ts_msecs);
        query.bindValue(1, stats.name);
        query.bindValue(2, QVariant::fromValue(stats.bytes_received));
        query.bindValue(3, QVariant::fromValue(stats.bytes_sent));
        if (!query.exec())
        {
            LOG_ERROR("db add snapshot failed {} {}", stats.name.toStdString(), query.lastError().text().toStdString());
            db_.rollback();
            return false;
        }
    }

    bool success = db_.commit();
    if (!success)
    {
        LOG_ERROR("db transaction commit failed {}", db_.lastError().text().toStdString());
    }
    return success;
}

QList<traffic_point> database_manager::get_snapshots_in_range(const QString& interface_name, const QDateTime& start, const QDateTime& end)
{
    QList<traffic_point> results;
    QSqlQuery query(db_);

    qint64 start_ts = start.toMSecsSinceEpoch();
    qint64 end_ts = end.toMSecsSinceEpoch();

    query.prepare(
        "SELECT timestamp, bytes_received, bytes_sent FROM traffic_snapshots "
        "WHERE interface_name = :name AND timestamp < :start_ts "
        "ORDER BY timestamp DESC LIMIT 1");
    query.bindValue(":name", interface_name);
    query.bindValue(":start_ts", start_ts);

    if (!query.exec())
    {
        LOG_ERROR("db get snapshots in range failed {}", query.lastError().text().toStdString());
        return results;
    }

    if (query.next())
    {
        results.append({query.value(0).toLongLong(), query.value(1).toULongLong(), query.value(2).toULongLong()});
    }

    query.prepare(
        "SELECT timestamp, bytes_received, bytes_sent FROM traffic_snapshots "
        "WHERE interface_name = :name AND timestamp BETWEEN :start_ts AND :end_ts "
        "ORDER BY timestamp ASC");
    query.bindValue(":name", interface_name);
    query.bindValue(":start_ts", start_ts);
    query.bindValue(":end_ts", end_ts);

    if (!query.exec())
    {
        LOG_ERROR("db get snapshots in range failed {}", query.lastError().text().toStdString());
        return results;
    }

    while (query.next())
    {
        results.append({query.value(0).toLongLong(), query.value(1).toULongLong(), query.value(2).toULongLong()});
    }

    return results;
}

void database_manager::prune_old_data(int days_to_keep)
{
    QDateTime cutoff = QDateTime::currentDateTime().addDays(-days_to_keep);
    QSqlQuery query(db_);
    query.prepare("DELETE FROM traffic_snapshots WHERE timestamp < ?");
    query.bindValue(0, cutoff.toMSecsSinceEpoch());

    if (!query.exec())
    {
        LOG_ERROR("prune old data failed {}", query.lastError().text().toStdString());
    }
    else
    {
        LOG_INFO("pruned data older than {} days", days_to_keep);
    }
}
