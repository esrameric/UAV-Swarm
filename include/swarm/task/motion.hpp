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
double yatay_mesafe(double x1, double y1, double x2, double y2);

// Aracı hedefe doğru `hiz` ile `gecen_saniye` kadar ilerletir ve hız
// alanlarını günceller.
//
// Dönüş: hedefe (tolerans dahilinde) ulaşıldıysa true.
//
// Hedefe kalan mesafe bir adımda aşılacaksa araç hedefi geçmez, tam hedefe
// oturtulur — aksi halde hedef etrafında sonsuza kadar salınırdı.
bool hedefe_dogru_ilerlet(
        DroneState& durum,
        double hedef_x,
        double hedef_y,
        double hiz_metre_saniye,
        double tolerans_metre,
        double gecen_saniye);

}  // namespace swarm
