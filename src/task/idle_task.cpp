#include "swarm/task/idle_task.hpp"

namespace swarm {

IdleTask::IdleTask(DroneState& durum)
    : durum_(durum)
{
}

void IdleTask::on_enter(TimePoint)
{
    durum_.hizi_sifirla();
}

void IdleTask::run(TimePoint)
{
    // Boşta beklerken olduğumuz yerde duruyoruz.
    durum_.hizi_sifirla();
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
