#include "swarm/fastdds_wrapper.hpp"

#include <iostream>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/rtps/transport/TCPv4TransportDescriptor.hpp>
#include <fastdds/utils/IPLocator.hpp>

#include "ConsensusPubSubTypes.hpp"
#include "HeartbeatPubSubTypes.hpp"
#include "TaskAllocationPubSubTypes.hpp"
#include "TelemetryPubSubTypes.hpp"

namespace swarm {

// `using namespace` yalnızca bu .cpp dosyasının içinde geçerli; Fast DDS'in
// uzun isim alanını (eprosima::fastdds::dds::...) her satırda yazmamak için.
using namespace eprosima::fastdds::dds;

using eprosima::fastdds::rtps::IPLocator;
using eprosima::fastdds::rtps::Locator_t;
using eprosima::fastdds::rtps::TCPv4TransportDescriptor;

namespace {

// ---------------------------------------------------------------------------
//  LOCATOR NEDİR? Bir uca "nasıl ulaşılır" bilgisini tek bir yapıda toplar:
//  [taşıyıcı tipi] + [IP adresi] + [port]. UDP'de tek bir port yeterlidir;
//  TCP'de Fast DDS iki ayrı port kavramı kullanır:
//
//    fiziksel (physical) port : gerçek TCP soketinin dinlediği port
//    mantıksal (logical) port : aynı TCP bağlantısı üzerinden birden fazla
//                               RTPS muhatabını ayırmak için kullanılan numara
//
//  İkisini de aynı değere ayarlıyoruz; tek bir dinleme portumuz olduğu için
//  ayırmanın bir faydası yok ve okunması kolaylaşıyor.
// ---------------------------------------------------------------------------
Locator_t make_tcp_locator(const std::string& ip, uint16_t physical_port)
{
    Locator_t locator;
    locator.kind = LOCATOR_KIND_TCPv4;
    IPLocator::setIPv4(locator, ip);
    IPLocator::setPhysicalPort(locator, physical_port);
    IPLocator::setLogicalPort(locator, physical_port);
    return locator;
}

// ---------------------------------------------------------------------------
//  DdsKanal — bir topic'in topic + writer + reader + listener dörtlüsü
//
//  ŞABLON (TEMPLATE) NEDİR? Aynı kodu farklı tipler için tekrar tekrar
//  yazmamayı sağlayan mekanizma. `template <typename MesajTipi>` yazınca
//  derleyici, kullanılan her tip için bu sınıfın bir kopyasını üretir.
//
//  Bu proje genel olarak template'lerden kaçınır (okunabilirlik kuralı),
//  ama burada gerekli: dört mesaj tipi için birebir aynı 100 satırı dört kez
//  yazmak, tek bir şablondan çok daha zor okunur ve bakımı zor olurdu.
//
//  Sınıf ayrıca DataReaderListener'dan türüyor: veri geldiğinde Fast DDS
//  on_data_available()'ı çağırır.
// ---------------------------------------------------------------------------
template <typename MessageType, typename MessageTypeSupport>
class DdsChannel : public DataReaderListener
{
public:
    using Callback = std::function<void(const MessageType&)>;

