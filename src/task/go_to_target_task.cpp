#include "swarm/task/go_to_target_task.hpp"

#include "swarm/task/motion.hpp"

namespace swarm {

GoToTargetTask::GoToTargetTask(DroneState& durum, double hedef_x, double hedef_y)
    : durum_(durum)
    , hedef_x_(hedef_x)
    , hedef_y_(hedef_y)
{
}

void GoToTargetTask::on_enter(TimePoint now)
{
    son_calisma_ = now;
    tamamlandi_ = false;
}

void GoToTargetTask::run(TimePoint now)
{
    const double gecen_saniye =
            std::chrono::duration<double>(now - son_calisma_).count();
    son_calisma_ = now;

    tamamlandi_ = hedefe_dogru_ilerlet(
            durum_, hedef_x_, hedef_y_,
            YATAY_HIZ_M_S, VARIS_TOLERANSI_M, gecen_saniye);
}

void GoToTargetTask::on_exit()
{
}

bool GoToTargetTask::is_finished() const
{
    return tamamlandi_;
}

TaskType GoToTargetTask::get_type() const
{
    return TaskType::GO_TO_TARGET;
}

}  // namespace swarm
