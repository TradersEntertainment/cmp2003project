# FindFast'i Kendi Bilgisayarında Test Etmek

Adımları sırayla izle. Her adımda ne yazacağın ve ne görmen gerektiği yazıyor. Bütün süreç yaklaşık 5 dakika sürer.

> English: [TESTING.md](TESTING.md)

## Adım 0: C++ derleyicisini kur (bir kez)

| Sistem | Ne yapılacak | Çalıştığını kontrol et |
|---|---|---|
| **Windows** | Normal bir PowerShell penceresinde: `Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser`, sonra `Invoke-RestMethod -Uri https://get.scoop.sh \| Invoke-Expression`, sonra `scoop install gcc` | `g++ --version` bir sürüm yazar |
| **Linux** | `sudo apt update`, sonra `sudo apt install g++` | `g++ --version` |
| **macOS** | `xcode-select --install`, açılan pencerede *Install* | `clang++ --version` |

Föyün 04. bölümündeki kurulumun aynısı. `g++` "tanınmıyor" derse terminali kapatıp yenisini aç.

## Adım 1: Projeyi indir

1. https://github.com/TradersEntertainment/cmp2003project adresini aç.
2. Yeşil **Code** düğmesine, sonra **Download ZIP**'e tıkla.
3. ZIP'i aç. Windows'ta sağ tıkla → *Tümünü Ayıkla*.
4. İçinde `findfast.cpp` olan klasörü aç ve orada bir terminal başlat:
   - **Windows:** klasörde boş bir yere sağ tıkla → *Terminalde Aç*.
   - **Linux / macOS:** bir terminal açıp `cd` ile klasöre gir.
5. Doğru yerde olduğunu kontrol et:
   - **Windows:** `Get-ChildItem findfast.cpp`
   - **Linux / macOS:** `ls findfast.cpp`

   Dosya listelenmeli. Listelenmiyorsa yanlış klasördesin.

## Adım 2 (hızlı yol): her şeyi tek komutla test et