    // tcp_locator nullptr degilse, bu kanalin writer ve reader'i keşifte
    // YALNIZCA o TCP locator'i ilan eder; boylece bu topic'in trafigi TCP
    // uzerinden akar (bkz. baslikta "TASIYICI NASIL SECILIYOR").
    //
    // Writer'a da yaziyoruz, sadece reader'a degil: RELIABLE QoS'ta alici
    // gonderene ACKNACK dondurur. Writer'in locator'i ayarlanmazsa veri
    // TCP'den giderken onaylar UDP'den donerdi.
    bool setup(DomainParticipant* participant,
             const std::string& topic_name,
             bool reliable,
             const Locator_t* tcp_locator)
    {
        participant_ = participant;

        // 1) Tipi participant'a tanıt. DDS, ağdan gelen baytları hangi
        //    sınıfa çevireceğini bu kayıttan bilir.
        type_support_ = TypeSupport(new MessageTypeSupport());
        if (type_support_.register_type(participant_) != RETCODE_OK)
        {
            return false;
        }

        // 2) Topic
        topic_ = participant_->create_topic(topic_name, type_support_.get_type_name(),
                                            TOPIC_QOS_DEFAULT);
        if (topic_ == nullptr)
        {
            return false;
        }

        // 3) Publisher + DataWriter
        publisher_ = participant_->create_publisher(PUBLISHER_QOS_DEFAULT);
        if (publisher_ == nullptr)
        {
            return false;
        }

        DataWriterQos writer_qos = DATAWRITER_QOS_DEFAULT;
        apply_qos(writer_qos.reliability(), writer_qos.durability(),
                   writer_qos.history(), reliable);
        if (tcp_locator != nullptr)
        {
            writer_qos.endpoint().unicast_locator_list.push_back(*tcp_locator);
        }

        writer_ = publisher_->create_datawriter(topic_, writer_qos);
        if (writer_ == nullptr)
        {
            return false;
        }

        // 4) Subscriber + DataReader (listener olarak `this`)
        subscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
        if (subscriber_ == nullptr)
        {
            return false;
        }

        DataReaderQos reader_qos = DATAREADER_QOS_DEFAULT;
        apply_qos(reader_qos.reliability(), reader_qos.durability(),
                   reader_qos.history(), reliable);
        if (tcp_locator != nullptr)
        {
            reader_qos.endpoint().unicast_locator_list.push_back(*tcp_locator);
        }

        reader_ = subscriber_->create_datareader(topic_, reader_qos, this);
        return reader_ != nullptr;
    }

    void teardown()
    {
        if (participant_ == nullptr)
        {
            return;
        }

        // Silme sırası önemlidir: önce içteki varlıklar, sonra kapsayanlar.
        if (subscriber_ != nullptr)
        {
            if (reader_ != nullptr)
            {
                subscriber_->delete_datareader(reader_);
                reader_ = nullptr;
            }
            participant_->delete_subscriber(subscriber_);
            subscriber_ = nullptr;
        }

        if (publisher_ != nullptr)
        {
            if (writer_ != nullptr)
            {
                publisher_->delete_datawriter(writer_);
                writer_ = nullptr;
            }
            participant_->delete_publisher(publisher_);
            publisher_ = nullptr;
        }

        if (topic_ != nullptr)
        {
            participant_->delete_topic(topic_);
            topic_ = nullptr;
        }

        participant_ = nullptr;
    }

    bool publish(const MessageType& message)
    {
        if (writer_ == nullptr)
        {
            return false;
        }

        // write() const olmayan bir işaretçi ister; kopya üzerinde
        // çalışıyoruz ki çağıranın mesajı değişmesin.
        MessageType copy = message;
        return writer_->write(&copy) == RETCODE_OK;
    }

    void set_callback(Callback callback)
    {
        callback_ = std::move(callback);
    }

