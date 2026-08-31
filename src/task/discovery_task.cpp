#include "swarm/task/discovery_task.hpp"

namespace swarm {

DiscoveryTask::DiscoveryTask(
        const PeerManager& peer_manager,
        std::chrono::milliseconds max_wait)
    : peer_manager_(peer_manager)
    , max_wait_(max_wait)
{
}

void DiscoveryTask::on_enter(TimePoint now)
{
    start_ = now;
    peer_found_ = false;
    finished_ = false;
}

void DiscoveryTask::run(TimePoint now)
{
    if (peer_manager_.online_peer_count() > 0)
    {
        peer_found_ = true;
        finished_ = true;
        return;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_);

    if (elapsed >= max_wait_)
    {
        // Kimse duyulmadı ama beklemeye devam etmiyoruz: var olanlarla
        // (yani yalnız başımıza) devam ederiz.
        finished_ = true;
    }
}

void DiscoveryTask::on_exit()
{
}

bool DiscoveryTask::is_finished() const
{
    return finished_;
}

TaskType DiscoveryTask::get_type() const
{
    return TaskType::DISCOVERY;
}

}  // namespace swarm
