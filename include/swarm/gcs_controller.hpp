// ============================================================================
//  GcsController — Taktik YKİ'nin (GCS) görev akışı
//
//  KAPSAM KARARI (Bölüm 9'daki açık nokta): GCS tam C++ node'dur ve
//  drone'larla aynı FastDDS altyapısını kullanır, ama drone'larınki kadar
//  derin bir Task hiyerarşisine İHTİYACI YOKTUR. GCS uçmaz; ScoutSearch,
//  GoToTarget, Landing gibi görevlerin GCS'te karşılığı yok.
//
//  Bu yüzden GCS'in görev motoru asgari tutuldu — üç şey yapar:
//    1) Görev teklif eder (consensus propose yayınlar)
//    2) Oylamayı izler (ConsensusTask'ı sürünün geri kalanıyla AYNI sınıfı
//       kullanarak yürütür; ayrı bir consensus motoru yazılmadı)
//    3) Oylama COMMITTED biterse görev emrini (task_alloc) yayınlar
//
//  Telemetri izleme, SwarmManager'ın peer table'ı üzerinden zaten yapılıyor.
// ============================================================================

#pragma once

#include <cstdint>

#include "swarm/swarm_manager.hpp"

namespace swarm {

class GcsController
{
public:
    // Görev akışının hangi aşamasında olduğumuz.
    enum class State
    {
        IDLE,               // Aktif görev yok
        VOTING,              // Teklif yayınlandı, oylar bekleniyor
        TASK_PUBLISHED,    // Oybirliği sağlandı, task_alloc yayınlandı
        CANCELLED                // Oylama başarısız; görev iptal edildi
    };

    explicit GcsController(SwarmManager& manager);

    // Yeni bir görev teklif eder:
    //   - ONLINE drone'ları oy verecek düğüm listesi olarak alır
    //   - bir ConsensusTask'ı SwarmManager'ın görev queue'suna koyar
    //   - /swarm/consensus üzerinden teklifi yayınlar
    //
    // Dönüş: bu teklifin transaction_id'si.
    uint32_t propose_task(
            DroneRole target_role,
            double target_x,
            double target_y,
            TimePoint now);

    // Her turda çağrılır. Oylamanın sonucunu kontrol eder; COMMITTED ise
    // görev emrini yayınlar, ABORTED ise görevi iptal eder.
    void step(TimePoint now);

    // Terminal durumu (GOREV_YAYINLANDI / IPTAL) okunduktan sonra çağrılır;
    // durumu BOSTA'ya çeker. Olmasaydı ana döngü aynı sonucu her turda
    // tekrar tekrar raporlardı.
    void consume_result();

    State state() const { return state_; }
    uint32_t active_transaction_id() const { return active_transaction_id_; }

private:
    SwarmManager& manager_;

    State state_ = State::IDLE;

    // Oylama turlarını birbirinden ayıran sayaç. 1'den başlar; 0 "geçersiz"
    // anlamına gelsin diye kullanılmıyor.
    uint32_t next_transaction_id_ = 1;
    uint32_t active_transaction_id_ = 0;

    // Oylama geçerse yayınlanacak emrin içeriği.
    TaskAllocation pending_order_{};
};

}  // namespace swarm