    // Fast DDS, bu topic'e veri geldiğinde KENDİ thread'inden bunu çağırır.
    void on_data_available(DataReader* reader) override
    {
        SampleInfo info;
        MessageType message;

        // Bir bildirimde birden fazla örnek birikmiş olabilir; hepsini
        // boşaltıyoruz.
        while (reader->take_next_sample(&message, &info) == RETCODE_OK)
        {
            // valid_data == false, "veri değil, durum değişikliği bildirimi"
            // demektir (örn. yayıncı ayrıldı). İçeriği okunmamalıdır.
            if (info.valid_data && callback_)
            {
                callback_(message);
            }
        }
    }

private:
    // QoS'u tek yerde kuruyoruz ki writer ve reader tarafi birbirinden
    // ayrismasin: UYUSMAYAN QoS'ta DDS uclari HIC ESLESMEZ ve veri akmaz.
    // Bu, DDS'te en sik yapilan hatalardan biridir ve sessizce olur -
    // hata mesaji yoktur, sadece veri gelmez.
    static void apply_qos(
            ReliabilityQosPolicy& reliability,
            DurabilityQosPolicy& durability,
            HistoryQosPolicy& history,
            bool reliable)
    {
        if (reliable)
        {
            // --- Komut ve consensus kanallari (Bolum 3.4) ---
            // RELIABLE: DDS teslim edilmeyen ornekleri yeniden gonderir;
            // %100 ulastirma garantisi. Gorev emrinin kaybolmasi kabul
            // edilemez.
            reliability.kind = RELIABLE_RELIABILITY_QOS;

            // TRANSIENT_LOCAL: yayinci son orneklerini saklar ve SONRADAN
            // katilan bir aboneye de gonderir. Emir yayinlandiktan sonra aga
            // giren bir drone son gorev emrini yine de gorebilsin diye.
            durability.kind = TRANSIENT_LOCAL_DURABILITY_QOS;

            history.kind = KEEP_LAST_HISTORY_QOS;
            history.depth = 10;
        }
        else
        {
            // --- Heartbeat ve telemetri kanallari ---
            // BEST_EFFORT: kaybolan paket yeniden gonderilmez. Yuksek
            // frekansli (10-50 Hz) bir akista tazelik, eksiksizlikten daha
            // onemlidir; eski bir konumu tekrar gondermenin degeri yoktur.
            reliability.kind = BEST_EFFORT_RELIABILITY_QOS;

            // VOLATILE: gecmis saklanmaz. Sonradan katilan bir dugumun eski
            // heartbeat'lere ihtiyaci yok, bir sonrakini zaten 100 ms icinde
            // alacak.
            durability.kind = VOLATILE_DURABILITY_QOS;

            history.kind = KEEP_LAST_HISTORY_QOS;
            history.depth = 1;   // yalnizca en tazesi ilgilendiriyor
        }
    }

    DomainParticipant* participant_ = nullptr;
    TypeSupport type_support_;
    Topic* topic_ = nullptr;
    Publisher* publisher_ = nullptr;
    DataWriter* writer_ = nullptr;
    Subscriber* subscriber_ = nullptr;
    DataReader* reader_ = nullptr;
    Callback callback_;
};

}  // namespace

// ---------------------------------------------------------------------------
//  FastDDSWrapper::Icerik — gerçek üyeler (PIMPL)
// ---------------------------------------------------------------------------

struct FastDDSWrapper::Impl
{
    DomainParticipant* participant = nullptr;

