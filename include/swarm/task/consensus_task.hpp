// ConsensusTask — 2-Phase Commit (2PC) oylaması.
//
// Bu bir "ayrı consensus motoru" DEĞİL, Task hiyerarşisinin bir child'ıdır
// (Bölüm 2). Böylece görev akışı doğrusal kalır: kuyruktaki diğer görevler
// gibi sıraya girer, biter, sıradakine geçilir.
//
// Akış (Bölüm 3.6):
//   1) Teklif : GCS bir transaction_id ile "göreve başlayalım mı?" yayınlar.
//   2) Oy     : Her drone durumunu kontrol edip ACK / NACK döner; cevap
//               vermeyenler PENDING'de kalır.
//   3) Sonuç  : Beklenen HERKES ACK verirse COMMITTED.
//               Tek bir NACK bile gelirse ANINDA ABORTED.
//
// Zaman aşımı davranışı Faz 2.3'te eklenir.
#pragma once

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
    // beklenen_oy_verenler: oyu beklenen düğümlerin drone_id listesi.
    // Boş liste verilirse oylayacak kimse yok demektir; bu durumda oylama
    // anında COMMITTED sayılır (tek başına kalan düğüm kilitlenmemeli).
    ConsensusTask(
            uint32_t transaction_id,
            std::vector<uint8_t> beklenen_oy_verenler);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

    // Ağdan bir oy geldiğinde çağrılır (Faz 3'te komut thread'i tarafından).
    // Beklenmeyen bir göndericiden gelen oylar ve sonuç belli olduktan sonra
    // gelen oylar yok sayılır.
    void on_vote(uint8_t gonderen_id, Vote oy);

    ConsensusResult result() const { return sonuc_; }
    uint32_t transaction_id() const { return transaction_id_; }

    // Belirli bir düğümün son oyu (test ve log için).
    Vote oy_durumu(uint8_t drone_id) const;

private:
    // Beklenen tüm oylar ACK ise sonucu COMMITTED yapar.
    void sonucu_degerlendir();

    uint32_t transaction_id_;

    // Her beklenen oy vereni PENDING ile başlatıp gelen oylarla güncelliyoruz.
    // Vote::PENDING == 0 olduğu için "henüz oy yok" hâli doğal başlangıçtır.
    std::map<uint8_t, Vote> oylar_;

    ConsensusResult sonuc_ = ConsensusResult::PENDING;
};

}  // namespace swarm
