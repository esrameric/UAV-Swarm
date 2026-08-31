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
//  QoS HARİTASI (Bölüm 3.4):
//    swarm/heartbeat  -> Best-Effort, Volatile          (~10 Hz, tazelik esas)
//    swarm/telemetry  -> Best-Effort, Volatile          (20-50 Hz)
//    swarm/task_alloc -> Reliable, Transient Local      (kayıp kabul edilemez)
//    swarm/consensus  -> Reliable, Transient Local      (kayıp kabul edilemez)
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
    bool publish(const Heartbeat& message);
    bool publish(const Telemetry& message);
    bool publish(const TaskAllocation& message);
    bool publish(const Consensus& message);

    // --- Alım ----------------------------------------------------------------
    // Mesaj geldiğinde çağrılacak fonksiyonlar. DDS'in kendi thread'inden
    // çağrılırlar; bu yüzden içlerinde uzun iş yapılmamalı, veri hemen
    // SwarmManager'ın queue'suna bırakılmalıdır.
    void set_heartbeat_callback(std::function<void(const Heartbeat&)> callback);
    void set_telemetry_callback(std::function<void(const Telemetry&)> callback);
    void set_task_allocation_callback(std::function<void(const TaskAllocation&)> callback);
    void set_consensus_callback(std::function<void(const Consensus&)> callback);

private:
    // PIMPL ("pointer to implementation"): Fast DDS başlıklarının tamamını
    // bu .hpp'ye dahil etmemek için gerçek üyeler bir .cpp içindeki gizli
    // sınıfta tutulur. Kazancı: bu başlığı #include eden dosyaların Fast
    // DDS'i tanımasına gerek kalmaz ve derleme süresi kısalır.
    struct Impl;
    std::unique_ptr<Impl> impl_;

    uint32_t domain_id_;
};

}  // namespace swarm
