#ifndef NETWORK_INFO_H
#define NETWORK_INFO_H

#include <QString>
#include <QList>
#include <QPair>
#include <QDateTime>

struct interface_stats
{
    QString name;
    quint64 bytes_received;
    quint64 bytes_sent;
    QDateTime timestamp;
};

class network_info
{
   public:
    static QList<interface_stats> get_all_stats();
};

#endif
