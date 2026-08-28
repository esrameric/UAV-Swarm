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
    enum class Durum
    {
        BOSTA,               // Aktif görev yok
        OYLAMA,              // Teklif yayınlandı, oylar bekleniyor
        GOREV_YAYINLANDI,    // Oybirliği sağlandı, task_alloc yayınlandı
        IPTAL                // Oylama başarısız; görev iptal edildi
    };

    explicit GcsController(SwarmManager& yonetici);

    // Yeni bir görev teklif eder:
    //   - ONLINE drone'ları oy verecek düğüm listesi olarak alır
    //   - bir ConsensusTask'ı SwarmManager'ın görev kuyruğuna koyar
    //   - /swarm/consensus üzerinden teklifi yayınlar
    //
    // Dönüş: bu teklifin transaction_id'si.
    uint32_t gorev_teklif_et(
            DroneRole hedef_rol,
            double hedef_x,
            double hedef_y,
            TimePoint now);

    // Her turda çağrılır. Oylamanın sonucunu kontrol eder; COMMITTED ise
    // görev emrini yayınlar, ABORTED ise görevi iptal eder.
    void adim(TimePoint now);

    Durum durum() const { return durum_; }
    uint32_t aktif_transaction_id() const { return aktif_transaction_id_; }

private:
    SwarmManager& yonetici_;

    Durum durum_ = Durum::BOSTA;

    // Oylama turlarını birbirinden ayıran sayaç. 1'den başlar; 0 "geçersiz"
    // anlamına gelsin diye kullanılmıyor.
    uint32_t sonraki_transaction_id_ = 1;
    uint32_t aktif_transaction_id_ = 0;

    // Oylama geçerse yayınlanacak emrin içeriği.
    TaskAllocation bekleyen_emir_{};
};

}  // namespace swarm
