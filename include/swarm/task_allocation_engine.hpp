// ============================================================================
//  TaskAllocationEngine — görev dağıtım servisi
//
//  Bu bir Task DEĞİLDİR (Bölüm 3.3). Göreve karar veren bağımsız bir
//  servistir: gelen TaskAllocation emrinin `target_role` alanına bakıp
//  hangi child task'ın queue'ya gireceğine karar verir.
//
//    SCOUT   -> ScoutSearchTask   (alan taraması)
//    STRIKER -> GoToTargetTask    (hedefe müdahale)
//
//  Fonksiyonlar `static`: servisin saklayacağı bir durumu yok, dolayısıyla
//  bir nesne oluşturmaya gerek yok. Sınıfın kendisi yalnızca ilgili
//  fonksiyonları bir arada tutan bir isim alanı görevi görüyor.
// ============================================================================

#pragma once

#include <memory>

#include "TaskAllocation.hpp"
#include "swarm/drone_state.hpp"
#include "swarm/swarm_manager.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

class TaskAllocationEngine
{
public:
    // Bu emir bu düğümü ilgilendiriyor mu?
    // GCS'i hiçbir görev emri ilgilendirmez (GCS uçmaz, emri o verir).
    static bool concerns_this_node(
            const TaskAllocation& order,
            const SwarmConfig& config);

    // Emre karşılık gelen görevi üretir.
    // Emir bu düğümü ilgilendirmiyorsa nullptr döner.
    static std::unique_ptr<Task> create_task(
            const TaskAllocation& order,
            const SwarmConfig& config,
            DroneState& state);
};

}  // namespace swarm
