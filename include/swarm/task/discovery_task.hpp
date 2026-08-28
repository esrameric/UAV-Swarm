// DiscoveryTask — sürüdeki diğer düğümleri keşfetme görevi.
//
// KEŞİF STRATEJİSİ NON-BLOCKING (Bölüm 2): 3 drone'un hepsi bulunana kadar
// beklenmez. En az bir peer duyulduğunda görev tamamlanır; hiç kimse
// duyulmazsa da azami bekleme süresi sonunda yine tamamlanır ve sürü var
// olanlarla çalışmaya devam eder. Gerçek sahada bir drone hiç ayağa
// kalkmayabilir; sistemin buna takılıp kalmaması gerekir.
#pragma once

#include <chrono>

#include "swarm/peer_manager.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

class DiscoveryTask : public Task
{
public:
    static constexpr std::chrono::milliseconds VARSAYILAN_AZAMI_BEKLEME{5000};

    // PeerManager `const&` alınıyor: DiscoveryTask peer tablosunu yalnızca
    // OKUR, değiştirmez. Tabloyu güncelleyen, ağ dinleyen thread'dir.
    DiscoveryTask(
            const PeerManager& peer_yoneticisi,
            std::chrono::milliseconds azami_bekleme = VARSAYILAN_AZAMI_BEKLEME);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

    // Görev, peer bulduğu için mi yoksa süre dolduğu için mi bitti?
    bool peer_bulundu() const { return peer_bulundu_; }

private:
    const PeerManager& peer_yoneticisi_;
    std::chrono::milliseconds azami_bekleme_;
    TimePoint baslangic_{};
    bool peer_bulundu_ = false;
    bool tamamlandi_ = false;
};

}  // namespace swarm
