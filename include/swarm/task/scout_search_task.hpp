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
    static constexpr double HORIZONTAL_SPEED_M_S = 5.0;
    static constexpr double ARRIVAL_TOLERANCE_M = 0.5;
    static constexpr std::chrono::milliseconds DEFAULT_SCAN_DURATION{5000};

    ScoutSearchTask(
            DroneState& state,
            double center_x,
            double center_y,
            std::chrono::milliseconds scan_duration = DEFAULT_SCAN_DURATION);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

    // Arama bölgesine varıldı mı? (test ve log için)
    bool region_reached() const { return region_reached_; }

private:
    DroneState& state_;
    double center_x_;
    double center_y_;
    std::chrono::milliseconds scan_duration_;

    TimePoint last_update_{};
    TimePoint scan_start_{};
    bool region_reached_ = false;
    bool finished_ = false;
};

}  // namespace swarm
