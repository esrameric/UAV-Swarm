#include "swarm/task/scout_search_task.hpp"

#include "swarm/task/motion.hpp"

namespace swarm {

ScoutSearchTask::ScoutSearchTask(
        DroneState& durum,
        double merkez_x,
        double merkez_y,
        std::chrono::milliseconds tarama_suresi)
    : durum_(durum)
    , merkez_x_(merkez_x)
    , merkez_y_(merkez_y)
    , tarama_suresi_(tarama_suresi)
{
}

void ScoutSearchTask::on_enter(TimePoint now)
{
    son_calisma_ = now;
    bolgeye_varildi_ = false;
    tamamlandi_ = false;
}

void ScoutSearchTask::run(TimePoint now)
{
    const double gecen_saniye =
            std::chrono::duration<double>(now - son_calisma_).count();
    son_calisma_ = now;

    // --- 1. aşama: arama bölgesine git ---
    if (!bolgeye_varildi_)
    {
        bolgeye_varildi_ = hedefe_dogru_ilerlet(
                durum_, merkez_x_, merkez_y_,
                YATAY_HIZ_M_S, VARIS_TOLERANSI_M, gecen_saniye);

        if (bolgeye_varildi_)
        {
            // Tarama sayacı, bölgeye VARDIĞIMIZ anda başlar; görevin
            // başında değil. Uzak bir bölgeye uçuş süresi taramadan sayılmaz.
            tarama_baslangici_ = now;
        }
        return;
    }

    // --- 2. aşama: bölgede tara ---
    durum_.hizi_sifirla();

    const auto tarama_gecen = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - tarama_baslangici_);

    if (tarama_gecen >= tarama_suresi_)
    {
        tamamlandi_ = true;
    }
}

void ScoutSearchTask::on_exit()
{
}

bool ScoutSearchTask::is_finished() const
{
    return tamamlandi_;
}

TaskType ScoutSearchTask::get_type() const
{
    return TaskType::SCOUT_SEARCH;
}

}  // namespace swarm
