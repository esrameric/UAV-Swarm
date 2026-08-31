#include "swarm/gcs_controller.hpp"

#include <memory>
#include <vector>

#include "swarm/task/consensus_task.hpp"

namespace swarm {

GcsController::GcsController(SwarmManager& manager)
    : manager_(manager)
{
}

uint32_t GcsController::propose_task(
        DroneRole target_role,
        double target_x,
        double target_y,
        TimePoint now)
{
    active_transaction_id_ = next_transaction_id_;
    ++next_transaction_id_;

    // Oylamaya yalnızca ŞU AN duyduğumuz drone'lar katılır. Hiç ayağa
    // kalkmamış bir drone'un oyunu beklemek, sürüyü 5 saniyelik zaman
    // aşımına mahkûm ederdi (Bölüm 2: non-blocking keşif).
    const std::vector<uint8_t> voters = manager_.online_drone_ids();

    // Emri şimdiden hazırlıyoruz ama YAYINLAMIYORUZ: önce oybirliği şart.
    pending_order_ = TaskAllocation{};
    pending_order_.task_id(active_transaction_id_);
    pending_order_.target_role(target_role);
    pending_order_.target_x(target_x);
    pending_order_.target_y(target_y);

    // Oylamayı sürünün geri kalanıyla AYNI ConsensusTask sınıfı yürütür;
    // GCS'e özel ayrı bir consensus motoru yazılmadı (Bölüm 2).
    //
    // Queue'ya DOĞRUDAN dokunmuyoruz: GcsController ana thread'den çalışıyor,
    // görev queue'su ise Task Engine thread'ine ait. İstek bırakıyoruz,
    // queue'yu o düzenliyor.
    manager_.request_consensus(active_transaction_id_, voters);

    // Teklif mesajı: vote alanı PENDING'dir — bu, mesajın bir OY değil
    // TEKLİF olduğunu gösterir (Bölüm 3.6).
    Consensus proposal;
    proposal.transaction_id(active_transaction_id_);
    proposal.sender_id(manager_.config().drone_id);
    proposal.vote(Vote::PENDING);
    proposal.seq_num(active_transaction_id_);

    manager_.publish_consensus(proposal);

    state_ = State::VOTING;

    (void)now;
    return active_transaction_id_;
}

void GcsController::consume_result()
{
    // Terminal durumu bir kez okuduktan sonra BOSTA'ya dönüyoruz; aksi halde
    // ana döngü aynı sonucu her turda tekrar tekrar raporlardı.
    if (state_ == State::TASK_PUBLISHED || state_ == State::CANCELLED)
    {
        state_ = State::IDLE;
    }
}

void GcsController::step(TimePoint)
{
    if (state_ != State::VOTING)
    {
        return;
    }

    const SwarmManager::ConsensusOutcome result = manager_.last_consensus_result();

    // Henüz bir oylama tamamlanmadı ya da tamamlanan bizim turumuz değil.
    if (!result.valid || result.transaction_id != active_transaction_id_)
    {
        return;
    }

    if (result.result == ConsensusResult::COMMITTED)
    {
        // Oybirliği sağlandı: görev emri artık yayınlanabilir.
        manager_.publish_task_allocation(pending_order_);
        state_ = State::TASK_PUBLISHED;
        return;
    }

    if (result.result == ConsensusResult::ABORTED)
    {
        // Emir YAYINLANMAZ. Bölüm 2: tam ACK sağlanamazsa tüm görev iptal.
        state_ = State::CANCELLED;
    }
}

}  // namespace swarm
