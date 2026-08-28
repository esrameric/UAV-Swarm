#include "swarm/peer_manager.hpp"

namespace swarm {

PeerManager::PeerManager(std::chrono::milliseconds heartbeat_timeout)
    // Kurucu başlatma listesi (constructor initializer list): üyeler gövdeye
    // girilmeden önce doğrudan bu değerlerle kurulur. Gövde içinde atama
    // yapmaktan hem daha verimli hem de const üyeler için tek yoldur.
    : heartbeat_timeout_(heartbeat_timeout)
{
}

void PeerManager::on_heartbeat(
        const Heartbeat& heartbeat,
        std::chrono::steady_clock::time_point now)
{
    const uint8_t gonderen_id = heartbeat.drone_id();

    // operator[] : anahtar tabloda yoksa varsayılan kurulmuş bir kayıt
    // oluşturup ekler. Yani "yoksa ekle, varsa getir" tek satırda olur.
    PeerRecord& kayit = peers_[gonderen_id];

    const bool geri_dondu = (kayit.status == PeerStatus::OFFLINE);

    // Heartbeat'ten gelen bilgileri tazeliyoruz.
    kayit.info.drone_id = gonderen_id;
    kayit.info.node_type = heartbeat.node_type();
    kayit.info.role = heartbeat.role();
    kayit.info.current_task = heartbeat.current_task();

    // Canlılık damgası: gönderenin saati değil, BİZİM şu anki saatimiz.
    kayit.info.last_heartbeat_local = now;

    if (geri_dondu)
    {
        // OFFLINE -> ONLINE geçişi (ilk kez görülen peer de buraya düşer,
        // çünkü yeni eklenen kayıt varsayılan olarak OFFLINE'dır).
        //
        // Drone yeniden başladıysa telemetri sayacı sıfırdan başlayacak.
        // Eski yüksek seq değerini saklamaya devam edersek, gelen taze
        // veriyi "bayat" diye reddeder ve drone'u bir daha hiç görmeyiz.
        kayit.info.last_seen_seq = 0;
        kayit.status = PeerStatus::ONLINE;
    }
}

bool PeerManager::on_telemetry(
        const Telemetry& telemetry,
        std::chrono::steady_clock::time_point now)
{
    // find(): anahtarı arar, bulamazsa end() döner. operator[]'in aksine
    // tabloya kayıt EKLEMEZ — telemetri, heartbeat'le tanışmadığımız bir
    // peer'ı tabloya sokmamalı.
    auto bulunan = peers_.find(telemetry.drone_id());
    if (bulunan == peers_.end())
    {
        return false;
    }

    PeerRecord& kayit = bulunan->second;

    // Bayatlık kontrolü: yalnızca sayacı İLERLETEN paketler kabul edilir.
    // UDP paket sırasını bozabildiği için bu kontrol şart. Eşit olan da
    // reddedilir (aynı paketin tekrarı).
    //
    // Not: last_seen_seq == 0 ve gelen seq_num == 0 durumunda paket
    // reddedilir; drone'un ilk telemetri paketi 1'den başlar.
    if (telemetry.seq_num() <= kayit.info.last_seen_seq)
    {
        return false;
    }

    kayit.info.last_seen_seq = telemetry.seq_num();

    // Telemetri de peer'ın yaşadığının kanıtıdır.
    kayit.info.last_heartbeat_local = now;
    kayit.status = PeerStatus::ONLINE;

    return true;
}

void PeerManager::refresh_status(std::chrono::steady_clock::time_point now)
{
    // `auto&` ve range-for: tablodaki her kaydı tek tek geziyoruz.
    // `&` şart — kopya üzerinde değil, tablodaki asıl kayıt üzerinde
    // çalışmak istiyoruz.
    for (auto& giris : peers_)
    {
        PeerRecord& kayit = giris.second;

        if (kayit.status != PeerStatus::ONLINE)
        {
            continue;
        }

        const auto sessizlik_suresi = now - kayit.info.last_heartbeat_local;

        // duration_cast: iki zaman noktası arasındaki farkı milisaniyeye
        // çevirir, böylece timeout ile aynı birimde karşılaştırabiliriz.
        const auto sessizlik_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(sessizlik_suresi);

        if (sessizlik_ms >= heartbeat_timeout_)
        {
            kayit.status = PeerStatus::OFFLINE;
        }
    }
}

PeerStatus PeerManager::status_of(uint8_t drone_id) const
{
    const auto bulunan = peers_.find(drone_id);
    if (bulunan == peers_.end())
    {
        // Hiç duymadığımız bir peer, OFFLINE sayılır.
        return PeerStatus::OFFLINE;
    }
    return bulunan->second.status;
}

const PeerRecord* PeerManager::find(uint8_t drone_id) const
{
    const auto bulunan = peers_.find(drone_id);
    if (bulunan == peers_.end())
    {
        return nullptr;
    }
    // `&` ile kaydın adresini döndürüyoruz; kopya değil, tablodaki asıl kayıt.
    return &bulunan->second;
}

std::size_t PeerManager::peer_count() const
{
    return peers_.size();
}

std::size_t PeerManager::online_peer_count() const
{
    std::size_t sayac = 0;
    for (const auto& giris : peers_)
    {
        if (giris.second.status == PeerStatus::ONLINE)
        {
            ++sayac;
        }
    }
    return sayac;
}

std::size_t PeerManager::offline_peer_count() const
{
    std::size_t sayac = 0;
    for (const auto& giris : peers_)
    {
        if (giris.second.status == PeerStatus::OFFLINE)
        {
            ++sayac;
        }
    }
    return sayac;
}

}  // namespace swarm
