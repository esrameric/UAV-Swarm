// ConsensusTask — 2-Phase Commit (2PC) oylaması.
//
// Bu bir "ayrı consensus motoru" DEĞİL, Task hiyerarşisinin bir child'ıdır
// (Bölüm 2). Böylece görev akışı doğrusal kalır: queue'daki diğer görevler
// gibi sıraya girer, biter, sıradakine geçilir.
//
// Akış (Bölüm 3.6):
//   1) Teklif : GCS bir transaction_id ile "göreve başlayalım mı?" yayınlar.
//   2) Oy     : Her drone durumunu kontrol edip ACK / NACK döner; cevap
//               vermeyenler PENDING'de kalır.
//   3) Sonuç  : Beklenen HERKES ACK verirse COMMITTED.
//               Tek bir NACK bile gelirse ANINDA ABORTED.
//
//   4) Zaman aşımı: 5 saniye içinde tam ACK sağlanamazsa oylama ABORTED
//      olur. "Cevap yok" ile "açık NACK" farklı sebeplerdir ama sonuç
//      aynıdır: heterojen bir sürüde 1 drone eksikken göreve başlamak
//      risklidir, davranış net ve öngörülebilir olmalıdır.
//
// GÖREV İPTALİ: Sonuç ABORTED olduğunda yalnızca bu task bitmez — TÜM görev
// iptal edilir ve sürü IdleTask'a döner (Bölüm 2). Kuyruğu boşaltıp
// IdleTask'ı yerleştirmek Task Engine'in işi olduğu için ConsensusTask bunu
// `mission_should_abort()` ile bildirir; Engine (Faz 3.4) buna uyar.
#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <vector>

#include "SwarmEnums.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

// Oylamanın sonucu. Vote'tan farklı bir tip: Vote TEK BİR düğümün oyudur,
// ConsensusResult ise oylamanın TOPLU sonucudur.
enum class ConsensusResult
{
    PENDING,     // Oylama sürüyor
    COMMITTED,   // Herkes ACK verdi -> göreve başlanır
    ABORTED      // En az bir NACK -> görev iptal
};

class ConsensusTask : public Task
{
public:
    // Bölüm 3.6: 5 saniye içinde tam ACK sağlanamazsa görev iptal edilir.
    static constexpr std::chrono::milliseconds DEFAULT_TIMEOUT{5000};

    // beklenen_oy_verenler: oyu beklenen düğümlerin drone_id listesi.
    // Boş liste verilirse oylayacak kimse yok demektir; bu durumda oylama
    // anında COMMITTED sayılır (tek başına kalan düğüm kilitlenmemeli).
    ConsensusTask(
            uint32_t transaction_id,
            std::vector<uint8_t> expected_voters,
            std::chrono::milliseconds timeout = DEFAULT_TIMEOUT);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

    // Ağdan bir oy geldiğinde çağrılır (Faz 3'te komut thread'i tarafından).
    // Beklenmeyen bir göndericiden gelen oylar ve sonuç belli olduktan sonra
    // gelen oylar yok sayılır.
    void on_vote(uint8_t sender_id, Vote vote);

    ConsensusResult result() const { return result_; }
    uint32_t transaction_id() const { return transaction_id_; }

    // Belirli bir düğümün son oyu (test ve log için).
    Vote vote_status(uint8_t drone_id) const;

    // Task Engine'e verilen talimat: oylama ABORTED bittiyse tüm görev
    // queue'su boşaltılmalı ve IdleTask'a dönülmelidir (Bölüm 2/3.6).
    bool mission_should_abort() const { return result_ == ConsensusResult::ABORTED; }

    // Oylama zaman aşımına uğradığı için mi iptal oldu? (NACK değil)
    bool cancelled_by_timeout() const { return cancelled_by_timeout_; }

private:
    // Beklenen tüm oylar ACK ise sonucu COMMITTED yapar.
    void evaluate_result();

    uint32_t transaction_id_;

    // Her beklenen oy vereni PENDING ile başlatıp gelen oylarla güncelliyoruz.
    // Vote::PENDING == 0 olduğu için "henüz oy yok" hâli doğal başlangıçtır.
    std::map<uint8_t, Vote> votes_;

    std::chrono::milliseconds timeout_;
    TimePoint start_{};

    ConsensusResult result_ = ConsensusResult::PENDING;
    bool cancelled_by_timeout_ = false;
};

}  // namespace swarm
