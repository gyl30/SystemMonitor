#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <QList>
#include <QObject>
#include <QDateTime>
#include <QPointF>
#include <QPair>
#include <QStringList>
#include <QtSql/QSqlDatabase>
#include "network_info.h"
#include "dns_query_info.h"

struct traffic_point
{
    qint64 timestamp_ms;
    quint64 bytes_received;
    quint64 bytes_sent;
};

class database_manager : public QObject
{
    Q_OBJECT

   public:
    explicit database_manager(QString db_path, QObject* parent = nullptr);
    ~database_manager() override;

   public slots:
    void initialize();
    void add_snapshots(const QList<interface_stats>& stats_list, const QDateTime& timestamp);
    void get_snapshots_in_range(quint64 request_id, const QString& interface_name, const QDateTime& start, const QDateTime& end);
    void add_dns_log(const dns_query_info& info);
    void get_qps_stats(quint64 request_id, const QDateTime& start, const QDateTime& end, int interval_secs);
    void get_all_domains(quint64 request_id, const QDateTime& start, const QDateTime& end);
    void get_dns_details_for_domain(quint64 request_id, const QString& domain, const QDateTime& start, const QDateTime& end);

   signals:
    void snapshots_ready(quint64 request_id, const QString& interface_name, const QList<traffic_point>& data);
    void initialization_failed();
    void database_ready();
    void qps_stats_ready(quint64 request_id, const QList<QPointF>& data);
    void all_domains_ready(quint64 request_id, const QStringList& domains);
    void dns_details_ready(quint64 request_id, const QList<dns_query_info>& details);

   private:
    bool open_database();
    bool create_tables();
    void prune_old_data(int days_to_keep);

    QString db_path_;
    QSqlDatabase db_;
};

#endif
