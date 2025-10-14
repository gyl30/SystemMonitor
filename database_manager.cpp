#include <cstdlib>
#include <QThread>
#include <QVariant>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

#include "log.h"
#include "database_manager.h"

static QString connection_name() { return QString("db_connection_%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId())); }

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

    if (!open_database() || !create_tables())
    {
        LOG_ERROR("failed to initialize database aborting initialization");
        emit initialization_failed();
        return;
    }

    prune_old_data(30);
    LOG_INFO("database is ready.");
    emit database_ready();
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

bool database_manager::create_tables()
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

    success = query.exec(
        "CREATE TABLE IF NOT EXISTS dns_logs ("
        "timestamp INTEGER NOT NULL, "
        "transaction_id INTEGER NOT NULL, "
        "direction INTEGER NOT NULL, "
        "query_domain TEXT NOT NULL, "
        "query_type TEXT NOT NULL, "
        "response_code TEXT, "
        "response_data TEXT, "
        "resolver_ip TEXT NOT NULL"
        ")");
    if (!success)
    {
        LOG_ERROR("create table dns_logs failed {}", query.lastError().text().toStdString());
        return false;
    }

    success = query.exec("CREATE INDEX IF NOT EXISTS idx_dns_log_time ON dns_logs (timestamp)");
    if (!success)
    {
        LOG_ERROR("create index on dns_logs timestamp failed {}", query.lastError().text().toStdString());
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

void database_manager::add_dns_log(const dns_query_info& info)
{
    if (!db_.isOpen())
    {
        LOG_WARN("cannot add dns log database is not open");
        return;
    }

    QSqlQuery query(db_);
    query.prepare(
        "INSERT INTO dns_logs (timestamp, transaction_id, direction, query_domain, query_type, response_code, response_data, resolver_ip) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");

    query.bindValue(0, info.timestamp.toMSecsSinceEpoch());
    query.bindValue(1, info.transaction_id);
    query.bindValue(2, static_cast<int>(info.direction));
    query.bindValue(3, info.query_domain);
    query.bindValue(4, info.query_type);
    query.bindValue(5, info.response_code);
    query.bindValue(6, info.response_data.join(", "));
    query.bindValue(7, info.resolver_ip);

    if (!query.exec())
    {
        LOG_ERROR("db add dns log failed for {} {}", info.query_domain.toStdString(), query.lastError().text().toStdString());
    }
    else
    {
        LOG_TRACE("successfully added dns log for {} to database", info.query_domain.toStdString());
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
        LOG_ERROR("prune old traffic data failed {}", query.lastError().text().toStdString());
    }
    else
    {
        LOG_INFO("pruned traffic data older than {} days", days_to_keep);
    }

    query.prepare("DELETE FROM dns_logs WHERE timestamp < ?");
    query.bindValue(0, cutoff.toMSecsSinceEpoch());
    if (!query.exec())
    {
        LOG_ERROR("prune old dns data failed {}", query.lastError().text().toStdString());
    }
    else
    {
        LOG_INFO("pruned dns data older than {} days", days_to_keep);
    }
}

void database_manager::get_qps_stats(quint64 request_id, const QDateTime& start, const QDateTime& end, int interval_secs)
{
    LOG_DEBUG("processing get_qps_stats request id {}", request_id);
    QList<QPointF> results;
    if (!db_.isOpen() || interval_secs <= 0)
    {
        LOG_WARN("cannot get qps stats db not open or interval invalid");
        emit qps_stats_ready(request_id, results);
        return;
    }

    qint64 interval_ms = interval_secs * 1000L;

    QSqlQuery query(db_);
    query.prepare(
        "SELECT "
        "  (timestamp / :interval_ms) * :interval_ms AS time_window, "
        "  COUNT(*) "
        "FROM dns_logs "
        "WHERE timestamp BETWEEN :start_ts AND :end_ts AND direction = 0 "
        "GROUP BY time_window "
        "ORDER BY time_window");

    query.bindValue(":interval_ms", interval_ms);
    query.bindValue(":start_ts", start.toMSecsSinceEpoch());
    query.bindValue(":end_ts", end.toMSecsSinceEpoch());

    if (!query.exec())
    {
        LOG_ERROR("db get qps stats failed {}", query.lastError().text().toStdString());
    }
    else
    {
        while (query.next())
        {
            qreal timestamp = static_cast<qreal>(query.value(0).toLongLong());
            qreal count = query.value(1).toInt();
            results.append(QPointF(timestamp, count));
        }
    }

    LOG_DEBUG("qps stats query finished for id {} found {} data points", request_id, results.size());
    emit qps_stats_ready(request_id, results);
}

void database_manager::get_top_domains(quint64 request_id, const QDateTime& start, const QDateTime& end)
{
    LOG_DEBUG("top domains request id {}", request_id);
    QList<QPair<QString, int>> results;
    if (!db_.isOpen())
    {
        LOG_WARN("cannot get top domains db not open");
        emit top_domains_ready(request_id, results);
        return;
    }

    QSqlQuery query(db_);
    query.prepare(
        "SELECT "
        "  query_domain, "
        "  COUNT(*) as query_count "
        "FROM dns_logs "
        "WHERE timestamp BETWEEN :start_ts AND :end_ts AND direction = 0 "
        "GROUP BY query_domain "
        "ORDER BY query_count DESC "
        "LIMIT 10");

    query.bindValue(":start_ts", start.toMSecsSinceEpoch());
    query.bindValue(":end_ts", end.toMSecsSinceEpoch());

    if (!query.exec())
    {
        LOG_ERROR("db get top domains failed {}", query.lastError().text().toStdString());
    }
    else
    {
        while (query.next())
        {
            results.append({query.value(0).toString(), query.value(1).toInt()});
        }
    }

    LOG_DEBUG("top domains query finished for id {} found {} domains", request_id, results.size());
    emit top_domains_ready(request_id, results);
}