    DdsChannel<Heartbeat, HeartbeatPubSubType> heartbeat;
    DdsChannel<Telemetry, TelemetryPubSubType> telemetry;
    DdsChannel<TaskAllocation, TaskAllocationPubSubType> task_alloc;
    DdsChannel<Consensus, ConsensusPubSubType> consensus;
};

FastDDSWrapper::FastDDSWrapper(uint32_t domain_id, TcpTransportConfig tcp_config)
    : impl_(std::make_unique<Impl>())
    , domain_id_(domain_id)
    , tcp_config_(std::move(tcp_config))
{
}

FastDDSWrapper::~FastDDSWrapper()
{
    impl_->heartbeat.teardown();
    impl_->telemetry.teardown();
    impl_->task_alloc.teardown();
    impl_->consensus.teardown();

    if (impl_->participant != nullptr)
    {
        impl_->participant->delete_contained_entities();
        DomainParticipantFactory::get_instance()->delete_participant(impl_->participant);
        impl_->participant = nullptr;
    }
}

bool FastDDSWrapper::init()
{
    DomainParticipantQos participant_qos = PARTICIPANT_QOS_DEFAULT;
    participant_qos.name("swarm_node");

    // --- TCP taşıyıcısı (Bölüm 3.4) -----------------------------------------
    // Participant, UDP'ye EK OLARAK bir TCPv4 taşıyıcısı açar. use_builtin_
    // transports açık kalıyor: keşif (SPDP) UDP multicast'ten yürümeye devam
    // etsin diye. Kapatsaydık her düğümün diğerlerinin IP'sini önceden bilmesi
    // gerekirdi ve Bölüm 2'deki otomatik keşif gereksinimi çökerdi.
    if (tcp_config_.enabled)
    {
        auto tcp_transport = std::make_shared<TCPv4TransportDescriptor>();
        tcp_transport->add_listener_port(tcp_config_.listening_port);

        // TCP_NODELAY (Nagle algoritmasını kapatır): Nagle, küçük paketleri
        // biriktirip tek seferde göndererek verimi artırır ama gecikme ekler.
        // Consensus oyları küçük ve gecikmeye duyarlı olduğu için kapatıyoruz.
        tcp_transport->enable_tcp_nodelay = true;

        // Taşıyıcının duyurularında kullanacağı adres. İsmi "WAN" olsa da
        // burada NAT/internet senaryosu yok: bu, Fast DDS'in TCP taşıyıcısına
        // "kendini hangi adresle tanıt" demenin yolu. Docker'da container'ın
        // sabit IP'si veriliyor.
        tcp_transport->set_WAN_address(tcp_config_.local_ip);

        participant_qos.transport().user_transports.push_back(tcp_transport);
    }

    // Aynı domain_id'yi paylaşan participant'lar birbirini SPDP ile
    // (UDP multicast) otomatik bulur — "drone'lar birbirinin IP'sini ağ
    // üzerinden keşfeder" gereksinimi burada karşılanıyor (Bölüm 3.2).
    impl_->participant = DomainParticipantFactory::get_instance()->create_participant(
            static_cast<DomainId_t>(domain_id_), participant_qos);

    if (impl_->participant == nullptr)
    {
        std::cerr << "[FastDDSWrapper] DomainParticipant olusturulamadi "
                  << "(domain " << domain_id_ << ")" << std::endl;
        return false;
    }

    // Güvenilir kanalların ilan edeceği TCP locator. tcp_config_.enabled
    // kapalıysa nullptr geçilir ve o kanallar da UDP'de kalır.
    Locator_t tcp_locator;
    const Locator_t* tcp_locator_ptr = nullptr;
    if (tcp_config_.enabled)
    {
        tcp_locator = make_tcp_locator(tcp_config_.local_ip,
                                        tcp_config_.listening_port);
        tcp_locator_ptr = &tcp_locator;
    }

    // Bölüm 3.4'teki QoS haritası. İsimlendirilmiş sabitler, çağrı
    // yerinde `true`/`false` görmekten çok daha okunur.
    const bool BEST_EFFORT = false;
    const bool RELIABLE = true;
    const Locator_t* UDP = nullptr;   // locator ilan etme -> varsayılan UDP

    if (!impl_->heartbeat.setup(impl_->participant, "swarm/heartbeat", BEST_EFFORT, UDP) ||
        !impl_->telemetry.setup(impl_->participant, "swarm/telemetry", BEST_EFFORT, UDP) ||
        !impl_->task_alloc.setup(impl_->participant, "swarm/task_alloc", RELIABLE,
                                  tcp_locator_ptr) ||
        !impl_->consensus.setup(impl_->participant, "swarm/consensus", RELIABLE,
                                 tcp_locator_ptr))
    {
        std::cerr << "[FastDDSWrapper] Topic kurulumu basarisiz" << std::endl;
        return false;
    }

    return true;
}

bool FastDDSWrapper::publish(const Heartbeat& message)
{
    return impl_->heartbeat.publish(message);
}

bool FastDDSWrapper::publish(const Telemetry& message)
{
    return impl_->telemetry.publish(message);
}

bool FastDDSWrapper::publish(const TaskAllocation& message)
{
    return impl_->task_alloc.publish(message);
}

bool FastDDSWrapper::publish(const Consensus& message)
{
    return impl_->consensus.publish(message);
}

void FastDDSWrapper::set_heartbeat_callback(std::function<void(const Heartbeat&)> callback)
{
    impl_->heartbeat.set_callback(std::move(callback));
}

void FastDDSWrapper::set_telemetry_callback(std::function<void(const Telemetry&)> callback)
{
    impl_->telemetry.set_callback(std::move(callback));
}

void FastDDSWrapper::set_task_allocation_callback(
        std::function<void(const TaskAllocation&)> callback)
{
    impl_->task_alloc.set_callback(std::move(callback));
}

void FastDDSWrapper::set_consensus_callback(std::function<void(const Consensus&)> callback)
{
    impl_->consensus.set_callback(std::move(callback));
}

}  // namespace swarm
