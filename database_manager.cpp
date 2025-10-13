#include <cstdlib>
#include <QThread>
#include <QVariant>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

#include "log.h"
#include "database_manager.h"

static QString connection_name() { return QString("traffic_connection_%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId())); }

database_manager::database_manager(QString dbPath, QObject* parent) : QObject(parent), db_path_(std::move(dbPath)) {}

database_manager::~database_manager()
{
    if (db_.isOpen())
    {
        db_.close();
    }
    LOG_INFO("database manager destroyed");
}

void database_manager::initialize()
{
    LOG_INFO("initializing database manager in thread {}", QThread::currentThreadId());
    db_ = QSqlDatabase::addDatabase("QSQLITE", connection_name());
    db_.setDatabaseName(db_path_);

    if (!open_database() || !create_table())
    {
        LOG_ERROR("failed to initialize database aborting initialization");
        emit initialization_failed();
        return;
    }

    prune_old_data(30);
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

void database_manager::add_snapshots(const QList<interface_stats>& stats_list, const QDateTime& timestamp)
{
    if (stats_list.isEmpty() || !db_.isOpen())
    {
        return;
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
            return;
        }
    }

    if (!db_.commit())
    {
        LOG_ERROR("db transaction commit failed {}", db_.lastError().text().toStdString());
    }
}

void database_manager::get_snapshots_in_range(quint64 request_id, const QString& interface_name, const QDateTime& start, const QDateTime& end)
{
    QList<traffic_point> results;
    if (!db_.isOpen())
    {
        emit snapshots_ready(request_id, interface_name, results);
        return;
    }

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
        LOG_ERROR("db get snapshots pre-range failed for {} {}", interface_name.toStdString(), query.lastError().text().toStdString());
    }
    else if (query.next())
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
        LOG_ERROR("db get snapshots in-range failed for {} {}", interface_name.toStdString(), query.lastError().text().toStdString());
    }
    else
    {
        while (query.next())
        {
            results.append({query.value(0).toLongLong(), query.value(1).toULongLong(), query.value(2).toULongLong()});
        }
    }

    emit snapshots_ready(request_id, interface_name, results);
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
