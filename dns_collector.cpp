#include <QThread>
#include <Packet.h>
#include <PcapFilter.h>
#include <DnsLayer.h>
#include <UdpLayer.h>
#include <TcpLayer.h>
#include <IPv4Layer.h>
#include <IPv6Layer.h>
#include <PcapLiveDeviceList.h>
#include "log.h"
#include "dns_collector.h"

namespace
{
struct dns_info_registrar
{
    dns_info_registrar()
    {
        qRegisterMetaType<dns_query_info::packet_direction>("dns_query_info::packet_direction");
        qRegisterMetaType<dns_query_info>("dns_query_info");
    }
};
dns_info_registrar registrar;
}    // namespace

static QString dns_type_to_string(pcpp::DnsType type)
{
    switch (type)
    {
        case pcpp::DNS_TYPE_A:
            return "A";
        case pcpp::DNS_TYPE_AAAA:
            return "AAAA";
        case pcpp::DNS_TYPE_NS:
            return "NS";
        case pcpp::DNS_TYPE_CNAME:
            return "CNAME";
        case pcpp::DNS_TYPE_PTR:
            return "PTR";
        case pcpp::DNS_TYPE_MX:
            return "MX";
        case pcpp::DNS_TYPE_SRV:
            return "SRV";
        case pcpp::DNS_TYPE_TXT:
            return "TXT";
        default:
            return QString("Type %1").arg(type);
    }
}

static QString dns_response_code_to_string(uint8_t code)
{
    switch (code)
    {
        case 0:
            return "NoError";
        case 1:
            return "FormErr";
        case 2:
            return "ServFail";
        case 3:
            return "NXDomain";
        case 4:
            return "NotImp";
        case 5:
            return "Refused";
        default:
            return QString("Code %1").arg(code);
    }
}
dns_collector::dns_collector(QObject* parent) : QObject(parent) {}

dns_collector::~dns_collector() { stop_capture(); }

void dns_collector::start_capture()
{
    LOG_INFO("attempting to start dns capture in thread {}", QThread::currentThreadId());
    device_ = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDeviceByName("eno1");
    if (device_ == nullptr)
    {
        LOG_ERROR("could not find a default pcap device. dns capture will not start");
        return;
    }

    LOG_INFO("found device {}", device_->getName());

    if (!device_->open())
    {
        LOG_ERROR("could not open pcap device {} dns capture will not start", device_->getName());
        device_ = nullptr;
        return;
    }
    LOG_INFO("device {} opened successfully", device_->getName());

    pcpp::PortFilter dns_filter(53, pcpp::SRC_OR_DST);
    if (!device_->setFilter(dns_filter))
    {
        LOG_ERROR("could not set dns filter on device {} dns capture will not start", device_->getName());
        device_->close();
        device_ = nullptr;
        return;
    }
    LOG_INFO("dns filter set successfully on device {}", device_->getName());

    LOG_INFO("starting capture", device_->getName());
    device_->startCapture(packet_arrived_callback, this);
}

void dns_collector::stop_capture()
{
    if (device_ != nullptr)
    {
        LOG_INFO("stopping dns capture on device {}", device_->getName());
        device_->stopCapture();
        device_->close();
        device_ = nullptr;
    }
}

void dns_collector::packet_arrived_callback(pcpp::RawPacket* raw_packet, pcpp::PcapLiveDevice* dev, void* cookie)
{
    (void)dev;
    LOG_TRACE("packet_arrived_callback triggered");
    auto* collector = static_cast<dns_collector*>(cookie);
    if (collector != nullptr)
    {
        collector->process_packet(raw_packet);
    }
}

void dns_collector::process_packet(pcpp::RawPacket* raw_packet)
{
    LOG_TRACE("processing a new packet");
    pcpp::Packet parsed_packet(raw_packet);
    auto* dns_layer = parsed_packet.getLayerOfType<pcpp::DnsLayer>();

    if (dns_layer == nullptr)
    {
        LOG_TRACE("packet does not contain a dns layer skipping");
        return;
    }
    LOG_DEBUG("dns layer found in packet");

    pcpp::dnshdr* dns_header = dns_layer->getDnsHeader();
    dns_query_info info;

    info.timestamp = QDateTime::currentDateTime();
    info.transaction_id = be16toh(dns_header->transactionID);

    pcpp::DnsQuery* query = dns_layer->getFirstQuery();
    if (query != nullptr)
    {
        info.query_domain = QString::fromStdString(query->getName());
        info.query_type = dns_type_to_string(query->getDnsType());
    }
    else
    {
        LOG_WARN("dns layer found but it contains no query section");
        return;
    }

    if (dns_header->queryOrResponse == 0)
    {
        info.direction = dns_query_info::packet_direction::kRequest;
        if (auto* ipLayer = parsed_packet.getLayerOfType<pcpp::IPLayer>())
        {
            info.resolver_ip = QString::fromStdString(ipLayer->getDstIPAddress().toString());
        }
        LOG_DEBUG("parsed dns request for {} id {}", info.query_domain.toStdString(), info.transaction_id);
    }
    else
    {
        info.direction = dns_query_info::packet_direction::kResponse;
        info.response_code = dns_response_code_to_string(dns_header->responseCode);

        for (pcpp::DnsResource* answer = dns_layer->getFirstAnswer(); answer != nullptr; answer = dns_layer->getNextAnswer(answer))
        {
            switch (answer->getDnsType())
            {
                case pcpp::DNS_TYPE_A:
                    info.response_data.append(
                        QString::fromStdString(answer->getData()->castAs<pcpp::IPv4DnsResourceData>()->getIpAddress().toString()));
                    break;
                case pcpp::DNS_TYPE_AAAA:
                    info.response_data.append(
                        QString::fromStdString(answer->getData()->castAs<pcpp::IPv6DnsResourceData>()->getIpAddress().toString()));
                    break;
                case pcpp::DNS_TYPE_CNAME:
                case pcpp::DNS_TYPE_NS:
                case pcpp::DNS_TYPE_PTR:
                    info.response_data.append(QString::fromStdString(answer->getData()->castAs<pcpp::StringDnsResourceData>()->toString()));
                    break;
                default:
                    break;
            }
        }

        if (auto* ipLayer = parsed_packet.getLayerOfType<pcpp::IPLayer>())
        {
            info.resolver_ip = QString::fromStdString(ipLayer->getSrcIPAddress().toString());
        }
        LOG_DEBUG("parsed dns response for {} id {} code {}", info.query_domain.toStdString(), info.transaction_id, info.response_code.toStdString());
    }

    LOG_DEBUG("emitting dns_packet_collected signal");
    emit dns_packet_collected(info);
}
