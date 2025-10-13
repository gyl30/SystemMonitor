#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <QList>
#include <QObject>
#include <QDateTime>
#include <QtSql/QSqlDatabase>
#include "network_info.h"

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
    explicit database_manager(QString dbPath, QObject* parent = nullptr);
    ~database_manager() override;

   public slots:
    void initialize();
    void add_snapshots(const QList<interface_stats>& stats_list, const QDateTime& timestamp);
    void get_snapshots_in_range(quint64 request_id, const QString& interface_name, const QDateTime& start, const QDateTime& end);

   signals:
    void snapshots_ready(quint64 request_id, const QString& interface_name, const QList<traffic_point>& data);
    void initialization_failed();

   private:
    bool open_database();
    bool create_table();
    void prune_old_data(int days_to_keep);

    QString db_path_;
    QSqlDatabase db_;
};

#endif
