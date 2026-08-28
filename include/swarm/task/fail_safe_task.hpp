// FailSafeTask — arıza/acil durum davranışı.
//
// Tetiklendiğinde (örn. bir peer'ın kaybolması, batarya kritiği) aracı
// ANINDA durdurur ve kısa bir "değerlendirme" süresi bekler. Süre sonunda
// biter; Task Engine bunun ardından LandingTask'ı işletir.
//
// Not: Aracı hemen indirmek yerine önce durdurup kısa süre beklemek bilinçli
// bir seçim — geçici bir ağ kesintisi kalıcı bir arıza gibi davranıp
// sürüyü gereksiz yere yere indirmesin diye.
#pragma once

#include <chrono>

#include "swarm/drone_state.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

class FailSafeTask : public Task
{
public:
    static constexpr std::chrono::milliseconds VARSAYILAN_DEGERLENDIRME_SURESI{1000};

    explicit FailSafeTask(
            DroneState& durum,
            std::chrono::milliseconds degerlendirme_suresi = VARSAYILAN_DEGERLENDIRME_SURESI);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

private:
    DroneState& durum_;
    std::chrono::milliseconds degerlendirme_suresi_;
    TimePoint baslangic_{};
    bool tamamlandi_ = false;
};

}  // namespace swarm
