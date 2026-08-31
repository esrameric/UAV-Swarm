#include "swarm/task/consensus_task.hpp"

namespace swarm {

ConsensusTask::ConsensusTask(
        uint32_t transaction_id,
        std::vector<uint8_t> beklenen_oy_verenler,
        std::chrono::milliseconds timeout)
    : transaction_id_(transaction_id)
    , timeout_(timeout)
{
    for (const uint8_t oy_veren_id : beklenen_oy_verenler)
    {
        oylar_[oy_veren_id] = Vote::PENDING;
    }
}

void ConsensusTask::on_enter(TimePoint now)
{
    // Zaman aşımı sayacı, teklifin yayınlandığı andan itibaren işler.
    baslangic_ = now;
    sonuc_ = ConsensusResult::PENDING;
    timeout_ile_iptal_ = false;

    // Oylamanın hiç oy verecek kimsesi yoksa beklemek anlamsız: tek başına
    // kalmış bir düğüm burada sonsuza kadar kilitlenmemeli.
    if (oylar_.empty())
    {
        sonuc_ = ConsensusResult::COMMITTED;
        return;
    }

    // on_enter çağrılmadan ÖNCE oy gelmiş olabilir (ağ, Task Engine'in
    // görevi başlatmasından hızlı davranabilir). Yukarıdaki sıfırlama böyle
    // bir durumda verilmiş kararı silerdi; bu yüzden eldeki oyları burada
    // yeniden değerlendiriyoruz.
    for (const auto& giris : oylar_)
    {
        if (giris.second == Vote::NACK)
        {
            sonuc_ = ConsensusResult::ABORTED;
            return;
        }
    }
    sonucu_degerlendir();
}

void ConsensusTask::run(TimePoint now)
{
    // Oylar ağdan asenkron olarak on_vote() ile geliyor. run()'ın tek işi
    // zaman aşımını kollamak.
    if (sonuc_ != ConsensusResult::PENDING)
    {
        return;  // sonuç zaten belli
    }

    const auto gecen = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - baslangic_);

    if (gecen < timeout_)
    {
        return;
    }

    // Süre doldu ve hâlâ tam ACK yok. Eksik cevap veren düğümler PENDING'de
    // kaldı. Heterojen bir sürüde 1 drone eksikken göreve başlamak riskli
    // olduğu için tüm görev iptal edilir (Bölüm 2).
    sonuc_ = ConsensusResult::ABORTED;
    timeout_ile_iptal_ = true;
}

void ConsensusTask::on_exit()
{
}

bool ConsensusTask::is_finished() const
{
    return sonuc_ != ConsensusResult::PENDING;
}

TaskType ConsensusTask::get_type() const
{
    return TaskType::CONSENSUS;
}

void ConsensusTask::on_vote(uint8_t gonderen_id, Vote oy)
{
    // Sonuç belli olduktan sonra gelen oylar kararı değiştirmez.
    if (sonuc_ != ConsensusResult::PENDING)
    {
        return;
    }

    auto bulunan = oylar_.find(gonderen_id);
    if (bulunan == oylar_.end())
    {
        // Oyu beklenmeyen bir düğümden gelen mesaj yok sayılır.
        return;
    }

    bulunan->second = oy;

    if (oy == Vote::NACK)
    {
        // Tek bir NACK yeterli: 2PC'de karar oybirliği gerektirir.
        // Diğerlerini beklemeye gerek yok.
        sonuc_ = ConsensusResult::ABORTED;
        return;
    }

    sonucu_degerlendir();
}

void ConsensusTask::sonucu_degerlendir()
{
    for (const auto& giris : oylar_)
    {
        if (giris.second != Vote::ACK)
        {
            return;  // en az bir düğüm henüz ACK vermemiş
        }
    }

    sonuc_ = ConsensusResult::COMMITTED;
}

Vote ConsensusTask::oy_durumu(uint8_t drone_id) const
{
    const auto bulunan = oylar_.find(drone_id);
    if (bulunan == oylar_.end())
    {
        return Vote::PENDING;
    }
    return bulunan->second;
}

}  // namespace swarm
