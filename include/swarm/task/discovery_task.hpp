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
    static constexpr std::chrono::milliseconds DEFAULT_MAX_WAIT{5000};

    // PeerManager `const&` alınıyor: DiscoveryTask peer tablosunu yalnızca
    // OKUR, değiştirmez. Tabloyu güncelleyen, ağ dinleyen thread'dir.
    DiscoveryTask(
            const PeerManager& peer_manager,
            std::chrono::milliseconds max_wait = DEFAULT_MAX_WAIT);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

    // Görev, peer bulduğu için mi yoksa süre dolduğu için mi bitti?
    bool peer_found() const { return peer_found_; }

private:
    const PeerManager& peer_manager_;
    std::chrono::milliseconds max_wait_;
    TimePoint start_{};
    bool peer_found_ = false;
    bool finished_ = false;
};

}  // namespace swarm
