#include "swarm/task_allocation_engine.hpp"

#include "swarm/task/go_to_target_task.hpp"
#include "swarm/task/scout_search_task.hpp"

namespace swarm {

bool TaskAllocationEngine::bu_dugumu_ilgilendiriyor(
        const TaskAllocation& emir,
        const SwarmConfig& config)
{
    // GCS bir drone değildir; uçuş görevi almaz (Bölüm 3.1).
    if (config.node_type != NodeType::DRONE)
    {
        return false;
    }

    // Emir bir DRONE'a değil bir ROLE gönderilir. Rolü tutmayan drone'lar
    // emri sessizce yok sayar.
    return emir.target_role() == config.role;
}

std::unique_ptr<Task> TaskAllocationEngine::gorev_uret(
        const TaskAllocation& emir,
        const SwarmConfig& config,
        DroneState& durum)
{
    if (!bu_dugumu_ilgilendiriyor(emir, config))
    {
        return nullptr;
    }

    // Heterojen rol ayrımı burada gerçekleşiyor: aynı emir, rolüne göre
    // farklı drone'larda farklı göreve dönüşür.
    switch (emir.target_role())
    {
        case DroneRole::SCOUT:
            return std::make_unique<ScoutSearchTask>(
                    durum, emir.target_x(), emir.target_y());

        case DroneRole::STRIKER:
            return std::make_unique<GoToTargetTask>(
                    durum, emir.target_x(), emir.target_y());
    }

    return nullptr;
}

}  // namespace swarm
