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
//  DdsKanal — bir topic'in topic + writer + reader + dinleyici dörtlüsü
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
template <typename MesajTipi, typename MesajTipDestegi>
class DdsKanal : public DataReaderListener
{
public:
    using GeriCagirma = std::function<void(const MesajTipi&)>;

    bool kur(DomainParticipant* participant,
             const std::string& topic_adi,
             bool guvenilir)
    {
        participant_ = participant;

        // 1) Tipi participant'a tanıt. DDS, ağdan gelen baytları hangi
        //    sınıfa çevireceğini bu kayıttan bilir.
        tip_destegi_ = TypeSupport(new MesajTipDestegi());
        if (tip_destegi_.register_type(participant_) != RETCODE_OK)
        {
            return false;
        }

        // 2) Topic
        topic_ = participant_->create_topic(topic_adi, tip_destegi_.get_type_name(),
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

        DataWriterQos yazici_qos = DATAWRITER_QOS_DEFAULT;
        qos_uygula(yazici_qos.reliability(), yazici_qos.durability(),
                   yazici_qos.history(), guvenilir);

        writer_ = publisher_->create_datawriter(topic_, yazici_qos);
        if (writer_ == nullptr)
        {
            return false;
        }

        // 4) Subscriber + DataReader (dinleyici olarak `this`)
        subscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
        if (subscriber_ == nullptr)
        {
            return false;
        }

        DataReaderQos okuyucu_qos = DATAREADER_QOS_DEFAULT;
        qos_uygula(okuyucu_qos.reliability(), okuyucu_qos.durability(),
                   okuyucu_qos.history(), guvenilir);

        reader_ = subscriber_->create_datareader(topic_, okuyucu_qos, this);
        return reader_ != nullptr;
    }

    void kapat()
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

    bool yayinla(const MesajTipi& mesaj)
    {
        if (writer_ == nullptr)
        {
            return false;
        }

        // write() const olmayan bir işaretçi ister; kopya üzerinde
        // çalışıyoruz ki çağıranın mesajı değişmesin.
        MesajTipi kopya = mesaj;
        return writer_->write(&kopya) == RETCODE_OK;
    }

    void set_geri_cagirma(GeriCagirma geri_cagirma)
    {
        geri_cagirma_ = std::move(geri_cagirma);
    }

    // Fast DDS, bu topic'e veri geldiğinde KENDİ thread'inden bunu çağırır.
    void on_data_available(DataReader* okuyucu) override
    {
        SampleInfo bilgi;
        MesajTipi mesaj;

        // Bir bildirimde birden fazla örnek birikmiş olabilir; hepsini
        // boşaltıyoruz.
        while (okuyucu->take_next_sample(&mesaj, &bilgi) == RETCODE_OK)
        {
            // valid_data == false, "veri değil, durum değişikliği bildirimi"
            // demektir (örn. yayıncı ayrıldı). İçeriği okunmamalıdır.
            if (bilgi.valid_data && geri_cagirma_)
            {
                geri_cagirma_(mesaj);
            }
        }
    }

private:
    // QoS'u tek yerde kuruyoruz ki writer ve reader tarafi birbirinden
    // ayrismasin: UYUSMAYAN QoS'ta DDS uclari HIC ESLESMEZ ve veri akmaz.
    // Bu, DDS'te en sik yapilan hatalardan biridir ve sessizce olur -
    // hata mesaji yoktur, sadece veri gelmez.
    static void qos_uygula(
            ReliabilityQosPolicy& guvenilirlik,
            DurabilityQosPolicy& kalicilik,
            HistoryQosPolicy& gecmis,
            bool guvenilir)
    {
        if (guvenilir)
        {
            // --- Komut ve consensus kanallari (Bolum 3.4) ---
            // RELIABLE: DDS teslim edilmeyen ornekleri yeniden gonderir;
            // %100 ulastirma garantisi. Gorev emrinin kaybolmasi kabul
            // edilemez.
            guvenilirlik.kind = RELIABLE_RELIABILITY_QOS;

            // TRANSIENT_LOCAL: yayinci son orneklerini saklar ve SONRADAN
            // katilan bir aboneye de gonderir. Emir yayinlandiktan sonra aga
            // giren bir drone son gorev emrini yine de gorebilsin diye.
            kalicilik.kind = TRANSIENT_LOCAL_DURABILITY_QOS;

            gecmis.kind = KEEP_LAST_HISTORY_QOS;
            gecmis.depth = 10;
        }
        else
        {
            // --- Heartbeat ve telemetri kanallari ---
            // BEST_EFFORT: kaybolan paket yeniden gonderilmez. Yuksek
            // frekansli (10-50 Hz) bir akista tazelik, eksiksizlikten daha
            // onemlidir; eski bir konumu tekrar gondermenin degeri yoktur.
            guvenilirlik.kind = BEST_EFFORT_RELIABILITY_QOS;

            // VOLATILE: gecmis saklanmaz. Sonradan katilan bir dugumun eski
            // heartbeat'lere ihtiyaci yok, bir sonrakini zaten 100 ms icinde
            // alacak.
            kalicilik.kind = VOLATILE_DURABILITY_QOS;

            gecmis.kind = KEEP_LAST_HISTORY_QOS;
            gecmis.depth = 1;   // yalnizca en tazesi ilgilendiriyor
        }
    }

    DomainParticipant* participant_ = nullptr;
    TypeSupport tip_destegi_;
    Topic* topic_ = nullptr;
    Publisher* publisher_ = nullptr;
    DataWriter* writer_ = nullptr;
    Subscriber* subscriber_ = nullptr;
    DataReader* reader_ = nullptr;
    GeriCagirma geri_cagirma_;
};

}  // namespace

