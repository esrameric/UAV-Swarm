// ScoutSearchTask — Gözcü (SCOUT) drone'unun görevi.
//
// İki aşamalı: önce arama bölgesinin merkezine uçar, oraya varınca belirli
// bir süre boyunca "tarama" yapar. Tarama süresi dolunca görev biter.
#pragma once

#include <chrono>

#include "swarm/drone_state.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

class ScoutSearchTask : public Task
{
public:
    static constexpr double YATAY_HIZ_M_S = 5.0;
    static constexpr double VARIS_TOLERANSI_M = 0.5;
    static constexpr std::chrono::milliseconds VARSAYILAN_TARAMA_SURESI{5000};

    ScoutSearchTask(
            DroneState& durum,
            double merkez_x,
            double merkez_y,
            std::chrono::milliseconds tarama_suresi = VARSAYILAN_TARAMA_SURESI);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

    // Arama bölgesine varıldı mı? (test ve log için)
    bool bolgeye_varildi() const { return bolgeye_varildi_; }

private:
    DroneState& durum_;
    double merkez_x_;
    double merkez_y_;
    std::chrono::milliseconds tarama_suresi_;

    TimePoint son_calisma_{};
    TimePoint tarama_baslangici_{};
    bool bolgeye_varildi_ = false;
    bool tamamlandi_ = false;
};

}  // namespace swarm
