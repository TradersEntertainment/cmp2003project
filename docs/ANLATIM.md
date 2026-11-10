# FindFast — Çalışma ve Sunum Notları

Bu dosya, kodu **sözlü sınavda anlatabilmen** için yazıldı. Sırayla oku; her bölümün sonunda hocanın sorabileceği sorular var.

> **Durum (10 Kasım):** Gerçek şablon `findfast.cpp` ve izin verilen yapı listesi 12 Kasım'da gelecek.
> `prototype/` klasöründeki her şey, föydeki sözleşmeyi taklit eden **bizim yazdığımız deneme ortamı**.
> Şablon gelince kazanan yapıyı işaretli bölgeye taşıyacağız.

## 1. Bizden ne isteniyor?

Tek bir sınıf: `StudentIndex`. Bir **sözlük** gibi çalışır: `kelime → liste numarası`.

| Fonksiyon | Ne yapar | Tuzak |
|---|---|---|
| `put(kelime, no)` | Yoksa ekler, varsa numarayı **değiştirir** | İkinci kayıt eklemek; çağıranın metnine işaretçi saklamak |
| `find(kelime, sonuç)` | Varsa `sonuç`'a yazar, `true` döner | Bulamayınca `sonuç`'u değiştirmek |
| `erase(kelime)` | Varsa siler, `true` döner | Sayacı yanlış azaltmak, belleği iki kez bırakmak |
| `size()` | Farklı kelime sayısı | |
| `clear()` | Her şeyi siler, belleği geri verir | İki kez çağrılınca çökmek |
| yıkıcı | `clear()` çağrılmasa da belleği bırakır | Boş bırakmak (sızıntı) |

Yasaklar: `std::vector/map/unordered_map` gibi hazır kaplar, `std::sort/find`, takipsiz `new/malloc`, önbellek (cache), ikinci bir arama yapısı.

## 2. Puan nasıl hesaplanıyor? (Stratejimizi bu belirliyor)

- 10 puan doğruluk: 20 gizli test. **20/20 olmazsa yarışa giremiyoruz.**
- 10 puan verimlilik: `T = 0.30·build + 0.40·lookup + 0.30·update`, `M = bellek`, `E = 10 · 2TM / (T+M)`.

`E` **harmonik ortalama**: zayıf olan taraf sonucu aşağı çeker. Çok hızlı ama belleği 4 kat fazla kullanan motor 4.0 alıyor.
Sonuç: **hem hızlı hem tutumlu** olmalıyız. Tasarımdaki her karar bu ikisini birlikte gözetiyor.

Ölçülen bellek **tepe** (peak) değerdir: bir an bile fazla bellek kullanırsak o sayılır. Bölüm 4'teki "önce bırak, sonra ayır" hilesi bu yüzden var.

## 3. Hangi yapı? Ölçtük, öyle karar verdik

Üç aday yazdık (`prototype/`) ve föydeki yarışı taklit ettik (n = 200 / 1500 / 5000, 2n sorgu, n güncelleme, 7 koşu ortanca):

| n = 5000 | build µs | lookup µs | update µs | tepe bellek | E (üçü arasında) |
|---|---|---|---|---|---|
| Zincirli hash (`hash_chain.hpp`) | 412 | 290 | 198 | 170 KB | 9.30 |
| Açık adresli hash (`hash_open.hpp`) | 323 | 309 | 244 | 163 KB | 9.41 |
| AVL ağacı (`avl.hpp`) | 1010 | 1518 | 1069 | 157 KB | 3.71 |

Üç boyutun ortalaması: zincirli **9.52**, açık adresli 8.53, AVL 4.94.

- **AVL** belleği en az kullanıyor ama 3–5 kat yavaş: her işlemde ~log₂(5000) ≈ 12 düğümde kelime karşılaştırıyor.
- İki **hash tablosu** başa baş. Zincirli olan küçük ve orta boyutta belirgin önde, daha basit ve silmesi çok kolay; **şimdilik favorimiz o**.
- Bu sayılar bizim uydurduğumuz kelimelerle (3–12 harf, rastgele) ölçüldü. Gerçek şablonun `--bench` komutuyla **yeniden ölçmeden** son kararı vermeyeceğiz.

## 4. Zincirli hash tablosu — satır satır mantık (`prototype/hash_chain.hpp`)

### 4.1 Ana fikir
1. Kelimeyi bir sayıya çevir: `hash_of("network") → 3 milyar küsur bir sayı`.
2. O sayının alt bitleri kova numarasıdır: `hash & (kova_sayısı - 1)`.
3. Aynı kovaya düşen kelimeler **tek yönlü bağlı liste** (zincir) oluşturur.
4. Kayıt sayısı `2 × kova sayısı`nı geçince kova dizisini iki katına çıkarırız; böylece zincirler ortalama en fazla 2 kayıt uzunluğunda kalır.

### 4.2 `Node` — tek blokta kayıt
```
[ next (8) | hash (4) | list (4) | len (4) | harfler... ]
                                             ^ 20. bayt
```
- Harfler düğümle **aynı blokta**: ayrı bir ayırma yok → daha az bellek, daha az önbellek ıskalaması.
- `kHeader = 20`: `sizeof(Node)` 24'tür (8'e hizalama dolgusu). Son 4 baytı harflere veriyoruz, kayıt başına 4 bayt kazanç.
- `hash` saklamanın iki faydası: (a) büyümede kelimeyi baştan hash'lemiyoruz, (b) karşılaştırmada önce sayı kıyaslıyoruz, `memcmp` çoğu zaman hiç çalışmıyor.
- `list` tam `uint32_t`: 0 ve 4.294.967.295 geçerli değerler, "boş" işareti olarak kullanmıyoruz (T18).

