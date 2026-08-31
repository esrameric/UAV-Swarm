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
    const uint8_t sender_id = heartbeat.drone_id();

    // operator[] : anahtar tabloda yoksa varsayılan kurulmuş bir kayıt
    // oluşturup ekler. Yani "yoksa ekle, varsa getir" tek satırda olur.
    PeerRecord& record = peers_[sender_id];

    const bool returned = (record.status == PeerStatus::OFFLINE);

    // Heartbeat'ten gelen bilgileri tazeliyoruz.
    record.info.drone_id = sender_id;
    record.info.node_type = heartbeat.node_type();
    record.info.role = heartbeat.role();
    record.info.current_task = heartbeat.current_task();

    // Canlılık damgası: gönderenin saati değil, BİZİM şu anki saatimiz.
    record.info.last_heartbeat_local = now;

    if (returned)
    {
        // OFFLINE -> ONLINE geçişi (ilk kez görülen peer de buraya düşer,
        // çünkü yeni eklenen kayıt varsayılan olarak OFFLINE'dır).
        //
        // Drone yeniden başladıysa telemetri sayacı sıfırdan başlayacak.
        // Eski yüksek seq değerini saklamaya devam edersek, gelen taze
        // veriyi "bayat" diye reddeder ve drone'u bir daha hiç görmeyiz.
        record.info.last_seen_seq = 0;
        record.status = PeerStatus::ONLINE;
    }
}

bool PeerManager::on_telemetry(
        const Telemetry& telemetry,
        std::chrono::steady_clock::time_point now,
        bool* new_stream_started)
{
    if (new_stream_started != nullptr)
    {
        *new_stream_started = false;
    }

    // find(): anahtarı arar, bulamazsa end() döner. operator[]'in aksine
    // tabloya kayıt EKLEMEZ — telemetri, heartbeat'le tanışmadığımız bir
    // peer'ı tabloya sokmamalı.
    auto it = peers_.find(telemetry.drone_id());
    if (it == peers_.end())
    {
        return false;
    }

    PeerRecord& record = it->second;

    // --- Restart tespiti ----------------------------------------------------
    // Sayaç büyük bir sıçramayla geriye gittiyse gönderici yeniden başlamıştır
    // (bkz. RESTART_TESPIT_ESIGI). Bu durumda takibi sıfırlıyoruz ki taze veri
    // "bayat" diye reddedilmesin. Bu, OFFLINE -> ONLINE sıfırlamasının
    // yakalayamadığı "hızlı restart" durumunu kapatan güvenlik ağıdır.
    const bool large_backward_jump =
            (telemetry.seq_num() + RESTART_DETECTION_THRESHOLD) < record.info.last_seen_seq;

    if (large_backward_jump)
    {
        record.info.last_seen_seq = 0;
    }

    // Bayatlık kontrolü: yalnızca sayacı İLERLETEN paketler kabul edilir.
    // UDP paket sırasını bozabildiği için bu kontrol şart. Eşit olan da
    // reddedilir (aynı paketin tekrarı).
    //
    // Not: last_seen_seq == 0 ve gelen seq_num == 0 durumunda paket
    // reddedilir; drone'un ilk telemetri paketi 1'den başlar.
    if (telemetry.seq_num() <= record.info.last_seen_seq)
    {
        return false;
    }

    if (new_stream_started != nullptr)
    {
        // Sayaç sıfırdan başlıyorsa bu, o peer'dan gelen ilk pakettir:
        // ya hiç duymamıştık ya da az önce restart tespit edildi.
        *new_stream_started = (record.info.last_seen_seq == 0);
    }

    record.info.last_seen_seq = telemetry.seq_num();

    // Telemetri de peer'ın yaşadığının kanıtıdır.
    record.info.last_heartbeat_local = now;
    record.status = PeerStatus::ONLINE;

    return true;
}

void PeerManager::refresh_status(std::chrono::steady_clock::time_point now)
{
    // `auto&` ve range-for: tablodaki her kaydı tek tek geziyoruz.
    // `&` şart — kopya üzerinde değil, tablodaki asıl kayıt üzerinde
    // çalışmak istiyoruz.
    for (auto& entry : peers_)
    {
        PeerRecord& record = entry.second;

        if (record.status != PeerStatus::ONLINE)
        {
            continue;
        }

        const auto silence_duration = now - record.info.last_heartbeat_local;

        // duration_cast: iki zaman noktası arasındaki farkı milisaniyeye
        // çevirir, böylece timeout ile aynı birimde karşılaştırabiliriz.
        const auto silence_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(silence_duration);

        if (silence_ms >= heartbeat_timeout_)
        {
            record.status = PeerStatus::OFFLINE;
        }
    }
}

PeerStatus PeerManager::status_of(uint8_t drone_id) const
{
    const auto it = peers_.find(drone_id);
    if (it == peers_.end())
    {
        // Hiç duymadığımız bir peer, OFFLINE sayılır.
        return PeerStatus::OFFLINE;
    }
    return it->second.status;
}

const PeerRecord* PeerManager::find(uint8_t drone_id) const
{
    const auto it = peers_.find(drone_id);
    if (it == peers_.end())
    {
        return nullptr;
    }
    // `&` ile kaydın adresini döndürüyoruz; kopya değil, tablodaki asıl kayıt.
    return &it->second;
}

std::size_t PeerManager::peer_count() const
{
    return peers_.size();
}

std::size_t PeerManager::online_peer_count() const
{
    std::size_t counter = 0;
    for (const auto& entry : peers_)
    {
        if (entry.second.status == PeerStatus::ONLINE)
        {
            ++counter;
        }
    }
    return counter;
}

std::vector<uint8_t> PeerManager::online_drone_ids() const
{
    std::vector<uint8_t> ids;
    for (const auto& entry : peers_)
    {
        const PeerRecord& record = entry.second;
        if (record.status == PeerStatus::ONLINE && record.info.node_type == NodeType::DRONE)
        {
            ids.push_back(record.info.drone_id);
        }
    }
    // std::map anahtara gore sirali oldugu icin liste de sirali cikar.
    return ids;
}

std::size_t PeerManager::offline_peer_count() const
{
    std::size_t counter = 0;
    for (const auto& entry : peers_)
    {
        if (entry.second.status == PeerStatus::OFFLINE)
        {
            ++counter;
        }
    }
    return counter;
}

}  // namespace swarm
