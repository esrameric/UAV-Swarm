#include "swarm/task/fail_safe_task.hpp"

namespace swarm {

FailSafeTask::FailSafeTask(
        DroneState& durum,
        std::chrono::milliseconds degerlendirme_suresi)
    : durum_(durum)
    , degerlendirme_suresi_(degerlendirme_suresi)
{
}

void FailSafeTask::on_enter(TimePoint now)
{
    baslangic_ = now;
    tamamlandi_ = false;

    // Acil durumda ilk yapılacak şey: hareketi kes.
    durum_.hizi_sifirla();
}

void FailSafeTask::run(TimePoint now)
{
    durum_.hizi_sifirla();

    const auto gecen = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - baslangic_);

    if (gecen >= degerlendirme_suresi_)
    {
        tamamlandi_ = true;
    }
}

void FailSafeTask::on_exit()
{
}

bool FailSafeTask::is_finished() const
{
    return tamamlandi_;
}

TaskType FailSafeTask::get_type() const
{
    return TaskType::FAIL_SAFE;
}

}  // namespace swarm
