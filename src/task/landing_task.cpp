#include "swarm/task/landing_task.hpp"

namespace swarm {

LandingTask::LandingTask(DroneState& durum)
    : durum_(durum)
{
}

void LandingTask::on_enter(TimePoint now)
{
    son_calisma_ = now;
    tamamlandi_ = false;
}

void LandingTask::run(TimePoint now)
{
    // duration<double>: farkı ondalıklı saniye olarak verir; mesafe
    // hesabında tam sayı bölmesi kaynaklı hata olmasın diye.
    const double gecen_saniye =
            std::chrono::duration<double>(now - son_calisma_).count();
    son_calisma_ = now;

    if (durum_.z <= 0.0)
    {
        durum_.z = 0.0;
        durum_.hizi_sifirla();
        tamamlandi_ = true;
        return;
    }

    durum_.z -= INIS_HIZI_M_S * gecen_saniye;
    durum_.vx = 0.0;
    durum_.vy = 0.0;
    durum_.vz = -INIS_HIZI_M_S;

    if (durum_.z <= 0.0)
    {
        durum_.z = 0.0;
        durum_.hizi_sifirla();
        tamamlandi_ = true;
    }
}

void LandingTask::on_exit()
{
}

bool LandingTask::is_finished() const
{
    return tamamlandi_;
}

TaskType LandingTask::get_type() const
{
    return TaskType::LANDING;
}

}  // namespace swarm
