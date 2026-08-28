# ============================================================================
#  Sürü İHA Sistemi — kök CMake dosyası
#
#  CMake, C++ projelerinde "hangi kaynak dosyalar hangi hedefe (executable /
#  kütüphane) derlenecek, hangi kütüphanelere bağlanacak" sorusunu tarif
#  ettiğimiz build sistemidir. Bu dosya projenin giriş noktasıdır.
#
#  Derlemek için:
#      cmake -B build -S .
#      cmake --build build -j3
#      ctest --test-dir build --output-on-failure
# ============================================================================

cmake_minimum_required(VERSION 3.16)

project(swarm_uav
    VERSION 0.1.0
    DESCRIPTION "3 drone'luk heterojen surü IHA sistemi (native Fast DDS)"
    LANGUAGES CXX)

# --- Dil standardı -----------------------------------------------------------
# C++17 projenin asgari standardı (Bölüm 5). STANDARD_REQUIRED=ON, derleyici
# C++17'yi desteklemiyorsa sessizce eskiye düşmek yerine hata versin diye.
# EXTENSIONS=OFF ise GNU'ya özgü eklentileri (gnu++17) kapatır — taşınabilir
# standart C++ yazdığımızdan emin oluruz.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Kullanıcı bir build tipi seçmediyse Release varsayalım (optimizasyon açık).
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Derleme tipi" FORCE)
endif()

# Uyarıları açıyoruz: yeni C++ öğrenirken derleyici uyarıları en iyi öğretmen.
add_compile_options(-Wall -Wextra -Wpedantic)

# ============================================================================
#  Testler (GoogleTest + CTest)
# ============================================================================
#
# `enable_testing()` CTest'i açar; `add_test()` ile kaydedilen her test
# `ctest` komutuyla topluca çalıştırılabilir hale gelir.
enable_testing()

# GoogleTest'i sistemden bul (Ubuntu'da `libgtest-dev` paketi sağlar).
# REQUIRED: bulunamazsa cmake burada hata verip dursun.
find_package(GTest REQUIRED)

# --- Yardımcı fonksiyon ------------------------------------------------------
# Her birim testi için aynı 3 satırı (add_executable + target_link_libraries +
# add_test) tekrar tekrar yazmak yerine tek bir fonksiyonda topluyoruz.
# Kullanımı:  swarm_add_unit_test(test_peer_info)
#   -> tests/unit/test_peer_info.cpp dosyasını derler, gtest'e bağlar,
#      ctest'e "test_peer_info" adıyla kaydeder.
function(swarm_add_unit_test test_name)
    add_executable(${test_name} tests/unit/${test_name}.cpp)

    # GTest::gtest_main, `int main()` fonksiyonunu bizim yerimize sağlar;
    # bu sayede test dosyalarında main yazmamıza gerek kalmaz.
    target_link_libraries(${test_name} PRIVATE GTest::gtest GTest::gtest_main)

    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

# --- Kayıtlı birim testleri --------------------------------------------------
swarm_add_unit_test(test_build_environment)
