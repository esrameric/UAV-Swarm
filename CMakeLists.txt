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
#  Fast DDS ve IDL'den C++ uretimi
# ============================================================================
#
# ONEMLI: Bu adimlarin calismasi icin Fast DDS'in ortamda olmasi gerekir:
#     source tools/fastdds_env.sh
# (Kurulum icin: bash tools/install_fastdds.sh)

find_package(fastcdr 2 REQUIRED)   # serilestirme kutuphanesi
find_package(fastdds 3 REQUIRED)   # DDS'in kendisi

# fastddsgen: .idl -> C++ ceviren Java aracı. Derleme sirasinda cagrilacak.
find_program(FASTDDSGEN_EXECUTABLE NAMES fastddsgen REQUIRED)

# Uretilen kod depoya commit EDILMEZ (bkz. README varsayim V4); her derlemede
# build dizini altinda yeniden uretilir. Boylece .idl ile uretilen kodun
# birbirinden ayrisma (drift) ihtimali kalmaz.
set(SWARM_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)

# SwarmEnums once gelmeli: digerleri onu #include ediyor.
set(SWARM_IDL_NAMES SwarmEnums Heartbeat Telemetry TaskAllocation Consensus)

foreach(idl_name IN LISTS SWARM_IDL_NAMES)
    list(APPEND SWARM_IDL_FILES     ${CMAKE_CURRENT_SOURCE_DIR}/idl/${idl_name}.idl)
    list(APPEND SWARM_IDL_BASENAMES ${idl_name}.idl)
    # fastddsgen her .idl icin bu iki .cxx dosyasini uretir:
    list(APPEND SWARM_GENERATED_SOURCES
        ${SWARM_GENERATED_DIR}/${idl_name}PubSubTypes.cxx
        ${SWARM_GENERATED_DIR}/${idl_name}TypeObjectSupport.cxx)
    list(APPEND SWARM_GENERATED_HEADERS ${SWARM_GENERATED_DIR}/${idl_name}.hpp)
endforeach()

# add_custom_command(OUTPUT ...): "su dosyalari uretmek icin su komutu calistir"
# der. CMake, OUTPUT dosyalari DEPENDS dosyalarindan eskiyse komutu yeniden
# calistirir; yani bir .idl'i degistirince C++ otomatik yeniden uretilir.
#
# WORKING_DIRECTORY olarak idl/ secildi: fastddsgen'e dosyalari kisa isimleriyle
# verince ciktiyi -d dizinine duz (alt klasorsuz) yaziyor ve #include'lar
# ayni dizinde cozuluyor.
add_custom_command(
    OUTPUT  ${SWARM_GENERATED_SOURCES} ${SWARM_GENERATED_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SWARM_GENERATED_DIR}
    COMMAND ${FASTDDSGEN_EXECUTABLE} -replace -d ${SWARM_GENERATED_DIR} ${SWARM_IDL_BASENAMES}
    DEPENDS ${SWARM_IDL_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/idl
    COMMENT "IDL -> C++ uretiliyor (fastddsgen)"
    VERBATIM)

# Uretilen mesaj tiplerini tek bir kutuphane hedefinde topluyoruz.
# STATIC: kod, kullanan her hedefin icine kopyalanir (ayri bir .so dosyasi
# tasimak zorunda kalmayiz).
add_library(swarm_msgs STATIC ${SWARM_GENERATED_SOURCES})

# PUBLIC: bu kutuphaneye baglanan herkes de bu include dizinini ve
# kutuphaneleri otomatik gorur. (PRIVATE olsaydi sadece swarm_msgs gorurdu.)
target_include_directories(swarm_msgs PUBLIC ${SWARM_GENERATED_DIR})
target_link_libraries(swarm_msgs PUBLIC fastdds fastcdr)

# Uretilen kod bize ait degil; -Wall -Wextra uyarilarini onun icin kapatiyoruz
# ki kendi kodumuzun uyarilari kalabalikta kaybolmasin.
target_compile_options(swarm_msgs PRIVATE -w)

# ============================================================================
#  swarm_core — projenin kendi kodu
# ============================================================================
#
# Uygulama mantigi buraya toplanir; hem main.cpp hem de testler bu kutuphaneye
# baglanir. Boylece ayni kod iki yerde ayri ayri derlenmez.
add_library(swarm_core STATIC
    src/peer_manager.cpp
    src/swarm_manager.cpp
    src/task_allocation_engine.cpp
    src/gcs_controller.cpp
    src/fastdds_wrapper.cpp
    src/task/motion.cpp
    src/task/init_task.cpp
    src/task/discovery_task.cpp
    src/task/consensus_task.cpp
    src/task/idle_task.cpp
    src/task/scout_search_task.cpp
    src/task/go_to_target_task.cpp
    src/task/hover_task.cpp
    src/task/fail_safe_task.cpp
    src/task/landing_task.cpp)

# PUBLIC include: swarm_core'a baglanan herkes #include "swarm/..." yazabilsin.
target_include_directories(swarm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# std::thread ve std::mutex, Linux'ta pthread kutuphanesine baglanmayi
# gerektirir. Threads::Threads bunu tasinabilir sekilde halleder.
find_package(Threads REQUIRED)

# PUBLIC link: swarm_core'un baslik dosyalari IDL tiplerini (Heartbeat vb.)
# disariya tasidigi icin bagimlilik da disariya acilmali.
target_link_libraries(swarm_core PUBLIC swarm_msgs Threads::Threads)

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
    # swarm_core, hem projenin kendi kodunu hem de (PUBLIC bagimlilik olarak)
    # uretilen IDL tiplerini ve include dizinlerini beraberinde getirir.
    target_link_libraries(${test_name} PRIVATE
        GTest::gtest GTest::gtest_main
        swarm_core)

    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

# --- Kayıtlı birim testleri --------------------------------------------------
swarm_add_unit_test(test_build_environment)
swarm_add_unit_test(test_message_types)
swarm_add_unit_test(test_peer_info)
swarm_add_unit_test(test_peer_manager)
swarm_add_unit_test(test_task)
swarm_add_unit_test(test_tasks)
swarm_add_unit_test(test_swarm_manager)
swarm_add_unit_test(test_gcs_controller)
swarm_add_unit_test(test_fastdds_wrapper)
