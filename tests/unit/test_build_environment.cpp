// ============================================================================
//  Faz 0.6 — Derleme ortamı doğrulama testi ("dummy" test)
//
//  Bu test bir iş mantığı doğrulamıyor; sonraki fazlardaki gerçek testlerin
//  üzerine kurulacağı iskeletin ayakta olduğunu kanıtlıyor:
//    1) GoogleTest bulunuyor ve bağlanıyor mu?
//    2) Derleyici gerçekten C++17 modunda mı?
//    3) `ctest` bu testi görüp çalıştırabiliyor mu?
// ============================================================================

// #include: başka bir dosyanın içeriğini buraya dahil eder. Açılı parantez
// <> sistem/kütüphane başlıklarında, çift tırnak "" kendi dosyalarımızda
// kullanılır. gtest.h, TEST/EXPECT_* makrolarını getirir.
#include <gtest/gtest.h>

#include <string>

// TEST(TestSuiteAdi, TestAdi) { ... } — GoogleTest'in temel yapı taşı.
// Her TEST bloğu bağımsız çalışan tek bir test senaryosudur. İçindeki
// EXPECT_* / ASSERT_* satırları sağlanmazsa test başarısız olur.
//   EXPECT_*: başarısız olsa bile testin geri kalanı çalışmaya devam eder.
//   ASSERT_*: başarısız olursa test fonksiyonu anında sonlanır.
TEST(BuildEnvironment, GoogleTestCalisiyor)
{
    EXPECT_EQ(2 + 2, 4);
    EXPECT_TRUE(true);
}

TEST(BuildEnvironment, DerleyiciCpp17Modunda)
{
    // __cplusplus, derleyicinin hangi C++ standardında çalıştığını söyleyen
    // standart bir makrodur. C++17 için değeri 201703L'dir. CMake'te
    // CMAKE_CXX_STANDARD 17 ayarının gerçekten etkili olduğunu burada
    // çalışma zamanında değil, derleme zamanında da doğrulayabiliriz.
    static_assert(__cplusplus >= 201703L, "C++17 veya ustu gerekli");

    EXPECT_GE(__cplusplus, 201703L);
}

TEST(BuildEnvironment, StandartKutuphaneKullanilabilir)
{
    // std::string, C++ standart kütüphanesinin metin tipidir. C'deki
    // char dizilerinin aksine kendi belleğini kendi yönetir (RAII):
    // kapsam (scope) bittiğinde belleği otomatik serbest bırakır.
    const std::string node_adi = "swarm_uav";

    EXPECT_EQ(node_adi.size(), 9u);
    EXPECT_EQ(node_adi, "swarm_uav");
}
