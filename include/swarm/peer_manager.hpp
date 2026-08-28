// ============================================================================
//  PeerManager — peer table'ı yöneten sınıf
//
//  Sorumluluğu: gelen heartbeat ve telemetri mesajlarına göre "sürüyü şu an
//  nasıl görüyorum" bilgisini güncel tutmak.
//
//  İKİ KRİTİK KURAL BURADA YAŞIYOR (Bölüm 3.5):
//
//  1) CANLILIK, ALICININ SAATİYLE ÖLÇÜLÜR.
//     Bir peer'ın "sustuğuna" karar verirken gönderenin zaman damgasına
//     bakmayız — son heartbeat'i aldığımız andaki kendi steady_clock
//     değerimizle şimdiki zamanı kıyaslarız.
//
//  2) OFFLINE -> ONLINE GEÇİŞİNDE last_seen_seq SIFIRLANIR.
//     Bir drone yeniden başladığında süreci sıfırdan başlar, dolayısıyla
//     telemetri sayacı da 0'dan başlar. Alıcı tarafta eski (yüksek) seq
//     değeri saklı kalırsa, restart sonrası gelen TAZE veri (düşük seq)
//     hatalı şekilde "bayat" diye reddedilir ve drone bir daha hiç
//     görünmez. Bu yüzden peer geri döndüğünde sayaç takibi sıfırlanır.
//
//  ZAMAN DIŞARIDAN VERİLİR. Fonksiyonlar `now` parametresi alır, içeride
//  `steady_clock::now()` çağırmaz. Bunun sebebi test edilebilirlik: zaman
//  aşımı senaryosunu gerçekten 3 saniye bekleyerek değil, ileri bir zaman
//  değeri vererek anında test edebiliyoruz.
// ============================================================================

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>

#include "Heartbeat.hpp"
#include "Telemetry.hpp"
#include "swarm/peer_info.hpp"

namespace swarm {

// Bir peer'ı şu an duyuyor muyuz?
enum class PeerStatus
{
    OFFLINE,   // Zaman aşımı süresince heartbeat gelmedi
    ONLINE     // Yakın zamanda heartbeat alındı
};

// Bir peer hakkında sakladığımız her şey: bilgisi + canlılık durumu.
struct PeerRecord
{
    PeerInfo info{};
    PeerStatus status = PeerStatus::OFFLINE;
};

class PeerManager
{
public:
    // Bir peer'dan bu süre boyunca heartbeat gelmezse OFFLINE sayılır.
    static constexpr std::chrono::milliseconds VARSAYILAN_HEARTBEAT_TIMEOUT{3000};

    // `explicit`: tek parametreli kurucuların istenmeyen örtük (implicit)
    // dönüşüm yapmasını engeller. Bu olmasaydı bir fonksiyona yanlışlıkla
    // süre verildiğinde derleyici onu sessizce PeerManager'a çevirebilirdi.
    explicit PeerManager(
            std::chrono::milliseconds heartbeat_timeout = VARSAYILAN_HEARTBEAT_TIMEOUT);

    // Bir heartbeat alındığında çağrılır.
    // Peer tabloda yoksa eklenir. Peer OFFLINE'dan ONLINE'a geçiyorsa
    // (veya ilk kez görülüyorsa) last_seen_seq sıfırlanır.
    void on_heartbeat(
            const Heartbeat& heartbeat,
            std::chrono::steady_clock::time_point now);

    // Bir telemetri paketi alındığında çağrılır.
    // Dönüş: paket kabul edildiyse true, bayat/tekrar olduğu için
    //        atıldıysa false.
    bool on_telemetry(
            const Telemetry& telemetry,
            std::chrono::steady_clock::time_point now);

    // Zaman aşımına uğramış peer'ları OFFLINE olarak işaretler.
    // Düzenli aralıklarla (heartbeat döngüsünden) çağrılması beklenir.
    void refresh_status(std::chrono::steady_clock::time_point now);

    // --- Sorgular ------------------------------------------------------------

    PeerStatus status_of(uint8_t drone_id) const;

    // Peer tabloda yoksa nullptr döner.
    // Ham işaretçi (raw pointer) döndürüyoruz ama SAHİPLİK devretmiyoruz:
    // işaret edilen kayıt PeerManager'a aittir, çağıran onu silmemelidir.
    const PeerRecord* find(uint8_t drone_id) const;

    std::size_t peer_count() const;

private:
    // std::map: anahtar-değer saklayan, anahtara göre sıralı tutan kap.
    // 3-4 peer'lık bir tabloda performans önemsiz; sıralı olması logları
    // ve testleri öngörülebilir kılıyor.
    std::map<uint8_t, PeerRecord> peers_;

    std::chrono::milliseconds heartbeat_timeout_;
};

}  // namespace swarm
