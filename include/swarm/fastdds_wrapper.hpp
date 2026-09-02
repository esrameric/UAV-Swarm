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
//  QoS VE TAŞIYICI HARİTASI (Bölüm 3.4):
//    swarm/heartbeat  -> UDP, Best-Effort, Volatile     (~10 Hz, tazelik esas)
//    swarm/telemetry  -> UDP, Best-Effort, Volatile     (20-50 Hz)
//    swarm/task_alloc -> TCP, Reliable, Transient Local (kayıp kabul edilemez)
//    swarm/consensus  -> TCP, Reliable, Transient Local (kayıp kabul edilemez)
//
//  TAŞIYICI (TRANSPORT) NASIL TOPIC BAZINDA SEÇİLİYOR?
//  Yaygın yanılgı, DDS'te taşıyıcının yalnızca participant seviyesinde
//  seçilebildiğidir. Doğrusu iki katmanlı:
//
//    1) Participant, HANGİ taşıyıcıları AÇACAĞINA karar verir. Bizim
//       participant'ımız hem varsayılan UDP'yi hem de bir TCPv4 taşıyıcısını
//       birlikte açar (bkz. init()).
//    2) Her DataReader/DataWriter, keşif (SEDP) sırasında "bana şu adresten
//       ulaş" diye bir LOCATOR listesi ilan eder. Bir endpoint'e elle TCP
//       locator verilirse, o endpoint'in ilan ettiği liste yalnızca TCP olur
//       (varsayılan UDP/SHM locator'ları YERİNE geçer, onlara eklenmez) ve
//       karşı taraf o topic'in verisini TCP soketi açarak gönderir.
//
//  Sonuç: keşif hâlâ UDP multicast (SPDP) ile otomatik yürür — hiçbir düğümün
//  bir başkasının IP'sini önceden bilmesi gerekmez (Bölüm 2). Yalnızca düğüm
//  KENDİ IP'sini bilir, çünkü ilan edeceği locator'a onu yazar.
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

// TCP taşıyıcısının yapılandırması (Bölüm 3.4).
//
// enabled == false iken sistem tümüyle eski davranışına döner: her topic
// participant'ın varsayılan UDP taşıyıcısını kullanır. Bu, TCP'nin kurulamadığı
// ortamlarda (ör. düğümün kendi IP'si bilinmiyorsa) sistemin yine de
// çalışabilmesi için bilinçli bir geri çekilme yoludur.
struct TcpTransportConfig
{
    bool enabled = false;

    // Bu düğümün KENDİ IP adresi. Karşı tarafın bağlanacağı adres olarak
    // ilan edilir; bu yüzden ağdan erişilebilir olmalıdır (Docker'da
    // container'ın sabit IP'si).
    std::string local_ip;

    // Bu düğümün TCP dinleme portu. Tüm düğümler aynı portu kullanabilir,
    // çünkü IP'leri farklıdır. Aynı makinede birden fazla düğüm çalışacaksa
    // (birim testleri) portların farklı olması gerekir.
    uint16_t listening_port = 5100;
};

class FastDDSWrapper
{
public:
    // tcp_config varsayılan (enabled == false) bırakılırsa tüm topic'ler
    // UDP üzerinden gider.
    explicit FastDDSWrapper(uint32_t domain_id, TcpTransportConfig tcp_config = {});

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
    TcpTransportConfig tcp_config_;
};

}  // namespace swarm
