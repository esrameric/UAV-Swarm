// InitTask — düğüm ayağa kalkarken bir kez çalışan başlangıç görevi.
// Kuyruğun ilk elemanıdır; işini bitirir bitirmez DiscoveryTask'a geçilir.
#pragma once

#include "swarm/task/task.hpp"

namespace swarm {

class InitTask : public Task
{
public:
    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

private:
    // is_finished() `const` olduğu ve `now` almadığı için, "bittim mi?"
    // kararı run() içinde verilip burada saklanır. Bu kalıp tüm task'larda
    // aynıdır: run() durumu ilerletir, is_finished() sadece okur.
    bool finished_ = false;
};

}  // namespace swarm
