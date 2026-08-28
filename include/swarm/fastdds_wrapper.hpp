// ============================================================================
//  FastDDSWrapper — Fast DDS ayrıntılarını tek yerde toplayan katman
//
//  Amaç: SwarmManager'ın DDS'i hiç tanımaması. Uygulama mantığı "heartbeat
//  yayınla" der; hangi DomainParticipant'ın, hangi DataWriter'ın, hangi QoS
//  ile bunu yaptığı buranın işidir.
//
//  DDS KAVRAMLARI (kısa sözlük — ayrıntısı README'de):
//    DomainParticipant : Ağdaki bir düğümün DDS'teki karşılığı. Aynı
//                        domain_id'yi paylaşan participant'lar birbirini
//                        otomatik bulur (discovery).
//    Topic             : İsimlendirilmiş bir veri akışı ("swarm/heartbeat").
//    DataWriter        : Bir topic'e veri yazan uç (yayıncı).
//    DataReader        : Bir topic'ten veri okuyan uç (abone).
//    QoS               : "Bu akış nasıl davransın?" ayarları — güvenilirlik,
//                        kalıcılık, geçmiş derinliği.
//
//  Bölüm 3.4'teki dört topic burada kurulur. Topic bazlı QoS ayrımı
//  (Best-Effort vs Reliable) Faz 4.2'de eklenir.
// ============================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "Consensus.hpp"
#include "Heartbeat.hpp"
#include "TaskAllocation.hpp"
#include "Telemetry.hpp"

namespace eprosima {
namespace fastdds {
namespace dds {
class DomainParticipant;
}  // namespace dds
}  // namespace fastdds
}  // namespace eprosima

namespace swarm {

class FastDDSWrapper
{
public:
    explicit FastDDSWrapper(uint32_t domain_id);

    // YIKICI: RAII. Nesne kapsam dışına çıkınca tüm DDS varlıkları
    // (writer, reader, topic, participant) doğru sırayla silinir. Elle
    // temizlik çağırmayı unutma ihtimali kalmaz.
    ~FastDDSWrapper();

    FastDDSWrapper(const FastDDSWrapper&) = delete;
    FastDDSWrapper& operator=(const FastDDSWrapper&) = delete;

    // DDS varlıklarını kurar. Başarısız olursa false döner ve yarım kalan
    // ne varsa temizler.
    bool init();

    // --- Yayın ---------------------------------------------------------------
    bool publish(const Heartbeat& mesaj);
    bool publish(const Telemetry& mesaj);
    bool publish(const TaskAllocation& mesaj);
    bool publish(const Consensus& mesaj);

    // --- Alım ----------------------------------------------------------------
    // Mesaj geldiğinde çağrılacak fonksiyonlar. DDS'in kendi thread'inden
    // çağrılırlar; bu yüzden içlerinde uzun iş yapılmamalı, veri hemen
    // SwarmManager'ın kuyruğuna bırakılmalıdır.
    void set_heartbeat_callback(std::function<void(const Heartbeat&)> geri_cagirma);
    void set_telemetry_callback(std::function<void(const Telemetry&)> geri_cagirma);
    void set_task_allocation_callback(std::function<void(const TaskAllocation&)> geri_cagirma);
    void set_consensus_callback(std::function<void(const Consensus&)> geri_cagirma);

private:
    // PIMPL ("pointer to implementation"): Fast DDS başlıklarının tamamını
    // bu .hpp'ye dahil etmemek için gerçek üyeler bir .cpp içindeki gizli
    // sınıfta tutulur. Kazancı: bu başlığı #include eden dosyaların Fast
    // DDS'i tanımasına gerek kalmaz ve derleme süresi kısalır.
    struct Icerik;
    std::unique_ptr<Icerik> icerik_;

    uint32_t domain_id_;
};

}  // namespace swarm
