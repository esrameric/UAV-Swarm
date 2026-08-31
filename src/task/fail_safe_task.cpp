#include "swarm/task/fail_safe_task.hpp"

namespace swarm {

FailSafeTask::FailSafeTask(
        DroneState& state,
        std::chrono::milliseconds assessment_duration)
    : state_(state)
    , assessment_duration_(assessment_duration)
{
}

void FailSafeTask::on_enter(TimePoint now)
{
    start_ = now;
    finished_ = false;

    // Acil durumda ilk yapılacak şey: hareketi kes.
    state_.reset_velocity();
}

void FailSafeTask::run(TimePoint now)
{
    state_.reset_velocity();

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_);

    if (elapsed >= assessment_duration_)
    {
        finished_ = true;
    }
}

void FailSafeTask::on_exit()
{
}

bool FailSafeTask::is_finished() const
{
    return finished_;
}

TaskType FailSafeTask::get_type() const
{
    return TaskType::FAIL_SAFE;
}

}  // namespace swarm
