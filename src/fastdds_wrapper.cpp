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

#include "ConsensusPubSubTypes.hpp"
#include "HeartbeatPubSubTypes.hpp"
#include "TaskAllocationPubSubTypes.hpp"
#include "TelemetryPubSubTypes.hpp"

namespace swarm {

// `using namespace` yalnızca bu .cpp dosyasının içinde geçerli; Fast DDS'in
// uzun isim alanını (eprosima::fastdds::dds::...) her satırda yazmamak için.
using namespace eprosima::fastdds::dds;

namespace {

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

    bool setup(DomainParticipant* participant,
             const std::string& topic_name,
             bool reliable)
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

FastDDSWrapper::FastDDSWrapper(uint32_t domain_id)
    : impl_(std::make_unique<Impl>())
    , domain_id_(domain_id)
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

    // Bölüm 3.4'teki QoS haritası. İsimlendirilmiş sabitler, çağrı
    // yerinde `true`/`false` görmekten çok daha okunur.
    const bool BEST_EFFORT = false;
    const bool RELIABLE = true;

    if (!impl_->heartbeat.setup(impl_->participant, "swarm/heartbeat", BEST_EFFORT) ||
        !impl_->telemetry.setup(impl_->participant, "swarm/telemetry", BEST_EFFORT) ||
        !impl_->task_alloc.setup(impl_->participant, "swarm/task_alloc", RELIABLE) ||
        !impl_->consensus.setup(impl_->participant, "swarm/consensus", RELIABLE))
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