### 4.3 `hash_of` — FNV-1a
Her harf için: `h ^= harf; h *= 16777619`. Basit, hızlı, iyi dağılım. Sondaki `h ^= h >> 15` üst bitleri alta karıştırır, çünkü kova seçerken yalnızca alt bitleri kullanıyoruz.

### 4.4 `insert_or_assign` (put)
1. Zinciri gez; kelime varsa `list`'i değiştir, **çık** (kopya kayıt yok → T04).
2. Yoksa: gerekirse büyüt, `20 + uzunluk` bayt ayır, harfleri `memcpy` ile **kopyala** (sahipli kopya → T08), zincirin **başına** tak (O(1)).

### 4.5 `lookup` (find)
Zinciri gez; bulursan `out`'a yaz. Bulamazsan `out`'a **hiç dokunma** (T05). Fonksiyon `const`: yapıyı değiştirmez (T07, T19).

### 4.6 `remove` (erase) — işaretçinin işaretçisi
`Node** link`, "silinecek düğümü gösteren işaretçinin adresi"dir. `*link = n->next` demek, düğüm ister zincirin başında ister ortasında olsun onu aradan çıkarır; ayrı `if` gerekmez.

### 4.7 `rehash` — tepe belleği düşüren hile
Klasik yöntem: yeni diziyi ayır → taşı → eskiyi bırak. O an **eski + yeni** dizi birlikte bellekte.
Bizim yöntem: tüm düğümleri `next` işaretçileriyle tek geçici zincire diz → **eski diziyi bırak** → yeni diziyi ayır → dağıt.
Düğümler zaten kendi bloklarında durduğu için bu mümkün. Açık adreslemede mümkün değil; onun tepe belleği bu yüzden zıplıyor.

### 4.8 `clear` ve yıkıcı
Her zinciri gez (**önce `next`'i al, sonra bırak**), sonra kova dizisini bırak, işaretçiyi `nullptr` yap. Boş nesne hiç bellek tutmaz, bu yüzden `clear()`'ı iki kez çağırmak güvenli (T14) ve bellek tam boş seviyeye döner (T13). Yıkıcı sadece `clear()` çağırır (T15).

### 4.9 Karmaşıklık (rapora girecek) — n = kelime sayısı, L = kelime uzunluğu
| İşlem | Ortalama | En kötü |
|---|---|---|
| put / find / erase | O(L) | O(n·L) (herkes aynı kovada; iyi hash ile pratikte olmaz) |
| büyüme | O(n), ama nadir → put amortize O(L) | |
| clear / yıkıcı | O(n) | |
| Bellek | n·(20 + L) + 8·kova, kova ≤ ~n | |

### 4.10 Sorulabilecek sorular
- **Neden kova sayısı 2'nin kuvveti?** `%` yerine `&` kullanmak için; bölme pahalıdır.
- **Neden yük sınırı 2?** Daha düşük = daha hızlı ama daha çok kova (bellek). 2, ölçümlerimizde iyi denge. (Gerçek şablonla 1–4 arası deneyeceğiz.)
- **Amortize O(1) ne demek?** Büyüme n kayıt taşır ama her iki katına çıkışta bir kez olur; toplam taşıma n + n/2 + n/4 + … < 2n.
- **İki kelimenin hash'i aynıysa?** Uzunluk ve `memcmp` ile gerçek karşılaştırma yapıyoruz; hash sadece ön eleme.
- **Neden kopyalıyoruz?** `string_view` sahip değildir; `put` dönünce çağıran metni silebilir.

## 5. Yedek aday: AVL ağacı (`prototype/avl.hpp`)
İzin listesinde hash tablosu yoksa bunu kullanırız. İkili arama ağacı + her düğümde `height`; ekleme/silmeden sonra sol-sağ yükseklik farkı 2 olursa **dönüş** (rotation) ile düzeltilir, böylece derinlik ≤ 1.44·log₂n kalır. Her işlem O(L·log n).

## 6. Testlerimiz (`prototype/lab.cpp`)
- T01–T18'deki her tuzak için elle yazılmış kontroller (4096 karakterlik ve yalnızca son harfi farklı anahtarlar, boş kelime, 0 ve 2³²−1, geçici metin, çift `clear`).
- 3 farklı tohumla 60.000'er rastgele işlem, her adımda `std::map` ile karşılaştırma.
- Her testin sonunda takipli belleğin başlangıç seviyesine döndüğü kontrolü.
- AddressSanitizer + UBSan ile de koşuldu, hata yok.

Çalıştırmak için:
```
cd prototype
g++ -std=c++17 -O2 -Wall -Wextra -pedantic lab.cpp -o lab
./lab
```

## 7. Şablon gelince yapılacaklar
1. İzin verilen yapı listesinde hash tablosu var mı, hangi çeşidi? → aday seçimi.
2. `Key`, `Record`, `memory::` fonksiyonlarının gerçek imzalarına uyarla (örn. `deallocate` boyut istiyor mu?).
3. `memcpy/memcmp` kullanımı kurallara uygun mu kontrol et; değilse elle döngü yaz.
4. `--test all`, `--test all --scale 500 --seed 42`, `--bench --n 5000 --queries 10000 --updates 5000`.
5. Yük sınırı, başlangıç kova sayısı, hash fonksiyonu ince ayarı (gerçek ölçüme göre).
6. 2 sayfalık rapor + katkı beyanı.

**Unutma:** takım kaydı **11 Kasım Çarşamba 23:59**'da kapanıyor.
