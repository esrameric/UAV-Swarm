#include "swarm/task/consensus_task.hpp"

namespace swarm {

ConsensusTask::ConsensusTask(
        uint32_t transaction_id,
        std::vector<uint8_t> expected_voters,
        std::chrono::milliseconds timeout)
    : transaction_id_(transaction_id)
    , timeout_(timeout)
{
    for (const uint8_t voter_id : expected_voters)
    {
        votes_[voter_id] = Vote::PENDING;
    }
}

void ConsensusTask::on_enter(TimePoint now)
{
    // Zaman aşımı sayacı, teklifin yayınlandığı andan itibaren işler.
    start_ = now;
    result_ = ConsensusResult::PENDING;
    cancelled_by_timeout_ = false;

    // Oylamanın hiç oy verecek kimsesi yoksa beklemek anlamsız: tek başına
    // kalmış bir düğüm burada sonsuza kadar kilitlenmemeli.
    if (votes_.empty())
    {
        result_ = ConsensusResult::COMMITTED;
        return;
    }

    // on_enter çağrılmadan ÖNCE oy gelmiş olabilir (ağ, Task Engine'in
    // görevi başlatmasından hızlı davranabilir). Yukarıdaki sıfırlama böyle
    // bir durumda verilmiş kararı silerdi; bu yüzden eldeki oyları burada
    // yeniden değerlendiriyoruz.
    for (const auto& entry : votes_)
    {
        if (entry.second == Vote::NACK)
        {
            result_ = ConsensusResult::ABORTED;
            return;
        }
    }
    evaluate_result();
}

void ConsensusTask::run(TimePoint now)
{
    // Oylar ağdan asenkron olarak on_vote() ile geliyor. run()'ın tek işi
    // zaman aşımını kollamak.
    if (result_ != ConsensusResult::PENDING)
    {
        return;  // sonuç zaten belli
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_);

    if (elapsed < timeout_)
    {
        return;
    }

    // Süre doldu ve hâlâ tam ACK yok. Eksik cevap veren düğümler PENDING'de
    // kaldı. Heterojen bir sürüde 1 drone eksikken göreve başlamak riskli
    // olduğu için tüm görev iptal edilir (Bölüm 2).
    result_ = ConsensusResult::ABORTED;
    cancelled_by_timeout_ = true;
}

void ConsensusTask::on_exit()
{
}

bool ConsensusTask::is_finished() const
{
    return result_ != ConsensusResult::PENDING;
}

TaskType ConsensusTask::get_type() const
{
    return TaskType::CONSENSUS;
}

void ConsensusTask::on_vote(uint8_t sender_id, Vote vote)
{
    // Sonuç belli olduktan sonra gelen oylar kararı değiştirmez.
    if (result_ != ConsensusResult::PENDING)
    {
        return;
    }

    auto it = votes_.find(sender_id);
    if (it == votes_.end())
    {
        // Oyu beklenmeyen bir düğümden gelen mesaj yok sayılır.
        return;
    }

    it->second = vote;

    if (vote == Vote::NACK)
    {
        // Tek bir NACK yeterli: 2PC'de karar oybirliği gerektirir.
        // Diğerlerini beklemeye gerek yok.
        result_ = ConsensusResult::ABORTED;
        return;
    }

    evaluate_result();
}

void ConsensusTask::evaluate_result()
{
    for (const auto& entry : votes_)
    {
        if (entry.second != Vote::ACK)
        {
            return;  // en az bir düğüm henüz ACK vermemiş
        }
    }

    result_ = ConsensusResult::COMMITTED;
}

Vote ConsensusTask::vote_status(uint8_t drone_id) const
{
    const auto it = votes_.find(drone_id);
    if (it == votes_.end())
    {
        return Vote::PENDING;
    }
    return it->second;
}

}  // namespace swarm
