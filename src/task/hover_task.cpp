#include "swarm/task/hover_task.hpp"

namespace swarm {

HoverTask::HoverTask(DroneState& state, std::chrono::milliseconds duration)
    : state_(state)
    , duration_(duration)
{
}

void HoverTask::on_enter(TimePoint now)
{
    start_ = now;
    finished_ = false;
    state_.reset_velocity();
}

void HoverTask::run(TimePoint now)
{
    state_.reset_velocity();

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_);

    if (elapsed >= duration_)
    {
        finished_ = true;
    }
}

void HoverTask::on_exit()
{
}

bool HoverTask::is_finished() const
{
    return finished_;
}

TaskType HoverTask::get_type() const
{
    return TaskType::HOVER;
}

}  // namespace swarm
