#include "swarm/task/landing_task.hpp"

namespace swarm {

LandingTask::LandingTask(DroneState& state)
    : state_(state)
{
}

void LandingTask::on_enter(TimePoint now)
{
    last_update_ = now;
    finished_ = false;
}

void LandingTask::run(TimePoint now)
{
    // duration<double>: farkı ondalıklı saniye olarak verir; mesafe
    // hesabında tam sayı bölmesi kaynaklı hata olmasın diye.
    const double elapsed_seconds =
            std::chrono::duration<double>(now - last_update_).count();
    last_update_ = now;

    if (state_.z <= 0.0)
    {
        state_.z = 0.0;
        state_.reset_velocity();
        finished_ = true;
        return;
    }

    state_.z -= DESCENT_SPEED_M_S * elapsed_seconds;
    state_.vx = 0.0;
    state_.vy = 0.0;
    state_.vz = -DESCENT_SPEED_M_S;

    if (state_.z <= 0.0)
    {
        state_.z = 0.0;
        state_.reset_velocity();
        finished_ = true;
    }
}

void LandingTask::on_exit()
{
}

bool LandingTask::is_finished() const
{
    return finished_;
}

TaskType LandingTask::get_type() const
{
    return TaskType::LANDING;
}

}  // namespace swarm