| Sistem | Komut |
|---|---|
| Windows | `.\run_tests.bat` (ya da Dosya Gezgini'nde `run_tests.bat` dosyasına çift tıkla) |
| Linux / macOS | `bash run_tests.sh` |

Betik her şeyi derler ve 7 kontrol yapar; her birinin sonunda `PASS` ya da `FAIL` yazar. **Son satır `ALL CHECKS PASSED` olmalı.**

```
[1/7] Compiling findfast.cpp
   PASS: compiled
[2/7] Official tests: ./findfast --test all
   PASS: SUMMARY 20/20
[3/7] Larger inputs, like the hidden tests (4 scales x 3 seeds)
   PASS: 20/20 at every scale and seed
[4/7] Demo: ./findfast --demo
   PASS: output matches the brief
[5/7] Benchmark at grading scale ...
     peak_bytes 149281
   PASS: all answers correct
[6/7] Benchmark on the sample workload ...
     peak_bytes 32540
   PASS: all answers correct
[7/7] Our own tests: student_tests.cpp
   PASS: SUMMARY 11/11

ALL CHECKS PASSED
```

Linux ve macOS'ta derleyici destekliyorsa betik ayrıca bir bellek hatası kontrolü (AddressSanitizer) da yapar.

Her adımı kendin görmek istersen Adım 3'ü uygula.

## Adım 3 (elle): komutları tek tek çalıştır

Aşağıdaki komutlar Linux/macOS biçiminde. **Windows'ta `./findfast` yerine `.\findfast.exe` yaz; `-o`'dan sonra da `findfast.exe` yaz.** macOS'ta `g++` yerine `clang++` yaz.

### 3.1 Derle

```
g++ -std=c++17 -O2 -Wall -Wextra -pedantic findfast.cpp -o findfast
```

Hiçbir şey yazmazsa derleme başarılıdır ve klasörde `findfast` (ya da `findfast.exe`) dosyası oluşur. macOS'ta clang **`move(...)` hakkında 7 uyarı** verir. Bunlar şablonun hocaya ait kısmında, bizim kodumuzda değil. **Değiştirme**: yalnızca işaretli bölge değiştirilebilir.

### 3.2 Resmî 20 test

```
./findfast --test all
```

Beklenen: `PASS T01` … `PASS T20` diye 20 satır, ardından **`SUMMARY 20/20`**.

### 3.3 Gizli testlere benzer büyük girdiler

```
./findfast --test all --scale 500 --seed 42
./findfast --test all --scale 5000 --seed 7
```

Beklenen: her seferinde **`SUMMARY 20/20`**. İstediğin ölçek ve tohumu deneyebilirsin.

### 3.4 Föydeki demo

```
./findfast --demo
```

Beklenen çıktı (föydekiyle birebir aynı):

```
> network
results 2
2 notes.txt
3 report.txt
> network AND security
results 1
3 report.txt
> network OR data
results 3
1 book.txt
2 notes.txt
3 report.txt
```

### 3.5 Not verilen boyutta hız ve bellek

```
./findfast --bench --n 5000 --queries 10000 --updates 5000
./findfast --bench --workload data/small.csv
```

Beklenen: beş satır: `build_seconds`, `lookup_seconds`, `update_seconds`, `total_seconds`, `peak_bytes`.

- **Hata yoksa** bütün cevaplar doğrudur. Yanlış bir cevap ölçümü `error: ...` ile durdurur.
- 64 bitlik bir bilgisayarda `peak_bytes` ilk komutta **149281**, ikincisinde **32540** olmalı.
- Süreler bilgisayara göre değişir. Bizde ilk komutun toplam süresi yaklaşık 0,7 ms.

### 3.6 Kendi testlerimiz

```
g++ -std=c++17 -O2 -Wall -Wextra -pedantic student_tests.cpp -o student_tests
./student_tests
```

Beklenen: `PASS U01` … `PASS U11` diye 11 satır, ardından **`SUMMARY 11/11`**. Bu testler şunları dener: boş kelime, çok uzun kelimeler, alışılmadık baytlar içeren kelimeler, en büyük ListId, `clear`/yeniden kullanım/yıkıcıda bellek, `std::map` ile 1,4 milyon işlemlik karşılaştırma ve 200.000 kelime.

### 3.7 Arama motorunu elle dene

```
./findfast --app --data docs
```

Sonra şu komutları yaz, her birinden sonra Enter'a bas:

```
search network
search network AND security
search data OR algorithms
list
quit
```

`search network` için `results 2` (notes.txt ve report.txt) çıkmalı. `docs` klasörüne kendi `.txt` dosyalarını koyup onlarda da arama yapabilirsin.

## Adım 4: Notlandırma bilgisayarında test (Engine Board)

Yerel testler küçük veriyle çalışır. Gerçek sıralama çok daha büyük gizli iş yükleriyle yapılır.

1. Ders sayfasındaki gönderim sistemini aç.
2. `findfast.cpp` dosyasını yükle. Yalnızca bu dosyayı yükle, `student_tests.cpp`'yi değil.
3. Sistem kodu derler, gizli T01–T20 testlerini ve büyük iş yüklerini koşar, sonucu gösterir. **20/20** yazmalı. Ardından verimlilik puanımız ve sıramız görünür.

İstediğin kadar gönderebilirsin. Son tarihten önceki son gönderim sayılır.

## Bir şey ters giderse

| Sorun | Çözüm |
|---|---|
| `g++` tanınmıyor | Terminali kapatıp yenisini aç. Yine olmazsa Adım 0'ı tekrarla. |
| `findfast.cpp` bulunamıyor | Yanlış klasördesin. Adım 1.4'e dön. |
| Windows "running scripts is disabled" diyor | Bu mesaj `.ps1` dosyaları içindir; `run_tests.bat` etkilenmez. `.\run_tests.bat` yaz ya da çift tıkla. |
| Dosyanın adı `findfast.cpp.txt` | Dosya Gezgini'nde Görünüm → Göster → Dosya adı uzantıları'nı aç ve adını düzelt. |
| `.bat` penceresi hemen kapanıyor | Mesajları okumak için terminalden çalıştır (`.\run_tests.bat`). |
| Düzenlemeden sonra sonuç değişmiyor | Dosyayı kaydet ve yeniden derle (3.1). |
| Herhangi bir `FAIL` | Bütün çıktıyı kopyalayıp bize gönder. |
