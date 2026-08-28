#!/usr/bin/env bash
#
# Fast DDS ortamını mevcut kabuğa yükler. `source` ile çağrılmalıdır:
#
#     source tools/fastdds_env.sh
#
# Yaptığı üç şey:
#   1) ROS 2 girdilerini PATH'ten temizler (bu proje ROS 2 kullanmıyor;
#      /opt/ros/*/bin PATH'te kalırsa CMake ROS paketlerini bulup derlemeyi bozar)
#   2) Fast DDS'in colcon install prefix'ini ortama alır (setup.bash)
#   3) fastddsgen'i (gradle ile derlenen IDL derleyicisi) PATH'e ekler

_FASTDDS_WS="$HOME/Fast-DDS"

# 1) ROS 2 temizliği
PATH="$(echo "$PATH" | tr ':' '\n' | grep -v '/opt/ros/' | paste -sd: -)"
export PATH
unset AMENT_PREFIX_PATH COLCON_PREFIX_PATH ROS_DISTRO ROS_VERSION 2>/dev/null || true

# 2) Fast DDS kütüphaneleri
if [ -f "$_FASTDDS_WS/install/setup.bash" ]; then
    # colcon'un ürettiği setup.bash bazı değişkenleri (COLCON_TRACE vb.)
    # tanımsız bırakarak okur. Çağıran script `set -u` (tanımsız değişken =
    # hata) ile çalışıyorsa bu patlar. Bu yüzden ayarı source süresince
    # kapatıp, ÖNCEDEN açıksa geri açıyoruz.
    case "$-" in *u*) _u_acikti=1 ;; *) _u_acikti=0 ;; esac
    set +u
    # shellcheck disable=SC1091
    source "$_FASTDDS_WS/install/setup.bash"
    [ "$_u_acikti" = 1 ] && set -u
    unset _u_acikti
else
    echo "UYARI: $_FASTDDS_WS/install/setup.bash yok - once tools/install_fastdds.sh calistirin" >&2
fi

# 3) fastddsgen (IDL -> C++ derleyicisi)
if [ -d "$_FASTDDS_WS/src/fastddsgen/scripts" ]; then
    export PATH="$_FASTDDS_WS/src/fastddsgen/scripts:$PATH"
fi

# fastddsgen bir Java uygulamasi; Java 17 kurulu ise onu tercih et.
if [ -d /usr/lib/jvm/java-17-openjdk-amd64 ]; then
    export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
fi

unset _FASTDDS_WS
