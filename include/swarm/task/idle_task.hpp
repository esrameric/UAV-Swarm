// IdleTask — queue boşaldığında düşülen varsayılan görev.
//
// HİÇBİR ZAMAN BİTMEZ: is_finished() daima false döner. Sürünün "boşta
// bekleme" hâlidir; yeni bir görev emri geldiğinde Task Engine bu task'ı
// queue'dan çıkarıp yerine yenisini koyar.
#pragma once

#include "swarm/drone_state.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

class IdleTask : public Task
{
public:
    // Referans üye tutabilmek için durum dışarıdan verilir. Task, DroneState'i
    // SAHİPLENMEZ — yalnızca üzerinde çalışır.
    explicit IdleTask(DroneState& state);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

private:
    // REFERANS ÜYE (`&`): işaret ettiği nesnenin takma adıdır, kopyası değil.
    // Burada yapılan değişiklik dışarıdaki asıl DroneState'e yansır.
    // Referans üyeler kurucu başlatma listesinde bağlanmak ZORUNDADIR ve
    // sonradan başka bir nesneye yeniden bağlanamaz.
    DroneState& state_;
};

}  // namespace swarm
