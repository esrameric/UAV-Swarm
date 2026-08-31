#include "swarm/task/go_to_target_task.hpp"

#include "swarm/task/motion.hpp"

namespace swarm {

GoToTargetTask::GoToTargetTask(DroneState& state, double target_x, double target_y)
    : state_(state)
    , target_x_(target_x)
    , target_y_(target_y)
{
}

void GoToTargetTask::on_enter(TimePoint now)
{
    last_update_ = now;
    finished_ = false;
}

void GoToTargetTask::run(TimePoint now)
{
    const double elapsed_seconds =
            std::chrono::duration<double>(now - last_update_).count();
    last_update_ = now;

    finished_ = move_toward_target(
            state_, target_x_, target_y_,
            HORIZONTAL_SPEED_M_S, ARRIVAL_TOLERANCE_M, elapsed_seconds);
}

void GoToTargetTask::on_exit()
{
}

bool GoToTargetTask::is_finished() const
{
    return finished_;
}

TaskType GoToTargetTask::get_type() const
{
    return TaskType::GO_TO_TARGET;
}

}  // namespace swarm
