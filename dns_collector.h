#ifndef DNS_COLLECTOR_H
#define DNS_COLLECTOR_H

#include <QObject>
#include <PcapLiveDevice.h>
#include <RawPacket.h>
#include "dns_query_info.h"

class dns_collector : public QObject
{
    Q_OBJECT

   public:
    explicit dns_collector(QObject* parent = nullptr);
    ~dns_collector() override;

   public slots:
    void start_capture();
    void stop_capture();

   signals:
    void dns_packet_collected(const dns_query_info& info);

   private:
    void process_packet(pcpp::RawPacket* raw_packet);
    static void packet_arrived_callback(pcpp::RawPacket* raw_packet, pcpp::PcapLiveDevice* dev, void* cookie);

   private:
    pcpp::PcapLiveDevice* device_ = nullptr;
};

#endif
