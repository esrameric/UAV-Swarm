#include "swarm/gcs_controller.hpp"

#include <memory>
#include <vector>

#include "swarm/task/consensus_task.hpp"

namespace swarm {

GcsController::GcsController(SwarmManager& yonetici)
    : yonetici_(yonetici)
{
}

uint32_t GcsController::gorev_teklif_et(
        DroneRole hedef_rol,
        double hedef_x,
        double hedef_y,
        TimePoint now)
{
    aktif_transaction_id_ = sonraki_transaction_id_;
    ++sonraki_transaction_id_;

    // Oylamaya yalnızca ŞU AN duyduğumuz drone'lar katılır. Hiç ayağa
    // kalkmamış bir drone'un oyunu beklemek, sürüyü 5 saniyelik zaman
    // aşımına mahkûm ederdi (Bölüm 2: non-blocking keşif).
    const std::vector<uint8_t> oy_verenler = yonetici_.online_drone_ids();

    // Emri şimdiden hazırlıyoruz ama YAYINLAMIYORUZ: önce oybirliği şart.
    bekleyen_emir_ = TaskAllocation{};
    bekleyen_emir_.task_id(aktif_transaction_id_);
    bekleyen_emir_.target_role(hedef_rol);
    bekleyen_emir_.target_x(hedef_x);
    bekleyen_emir_.target_y(hedef_y);

    // Oylamayı sürünün geri kalanıyla AYNI ConsensusTask sınıfı yürütür;
    // GCS'e özel ayrı bir consensus motoru yazılmadı (Bölüm 2).
    yonetici_.clear_task_queue();
    yonetici_.push_task(std::make_unique<ConsensusTask>(aktif_transaction_id_, oy_verenler));

    // Teklif mesajı: vote alanı PENDING'dir — bu, mesajın bir OY değil
    // TEKLİF olduğunu gösterir (Bölüm 3.6).
    Consensus teklif;
    teklif.transaction_id(aktif_transaction_id_);
    teklif.sender_id(yonetici_.config().drone_id);
    teklif.vote(Vote::PENDING);
    teklif.seq_num(aktif_transaction_id_);

    yonetici_.publish_consensus(teklif);

    durum_ = Durum::OYLAMA;

    (void)now;
    return aktif_transaction_id_;
}

void GcsController::adim(TimePoint)
{
    if (durum_ != Durum::OYLAMA)
    {
        return;
    }

    const SwarmManager::ConsensusSonucu sonuc = yonetici_.last_consensus_result();

    // Henüz bir oylama tamamlanmadı ya da tamamlanan bizim turumuz değil.
    if (!sonuc.gecerli || sonuc.transaction_id != aktif_transaction_id_)
    {
        return;
    }

    if (sonuc.sonuc == ConsensusResult::COMMITTED)
    {
        // Oybirliği sağlandı: görev emri artık yayınlanabilir.
        yonetici_.publish_task_allocation(bekleyen_emir_);
        durum_ = Durum::GOREV_YAYINLANDI;
        return;
    }

    if (sonuc.sonuc == ConsensusResult::ABORTED)
    {
        // Emir YAYINLANMAZ. Bölüm 2: tam ACK sağlanamazsa tüm görev iptal.
        durum_ = Durum::IPTAL;
    }
}

}  // namespace swarm
