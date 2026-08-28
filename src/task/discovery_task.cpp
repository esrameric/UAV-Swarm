#include "swarm/task/discovery_task.hpp"

namespace swarm {

DiscoveryTask::DiscoveryTask(
        const PeerManager& peer_yoneticisi,
        std::chrono::milliseconds azami_bekleme)
    : peer_yoneticisi_(peer_yoneticisi)
    , azami_bekleme_(azami_bekleme)
{
}

void DiscoveryTask::on_enter(TimePoint now)
{
    baslangic_ = now;
    peer_bulundu_ = false;
    tamamlandi_ = false;
}

void DiscoveryTask::run(TimePoint now)
{
    if (peer_yoneticisi_.online_peer_count() > 0)
    {
        peer_bulundu_ = true;
        tamamlandi_ = true;
        return;
    }

    const auto gecen = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - baslangic_);

    if (gecen >= azami_bekleme_)
    {
        // Kimse duyulmadı ama beklemeye devam etmiyoruz: var olanlarla
        // (yani yalnız başımıza) devam ederiz.
        tamamlandi_ = true;
    }
}

void DiscoveryTask::on_exit()
{
}

bool DiscoveryTask::is_finished() const
{
    return tamamlandi_;
}

TaskType DiscoveryTask::get_type() const
{
    return TaskType::DISCOVERY;
}

}  // namespace swarm
