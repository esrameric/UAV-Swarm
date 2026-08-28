#include "swarm/task/hover_task.hpp"

namespace swarm {

HoverTask::HoverTask(DroneState& durum, std::chrono::milliseconds sure)
    : durum_(durum)
    , sure_(sure)
{
}

void HoverTask::on_enter(TimePoint now)
{
    baslangic_ = now;
    tamamlandi_ = false;
    durum_.hizi_sifirla();
}

void HoverTask::run(TimePoint now)
{
    durum_.hizi_sifirla();

    const auto gecen = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - baslangic_);

    if (gecen >= sure_)
    {
        tamamlandi_ = true;
    }
}

void HoverTask::on_exit()
{
}

bool HoverTask::is_finished() const
{
    return tamamlandi_;
}

TaskType HoverTask::get_type() const
{
    return TaskType::HOVER;
}

}  // namespace swarm
