#include "swarm/task/idle_task.hpp"

namespace swarm {

IdleTask::IdleTask(DroneState& state)
    : state_(state)
{
}

void IdleTask::on_enter(TimePoint)
{
    state_.reset_velocity();
}

void IdleTask::run(TimePoint)
{
    // Boşta beklerken olduğumuz yerde duruyoruz.
    state_.reset_velocity();
}

void IdleTask::on_exit()
{
}

bool IdleTask::is_finished() const
{
    // Bilinçli olarak daima false: IdleTask kendiliğinden bitmez.
    return false;
}

TaskType IdleTask::get_type() const
{
    return TaskType::IDLE;
}

}  // namespace swarm
