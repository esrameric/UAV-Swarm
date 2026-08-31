#include "swarm/task_allocation_engine.hpp"

#include "swarm/task/go_to_target_task.hpp"
#include "swarm/task/scout_search_task.hpp"

namespace swarm {

bool TaskAllocationEngine::concerns_this_node(
        const TaskAllocation& order,
        const SwarmConfig& config)
{
    // GCS bir drone değildir; uçuş görevi almaz (Bölüm 3.1).
    if (config.node_type != NodeType::DRONE)
    {
        return false;
    }

    // Emir bir DRONE'a değil bir ROLE gönderilir. Rolü tutmayan drone'lar
    // emri sessizce yok sayar.
    return order.target_role() == config.role;
}

std::unique_ptr<Task> TaskAllocationEngine::create_task(
        const TaskAllocation& order,
        const SwarmConfig& config,
        DroneState& state)
{
    if (!concerns_this_node(order, config))
    {
        return nullptr;
    }

    // Heterojen rol ayrımı burada gerçekleşiyor: aynı emir, rolüne göre
    // farklı drone'larda farklı göreve dönüşür.
    switch (order.target_role())
    {
        case DroneRole::SCOUT:
            return std::make_unique<ScoutSearchTask>(
                    state, order.target_x(), order.target_y());

        case DroneRole::STRIKER:
            return std::make_unique<GoToTargetTask>(
                    state, order.target_x(), order.target_y());
    }

    return nullptr;
}

}  // namespace swarm
