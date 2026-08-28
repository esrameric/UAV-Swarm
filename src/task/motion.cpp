#include "swarm/task/motion.hpp"

#include <cmath>

namespace swarm {

double yatay_mesafe(double x1, double y1, double x2, double y2)
{
    const double fark_x = x2 - x1;
    const double fark_y = y2 - y1;

    // std::hypot(a, b) = sqrt(a*a + b*b), ama ara sonuçlarda taşma/alttaşma
    // yaşamadan hesaplar. Elle sqrt yazmaya tercih edilir.
    return std::hypot(fark_x, fark_y);
}

bool hedefe_dogru_ilerlet(
        DroneState& durum,
        double hedef_x,
        double hedef_y,
        double hiz_metre_saniye,
        double tolerans_metre,
        double gecen_saniye)
{
    const double kalan = yatay_mesafe(durum.x, durum.y, hedef_x, hedef_y);

    if (kalan <= tolerans_metre)
    {
        durum.hizi_sifirla();
        return true;
    }

    const double adim = hiz_metre_saniye * gecen_saniye;

    if (adim >= kalan)
    {
        // Bu adımda hedefe varıyoruz. Hedefi GEÇMEYİP tam üstüne oturuyoruz;
        // aksi halde araç hedefin etrafında ileri geri salınırdı.
        durum.x = hedef_x;
        durum.y = hedef_y;
        durum.hizi_sifirla();
        return true;
    }

    // Hedefe doğru birim vektör: yön bilgisini uzunluğundan ayırıyoruz.
    const double yon_x = (hedef_x - durum.x) / kalan;
    const double yon_y = (hedef_y - durum.y) / kalan;

    durum.x += yon_x * adim;
    durum.y += yon_y * adim;

    durum.vx = yon_x * hiz_metre_saniye;
    durum.vy = yon_y * hiz_metre_saniye;
    durum.vz = 0.0;

    return false;
}

}  // namespace swarm
