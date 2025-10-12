#include <QDir>
#include <QFile>
#include "network_info.h"

QList<interface_stats> network_info::get_all_stats()
{
    QList<interface_stats> stats_list;
    QDir net_dir("/sys/class/net/");
    QStringList interface_list = net_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    static const QStringList ignored_prefixes = {"lo", "tailscale", "vnet", "veth", "br-", "docker", "virbr", "vmnet"};
    for (const QString& if_name : interface_list)
    {
        bool ignore = false;
        for (const QString& prefix : ignored_prefixes)
        {
            if (if_name.startsWith(prefix))
            {
                ignore = true;
                break;
            }
        }
        if (ignore)
        {
            continue;
        }
        QFile operstate_file(net_dir.filePath(if_name + "/operstate"));
        if (!operstate_file.open(QIODevice::ReadOnly))
        {
            continue;
        }

        QString state = operstate_file.readAll().trimmed();
        if (state != "up" && state != "unknown")
        {
            operstate_file.close();
            continue;
        }
        operstate_file.close();
        interface_stats stats;
        stats.name = if_name;

        QFile rx_file(net_dir.filePath(if_name + "/statistics/rx_bytes"));
        if (rx_file.open(QIODevice::ReadOnly))
        {
            stats.bytes_received = rx_file.readAll().trimmed().toULongLong();
            rx_file.close();
        }
        else
        {
            stats.bytes_received = 0;
        }

        QFile tx_file(net_dir.filePath(if_name + "/statistics/tx_bytes"));
        if (tx_file.open(QIODevice::ReadOnly))
        {
            stats.bytes_sent = tx_file.readAll().trimmed().toULongLong();
            tx_file.close();
        }
        else
        {
            stats.bytes_sent = 0;
        }

        stats_list.append(stats);
    }

    return stats_list;
}
