// ============================================================================
//  Faz 1.4 — PeerInfo struct'ının testi
//
//  Test aynı zamanda PeerInfo'nun nasıl kullanıldığını gösteren örnektir.
// ============================================================================

#include <gtest/gtest.h>

#include "swarm/peer_info.hpp"

TEST(PeerInfo, VarsayilanDegerlerGuvenli)
{
    // Hiçbir alan elle doldurulmadan oluşturulan bir PeerInfo, "uninitialized"
    // bir değer içermemeli; her alanın anlamlı bir başlangıcı olmalı.
    const swarm::PeerInfo peer;

    EXPECT_EQ(peer.drone_id, 0u);
    EXPECT_EQ(peer.node_type, swarm::NodeType::DRONE);
    EXPECT_EQ(peer.role, swarm::DroneRole::SCOUT);
    EXPECT_EQ(peer.swarm_id, 0u);
    EXPECT_EQ(peer.current_task, swarm::TaskType::INIT);
    EXPECT_EQ(peer.last_seen_seq, 0u);
}

TEST(PeerInfo, VarsayilanOlarakHicbirSuruyeAitDegil)
{
    const swarm::PeerInfo peer;

    EXPECT_FALSE(peer.is_in_swarm());
}

TEST(PeerInfo, SwarmIdVerilinceSuruyeAitOlur)
{
    swarm::PeerInfo peer;
    peer.swarm_id = 1;

    EXPECT_TRUE(peer.is_in_swarm());
}

TEST(PeerInfo, SwarmIdSifirlanincaSuruyeAitOlmaktanCikar)
{
    swarm::PeerInfo peer;
    peer.swarm_id = 7;
    ASSERT_TRUE(peer.is_in_swarm());

    // 0 = "hiçbir sürüde değil" sentinel değeri.
    peer.swarm_id = 0;

    EXPECT_FALSE(peer.is_in_swarm());
}

TEST(PeerInfo, SifirDisiTumSwarmIdDegerleriGecerliSayilir)
{
    // Sentinel'in yalnızca 0 olduğunu, 1..255 arasındaki HER değerin geçerli
    // bir sürü kimliği sayıldığını doğruluyoruz. Bu test, ileride birinin
    // "-1 de geçersiz olsun" diye bir kontrol eklemesine karşı koruma:
    // uint8_t'ye -1 atamak sessizce 255 verir ve 255 geçerli bir sürü
    // kimliğidir.
    swarm::PeerInfo peer;

    for (int value = 1; value <= 255; ++value)
    {
        peer.swarm_id = static_cast<uint8_t>(value);
        EXPECT_TRUE(peer.is_in_swarm()) << "swarm_id = " << value;
    }
}

TEST(PeerInfo, GcsDugumuRolAlaniniKullanmaz)
{
    // GCS bir "drone rolü" değildir; node_type ile role birbirinden ayrı
    // modellendi (Bölüm 3.5). role alanı GCS için okunmaz/anlamsızdır.
    swarm::PeerInfo gcs;
    gcs.drone_id = 0;
    gcs.node_type = swarm::NodeType::GCS;
    gcs.swarm_id = 1;

    EXPECT_EQ(gcs.node_type, swarm::NodeType::GCS);
    EXPECT_TRUE(gcs.is_in_swarm());
}

TEST(PeerInfo, HeartbeatZamaniAliciSaatiyleGuncellenir)
{
    // last_heartbeat_local, gönderenin değil ALICININ saatiyle doldurulur.
    swarm::PeerInfo peer;
    const auto once = std::chrono::steady_clock::now();

    peer.last_heartbeat_local = std::chrono::steady_clock::now();

    const auto after = std::chrono::steady_clock::now();

    EXPECT_GE(peer.last_heartbeat_local, once);
    EXPECT_LE(peer.last_heartbeat_local, after);
}
