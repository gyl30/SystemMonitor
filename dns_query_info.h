#ifndef DNS_QUERY_INFO_H
#define DNS_QUERY_INFO_H

#include <QDateTime>
#include <QString>
#include <QStringList>

struct dns_query_info
{
    enum class packet_direction : uint8_t
    {
        kRequest,
        kResponse
    };

    QDateTime timestamp;
    quint16 transaction_id;
    packet_direction direction;
    QString query_domain;
    QString query_type;
    QString response_code;
    QStringList response_data;
    QString resolver_ip;
};

Q_DECLARE_METATYPE(dns_query_info::packet_direction)
Q_DECLARE_METATYPE(dns_query_info)

#endif
