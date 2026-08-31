// ============================================================================
//  Hareket yardımcıları
//
//  Birden fazla task'ın (GoToTarget, ScoutSearch, Landing) ihtiyaç duyduğu
//  basit hareket hesapları burada toplanır — aynı kodun üç yerde tekrar
//  etmemesi için.
//
//  Gerçek bir uçuş kontrolcüsü yok; hareket, sabit hızla düz çizgi
//  üzerinde ilerleme olarak modelleniyor. SITL için yeterli ve okunabilir.
// ============================================================================

#pragma once

#include "swarm/drone_state.hpp"

namespace swarm {

// İki nokta arasındaki yatay (x-y düzlemindeki) mesafe, metre.
double horizontal_distance(double x1, double y1, double x2, double y2);

// Aracı hedefe doğru `speed_meters_per_second` ile `elapsed_seconds` kadar ilerletir ve hız
// alanlarını günceller.
//
// Dönüş: hedefe (tolerans dahilinde) ulaşıldıysa true.
//
// Hedefe kalan mesafe bir adımda aşılacaksa araç hedefi geçmez, tam hedefe
// oturtulur — aksi halde hedef etrafında sonsuza kadar salınırdı.
bool move_toward_target(
        DroneState& state,
        double target_x,
        double target_y,
        double speed_meters_per_second,
        double tolerance_meters,
        double elapsed_seconds);

}  // namespace swarm
