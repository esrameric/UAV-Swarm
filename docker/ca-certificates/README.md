# Ek kök sertifikalar (opsiyonel)

Bu dizin **normalde boştur** ve öyle kalmalıdır.

## Ne işe yarıyor?

Bazı kurumsal ağlar HTTPS trafiğini denetleyen bir proxy (TLS inspection)
arkasındadır. Böyle bir ağda dışarıya yapılan her TLS bağlantısı, kurumun
kendi kök sertifikasıyla imzalanmış görünür. Host makinede bu sertifika
sisteme kurulu olduğu için `curl`, `git` ve `pip` sorunsuz çalışır — ama
Docker container'ı temiz bir sistem olduğu için bunu tanımaz.

Özellikle **Java kendi ayrı truststore'unu** kullanır; sistem sertifikaları
kurulsa bile Java'ya ayrıca tanıtılması gerekir. `fastddsgen` bir Java
uygulaması olduğu ve derlenirken Gradle'ı indirdiği için, sertifika eksikse
imaj derlemesi şu hatayla durur:

```
unable to find valid certification path to requested target
```

## Nasıl kullanılır?

Elle bir şey yapmanıza gerek yok: `tools/build_docker_image.sh` çalıştığında
host'taki `/usr/local/share/ca-certificates/*.crt` dosyalarını buraya
kopyalar, imaj derlendikten sonra da siler.

Kurumsal proxy'nin arkasında değilseniz dizin boş kalır ve Dockerfile'daki
sertifika adımı hiçbir şey yapmaz.

## Neden depoya commit edilmiyor?

Sertifikaların kendisi gizli bilgi değildir, ama kuruma özgüdür ve projenin
bir parçası sayılmazlar. Depoyu başka bir ortamda kullanan biri kendi
sertifikalarını kullanmalıdır. Bu yüzden `.gitignore` bu dizindeki `*.crt`
dosyalarını dışarıda bırakır.