// ---------------------------------------------------------------------------
//  FastDDSWrapper::Icerik — gerçek üyeler (PIMPL)
// ---------------------------------------------------------------------------

struct FastDDSWrapper::Icerik
{
    DomainParticipant* participant = nullptr;

    DdsKanal<Heartbeat, HeartbeatPubSubType> heartbeat;
    DdsKanal<Telemetry, TelemetryPubSubType> telemetry;
    DdsKanal<TaskAllocation, TaskAllocationPubSubType> task_alloc;
    DdsKanal<Consensus, ConsensusPubSubType> consensus;
};

FastDDSWrapper::FastDDSWrapper(uint32_t domain_id)
    : icerik_(std::make_unique<Icerik>())
    , domain_id_(domain_id)
{
}

FastDDSWrapper::~FastDDSWrapper()
{
    icerik_->heartbeat.kapat();
    icerik_->telemetry.kapat();
    icerik_->task_alloc.kapat();
    icerik_->consensus.kapat();

    if (icerik_->participant != nullptr)
    {
        icerik_->participant->delete_contained_entities();
        DomainParticipantFactory::get_instance()->delete_participant(icerik_->participant);
        icerik_->participant = nullptr;
    }
}

bool FastDDSWrapper::init()
{
    DomainParticipantQos participant_qos = PARTICIPANT_QOS_DEFAULT;
    participant_qos.name("swarm_node");

    // Aynı domain_id'yi paylaşan participant'lar birbirini SPDP ile
    // (UDP multicast) otomatik bulur — "drone'lar birbirinin IP'sini ağ
    // üzerinden keşfeder" gereksinimi burada karşılanıyor (Bölüm 3.2).
    icerik_->participant = DomainParticipantFactory::get_instance()->create_participant(
            static_cast<DomainId_t>(domain_id_), participant_qos);

    if (icerik_->participant == nullptr)
    {
        std::cerr << "[FastDDSWrapper] DomainParticipant olusturulamadi "
                  << "(domain " << domain_id_ << ")" << std::endl;
        return false;
    }

    // Bölüm 3.4'teki QoS haritası. İsimlendirilmiş sabitler, çağrı
    // yerinde `true`/`false` görmekten çok daha okunur.
    const bool BEST_EFFORT = false;
    const bool GUVENILIR = true;

    if (!icerik_->heartbeat.kur(icerik_->participant, "swarm/heartbeat", BEST_EFFORT) ||
        !icerik_->telemetry.kur(icerik_->participant, "swarm/telemetry", BEST_EFFORT) ||
        !icerik_->task_alloc.kur(icerik_->participant, "swarm/task_alloc", GUVENILIR) ||
        !icerik_->consensus.kur(icerik_->participant, "swarm/consensus", GUVENILIR))
    {
        std::cerr << "[FastDDSWrapper] Topic kurulumu basarisiz" << std::endl;
        return false;
    }

    return true;
}

bool FastDDSWrapper::publish(const Heartbeat& mesaj)
{
    return icerik_->heartbeat.yayinla(mesaj);
}

bool FastDDSWrapper::publish(const Telemetry& mesaj)
{
    return icerik_->telemetry.yayinla(mesaj);
}

bool FastDDSWrapper::publish(const TaskAllocation& mesaj)
{
    return icerik_->task_alloc.yayinla(mesaj);
}

bool FastDDSWrapper::publish(const Consensus& mesaj)
{
    return icerik_->consensus.yayinla(mesaj);
}

void FastDDSWrapper::set_heartbeat_callback(std::function<void(const Heartbeat&)> geri_cagirma)
{
    icerik_->heartbeat.set_geri_cagirma(std::move(geri_cagirma));
}

void FastDDSWrapper::set_telemetry_callback(std::function<void(const Telemetry&)> geri_cagirma)
{
    icerik_->telemetry.set_geri_cagirma(std::move(geri_cagirma));
}

void FastDDSWrapper::set_task_allocation_callback(
        std::function<void(const TaskAllocation&)> geri_cagirma)
{
    icerik_->task_alloc.set_geri_cagirma(std::move(geri_cagirma));
}

void FastDDSWrapper::set_consensus_callback(std::function<void(const Consensus&)> geri_cagirma)
{
    icerik_->consensus.set_geri_cagirma(std::move(geri_cagirma));
}

}  // namespace swarm
