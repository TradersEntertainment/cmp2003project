# FindFast: İndeksimiz Nasıl Çalışıyor? (Türkçe)

Bu dosya `findfast.cpp` içindeki kodumuzu baştan sona Türkçe anlatır. Amaç, sözlü sınavda hocanın soracağı **her satırı** rahatça açıklayabilmen.

> İngilizce sürüm (takım arkadaşları ve rapor için): [EXPLANATION.md](EXPLANATION.md). İki dosyanın içeriği aynıdır; burada bazı yerler daha ayrıntılı anlatılıyor.

**İçindekiler**

1. [Görev tek paragrafta](#1-görev-tek-paragrafta)
2. [Neden zincirli hash tablosu?](#2-neden-zincirli-hash-tablosu)
3. [Akılda tutulacak resim](#3-akılda-tutulacak-resim)
4. [Kod, parça parça](#4-kod-parça-parça)
5. [Karmaşıklık](#5-karmaşıklık)
6. [Bellek, bayt bayt](#6-bellek-bayt-bayt)
7. [Ölçümler](#7-ölçümler)
8. [Kurallara nasıl uyuyoruz?](#8-kurallara-nasıl-uyuyoruz)
9. [Derleme, test, ölçüm komutları](#9-derleme-test-ölçüm-komutları)
10. [Hocanın sorabileceği sorular](#10-hocanın-sorabileceği-sorular)
11. [Terimler sözlüğü](#11-terimler-sözlüğü)

---

## 1. Görev tek paragrafta

Şablondaki arama motoru her kelimeyi bir **liste numarasına** (`uint32_t`) eşler. Bizden yalnızca bu eşlemeyi yapan sınıf, `StudentIndex`, isteniyor. Sınıfın beş işlemi var: `put` (ekle ya da numarayı değiştir), `find` (bul), `erase` (sil), `size` (kaç kelime var) ve `clear` (hepsini sil). Bunlara kurucu ve yıkıcı da dahil.

Yasaklar şunlar:

- hazır kaplar (`vector`, `map`, `string`…)
- kütüphanenin arama ve sıralama algoritmaları
- takip dışı bellek (`new`, `malloc`)
- önbellek (eski sorgu cevaplarını saklamak)
- giriş/çıkış ve iş parçacığı

Sakladığımız her kelime **bizim kopyamız** olmalı. Her bayt `memory::allocate` ile alınmalı.

Puanlama iki aşamalı:

- 20 gizli testin hepsini geçmezsek yarışa giremiyoruz.
- Geçersek sıralamayı **zaman** (kurulum %30, arama %40, güncelleme %30) ve **tepe bellek** belirliyor. İkisi harmonik ortalamayla birleşiyor, yani hangisinde zayıf kalırsak puan oraya çekilir.

## 2. Neden zincirli hash tablosu?

Hash tablosu, kelimeden bir sayı hesaplayıp doğrudan doğru yere atlar. Bu yüzden arama süresi kelime sayısıyla büyümez. Diğer seçeneklerin maliyeti:

- **Dengeli ağaç (AVL):** her işlemde yaklaşık log₂ n kelime karşılaştırması gerekir; n = 5000 için 12 karşılaştırma.
- **Sıralı dizi:** her eklemede O(n) kaydırma gerekir.

Kararı tahminle değil **ölçerek** verdik:

- **Yedi farklı** hash tablosu tasarımı yazdık; hepsi tüm testleri geçti.
- Çoğu takımın yazacağı türden **iki "tipik" çözüm** de yazdık, nerede durduğumuzu görmek için.
- Hepsini aynı makinede yarıştırdık. Her iş yükü 31 kez koştu ve resmî puanlamadaki gibi **ortanca** alındı.

Puanlar föydeki formülle hesaplandı. Her maliyet, denediğimiz tasarımların en iyisine göre oranlandı:

| Tasarım | Zaman oranı T | Bellek oranı M | Puan E (10 üzerinden) |
|---|---|---|---|
| **Zincirli, sıkı düğüm + 1 baytlık parmak izi (seçimimiz)** | **0.93** | **0.94** | **9.3** |
| Zincirli, sıkı düğüm, parmak izi yok | 0.84 | 0.97 | 9.0 |
| Açık adresleme + 1 baytlık kontrol dizisi | 0.87 | 0.88 | 8.7 |
| Zincirli, 2'nin kuvveti olmayan büyüme | 0.77 | 0.99 | 8.6 |
| Zincirli, 20 baytlık düğüm başlığı (ilk prototipimiz) | 0.77 | 0.76 | 7.7 |
| Açık adresleme, düz işaretçiler | 0.74 | 0.79 | 7.4 |
| *Tipik: düğümde `Record` + `std::hash`* | 0.70 | 0.54 | 6.1 |
| *Tipik: `Record` dizisi üzerinde açık adresleme* | 0.82 | 0.25 | 3.8 |

Kazandıran üç fikir:

1. **Kelime başına tek bellek bloğu, harfler bloğun içinde.** Şablondaki `Record` kullanılsaydı kayıt 24 bayt tutardı ve harfler için ikinci bir ayırma gerekirdi. Biz bağlantıyı, liste numarasını, parmak izini, uzunluğu ve harfleri tek bir `14 + L` baytlık bloğa koyuyoruz.
2. **1 baytlık parmak izi (fingerprint).** Zinciri gezerken başka bir kelimeyi tutan düğümü tek bayta bakarak eliyoruz; harf karşılaştırması yapmıyoruz.
3. **Bellek sıçraması olmadan büyüme.** Tablo büyürken eski kova dizisini yenisini istemeden **önce** bırakıyoruz (4.9). Puan **tepe** belleğe göre verildiği için bu fark doğrudan puana yansıyor.

Ayrıca kova başına kelime sayısı sınırını (yük faktörü) 1 ile 4 arasında taradık. Hız ve bellek arasındaki en iyi denge **2**'de çıktı; kullandığımız değer bu.

## 3. Akılda tutulacak resim

```
 buckets_ (2^k işaretçilik dizi)
 ┌─────┐
 │  0  │──► [düğüm "data3c4"] ──► [düğüm "catalog88"] ──► nullptr
 ├─────┤
 │  1  │──► nullptr
 ├─────┤
 │  2  │──► [düğüm "sharedprefixarchive0001c5"] ──► nullptr
 ├─────┤
 │ ... │
 └─────┘

 Bir düğüm = tek bir memory::allocate bloğu (128 harften kısa kelimeler için 14 + L bayt)
 ┌──────────────┬──────────────┬─────────────┬────────────┬──────────────────┐
 │ next (8 B)   │ list (4 B)   │ p.izi (1)   │ uzunluk(1) │ harfler (L B)    │
 └──────────────┴──────────────┴─────────────┴────────────┴──────────────────┘
   konum 0        konum 8        konum 12      konum 13     konum 14
```

Bir kelimeyi aramak dört adımdır:

1. Kelimenin 64 bitlik hash'ini (`h`) hesapla.
2. `h`'nin **üst** bitleri kovayı seçer: `h >> shift_`.
3. `h`'nin **alt** 8 biti parmak izidir.
4. Zinciri gez. Parmak izi tutmayan düğümü atla. Tutanda önce uzunluğu, sonra harfleri karşılaştır.

## 4. Kod, parça parça

Aşağıdaki her şey `// BEGIN STUDENT IMPLEMENTATION` ile `// END STUDENT IMPLEMENTATION` arasında. İşaretlerin dışındaki hiçbir şey değişmedi; `make_index` de olduğu gibi duruyor.

### 4.1 `Node` yapısı ve konumlar (offset)

```cpp
struct Node {
    Node* next;
};
static constexpr size_t kListOffset = sizeof(Node);                    // 8
static constexpr size_t kPrintOffset = kListOffset + sizeof(ListId);   // 12
static constexpr size_t kLengthOffset = kPrintOffset + 1;              // 13
```

Kelimelerin uzunluğu farklı olduğu için düğümün boyutu da değişken. C++'ta bir struct'ın boyutu değişken olamaz. Bu yüzden `Node` yalnızca bağlantıyı (`next`) tanımlıyor. Diğer alanlar aynı blokta sabit bayt konumlarında duruyor.

Neden bütün alanları struct'a koymadık? `{Node* next; ListId list; unsigned char print;}` derleyici tarafından 16 bayta tamamlanır ve her kelimede 2–3 bayt boşa gider. Konum kullanınca harfler uzunluk baytının hemen arkasından başlıyor.

### 4.2 `StudentIndex`'in alanları

```cpp
Node** buckets_ = nullptr;   // zincir başlarının dizisi
size_t bucket_count_ = 0;    // 0 ya da 2'nin kuvveti
size_t count_ = 0;           // farklı kelime sayısı
unsigned shift_ = 64;        // kova numarası = hash >> shift_
```

- `buckets_ == nullptr` "hiçbir şey saklanmıyor" demek. Kurucu bellek ayırmaz, bu yüzden yeni nesne hiç dinamik bellek tutmaz (T01, T13).
- `count_` sayesinde `size()` O(1).
- `shift_ = 64 - log2(kova sayısı)`. 8 kova varken `shift_ = 61`, yani `h >> 61` sonucu 0 ile 7 arasında olur. Başlangıçtaki 64 değeri hiçbir kaydırmada kullanılmaz; her işlem önce `buckets_`'ı kontrol eder.

Sabitler:

- `kInitialBits = 3`: ilk tablo 8 kova.
- `kMaxLoad = 2`: kelime sayısı kova sayısının 2 katını geçecekse tablo büyür.

### 4.3 `load8` / `load4`: 8 harfi tek seferde okumak

```cpp
static uint64_t load8(const char* p) {
    uint64_t value;
    char_traits<char>::copy(reinterpret_cast<char*>(&value), p, 8);
    return value;
}
```

Kelimenin 8 baytını 64 bitlik bir tamsayıya kopyalar. Derleyici bunu **tek** bir okuma komutuna çevirir.

`*(uint64_t*)p` yazmıyoruz. Kelime herhangi bir adreste başlayabilir ve böyle bir dönüşüm C++'ta tanımsız davranıştır. `char_traits<char>::copy`, şablondaki `Key` sınıfının da harf kopyalamak için kullandığı fonksiyondur; arama ya da sıralama algoritması değil, düz kopyadır.

### 4.4 `hash_of`: kelimeyi 64 bitlik sayıya çevirmek

```cpp
uint64_t h = 0x9E3779B97F4A7C15ull ^ n;          // başlangıç: sabit sayı + uzunluk
while (n > 8) {                                  // her adımda 8 harf
    h = (h ^ load8(p)) * 0xFF51AFD7ED558CCDull;  // bloğu kat, çarparak yay
    h ^= h >> 32;                                // üst yarıyı alt yarıya karıştır
    p += 8; n -= 8;
}
tail = ... son 1–8 harf ...
h = (h ^ tail) * 0xC4CEB9FE1A85EC53ull;          // son karıştırma ("çığ etkisi")
h ^= h >> 29;
h *= 0xFF51AFD7ED558CCDull;
h ^= h >> 32;
```

**Neden adım başına 8 harf?** İlk prototipimiz FNV-1a kullanıyordu; o her adımda tek harf işler. 14 harflik bir kelime için arka arkaya 14 çarpma demek. Aramalarımız onunla yaklaşık %30 daha yavaştı.

**Neden çarpma?** Büyük bir tek sayıyla çarpmak, her girdi bitini birçok üst bite yayar. `h ^= h >> 32` de üst bitleri aşağı geri getirir. İkisi birlikte, kelimedeki küçük bir değişikliğin ("data332" → "data333") hash'in yaklaşık yarısını değiştirmesini sağlar.

**Sabitler nereden geliyor?** Bilinen 64 bitlik karıştırma sabitleri: altın oran sabiti 0x9E3779B97F4A7C15 ve MurmurHash3'ün iki son-karıştırma çarpanı. Düzensiz bitleri olan büyük bir tek sayı iş görür; bunlar yaygın kullanılıp iyi test edildiği için seçildi.

**Neden uzunluğu da katıyoruz?** Birbirinin öneki olan kelimeler ("net", "network") farklı bir başlangıç durumundan çıksın diye.

**Kuyruk (son harfler):**

- En az 8 harf: kelimenin **son** 8 baytını okuyoruz. Bu, önceki blokla çakışabilir ama sorun değil, çünkü yine kelimenin kendi harflerini okuyoruz.
- 4–7 harf: çakışan iki 4 baytlık okuma.
- 4'ten kısa: harf harf kısa bir döngü.
- Boş kelime: `tail = 0`, hiçbir şey okunmaz.

**Kalite kontrolü.** Hash'i birçok anahtar ailesinde ölçtük: `term<i>`, `sharedprefixarchive%06x`, ondalık sayılar, bütün 3 harfli kelimeler, 4096 harflik anahtarlar. Kovalara dağılım düzgün rastgele dağılımla aynı çıktı; parmak izi baytları da dengeli dağıldı. Daha ucuz bir son karıştırma da denedik. Ölçülebilir şekilde hızlı değildi ve `term<i>` anahtarlarında parmak izini kötü dağıttı, o yüzden tam karıştırmayı tuttuk.

### 4.5 `fingerprint`, `list_of`, `set_list`, `fingerprint_of`

```cpp
static unsigned char fingerprint(uint64_t h) { return static_cast<unsigned char>(h); }   // alt 8 bit
```

Kova **üst** bitlerden, parmak izi **alt** bitlerden geliyor. Böylece aynı kovaya düşen iki kelimenin parmak izleri birbirinden bağımsız olur. Yanlış bir kelime parmak izi testini 256'da 1 ihtimalle geçer.

`list_of` ve `set_list`, 8. konumdaki 4 baytlık liste numarasını `char_traits<char>::copy` ile okuyup yazar. Ham bir bayt bloğunun içindeki tamsayıya erişmenin kesin doğru yolu budur ve tek bir `mov` komutuna derlenir. `fingerprint_of` 12. baytı okur; herhangi bir belleği `unsigned char` olarak okumak her zaman serbesttir.

### 4.6 Uzunluk, varint olarak: `varint_size`, `write_varint`, `word_of`

Kelimeler her uzunlukta olabilir: genel testlerde 4096 harf var, gizli testlerde daha uzunu olabilir. Uzunluğu `size_t` olarak saklasaydık kelime başına 8 bayt giderdi; oysa kelimelerin neredeyse hepsi kısa. Bu yüzden **varint** kullanıyoruz:

- Her bayt uzunluğun 7 bitini taşır.
- En üst bit (128) "arkada bir bayt daha var" demektir.

| Uzunluk | Bayt sayısı | Kodlama |
|---|---|---|
| 0–127 | 1 | `[L]` |
| 128–16.383 | 2 | `[L & 127 | 128][L >> 7]` |
| 16.384–2.097.151 | 3 | … |

`word_of(düğüm)` uzunluğu çözer (hızlı yol: 128'den küçük tek bayt) ve saklanan harfleri gösteren bir `string_view` döndürür. Bütün karşılaştırmalar bunun üzerinden yapılır.

### 4.7 `equal_text` ve `holds`: aradığımız kelime bu mu?

```cpp
static bool holds(const Node* node, unsigned char print, string_view word) {
    if (fingerprint_of(node) != print) return false;      // 256 yanlış kelimeden 255'i burada elenir
    const string_view stored = word_of(node);
    return stored.size() == word.size() && equal_text(stored.data(), word.data(), word.size());
}
```

`equal_text`, `load8` ile her adımda 8 harf karşılaştırır; son blokta aynı çakışma hilesini kullanır. Yalnızca "eşit mi, değil mi" sorusuna cevap verir. Bize gereken de bu kadar, bu yüzden "hangisi küçük" sorusunu da cevaplamak zorunda olan genel karşılaştırmadan daha hızlıdır.

### 4.8 `make_node` ve `free_chain`

```cpp
const size_t size = kLengthOffset + varint_size(word.size()) + word.size();   // 13 + 1 + L
char* raw = static_cast<char*>(memory::allocate(size));                        // takipli bellek
Node* node = ::new (raw) Node{next};      // placement new: hazır bloğun içine bağlantı nesnesini kur
set_list(node, list);
raw[kPrintOffset] = static_cast<char>(print);
char* text = write_varint(raw + kLengthOffset, word.size());
char_traits<char>::copy(text, word.data(), word.size());                       // KENDİ KOPYAMIZ
```

- **Placement new** (`::new (raw) Node{next}`) bellek ayırmaz. `memory::allocate`'ten zaten aldığımız alanın içinde bir `Node` nesnesi kurar. Şablondaki `memory::make` de aynı tekniği kullanır.
- Harfler **kopyalanır**, çünkü çağıranın `string_view`'i `put` döner dönmez yok olacak geçici bir string'i gösteriyor olabilir (T08).
- `free_chain`, düğümü bırakmadan **önce** `next`'i okur. Bırakılmış belleğe bir daha dokunmak yasaktır.

### 4.9 `rehash`: bellek sıçraması olmadan büyümek

```cpp
Node* all = nullptr;                    // 1. BÜTÜN düğümleri tek bir geçici listeye diz
her kova için: düğümlerini all'ın başına taşı
memory::deallocate(buckets_);           // 2. ÖNCE eski diziyi bırak
buckets_ = allocate(yeni boyut);        // 3. ANCAK SONRA yeni diziyi iste
all'daki her düğümü buckets_[hash >> shift_] zincirinin başına tak   // 4. dağıt
```

Klasik yöntem şöyledir: yeni diziyi ayır, düğümleri taşı, eskiyi bırak. Bu yöntemde bir an iki dizi birlikte bellekte durur ve not **tepe** değere göre verilir.

Bizim düğümlerimiz kendi `next` bağlantılarını taşıdığı için hepsini tek bir geçici listeye park edebiliyoruz. Eski dizi o anda gereksiz hâle geliyor; önce onu bırakıp sonra yenisini istiyoruz. **Bu yüzden tepe belleğimiz, son tablonun belleğine eşit.** Bunu bayt bayt bir modelle doğruladık: n = 5000 için modelin öngördüğü tepe (176.022 bayt) ölçümle birebir aynı.

Düğümde hash'in tamamını saklamıyoruz, bu yüzden `rehash` hash'i saklanan harflerden yeniden hesaplıyor. Hash'i saklayıp bunu atlayan bir sürüm de denedik (kelime başına +4 bayt). Pratikte hızlı değildi, o 4 bayta değmedi.

Büyüme tabloyu iki katına çıkarır. Bir kelime ortalamada O(1) kez taşınır (1 + ½ + ¼ + … < 2), bu yüzden `put` amortize O(L)'dir.

### 4.10 `put`

```cpp
const uint64_t h = hash_of(word);
const unsigned char print = fingerprint(h);
if (buckets_) {
    for (Node* node = buckets_[h >> shift_]; node; node = node->next)
        if (holds(node, print, word)) { set_list(node, list); return; }   // var olan kelime: numarayı değiştir
}
if (!buckets_) rehash(kInitialBits);                                       // ilk kelime
else if (count_ >= kMaxLoad * bucket_count_) rehash(64 - shift_ + 1);     // fazla dolu: iki katına çık
Node*& head = buckets_[h >> shift_];
head = make_node(word, list, print, head);                                // zincirin BAŞINA ekle
++count_;
```

- Var olan kelimenin yalnızca numarası değişir. İkinci kayıt açılmaz, `count_` değişmez (T04, T12).
- Tablo, kova hesaplanmadan **önce** büyütülür. Böylece yeni düğüm son tablonun doğru kovasına düşer.
- Zincirin başına eklemek O(1)'dir.

### 4.11 `find`

`put`'taki gezintinin aynısı:

- Bulursa `result`'a yazar ve `true` döner.
- Bulamazsa `result`'a **hiç dokunmadan** `false` döner (T05).
- `find` yapıyı asla değiştirmez: sıra değiştirmez, önbellek tutmaz (T07, T19).

### 4.12 `erase` ve işaretçinin işaretçisi

```cpp
for (Node** link = &buckets_[h >> shift_]; *link; link = &(*link)->next) {
    Node* node = *link;
    if (holds(node, print, word)) {
        *link = node->next;          // önceki işaretçi artık bu düğümün üstünden atlıyor
        memory::deallocate(node);    // düğümü (kelimesiyle birlikte) geri ver
        --count_;
        return true;
    }
}
return false;
```

`link`, o anki düğümü gösteren **işaretçinin adresini** tutar. İlk düğümde bu, kovanın kendisidir; sonra bir önceki düğümün `next` alanı olur. Bu yüzden `*link = node->next` tek satırı, düğüm zincirin başında da olsa ortasında da olsa onu çıkarır; "baştaki düğüm" için ayrı kod gerekmez. Olmayan kelimede `false` döner ve `count_`'a dokunmaz (T10).

### 4.13 `size`, `clear`, yıkıcı

- `size()` yalnızca `count_`'u döndürür. `const`'tur, hiçbir şeyi değiştirmez.
- `clear()` her zinciri bırakır, kova dizisini bırakır ve bütün alanları kurucudaki hâline getirir. İkinci kez çağrılması güvenlidir: `bucket_count_` 0 olduğu için döngü hiç dönmez, `deallocate(nullptr)` da hiçbir şey yapmaz. Takipli bellek boş nesne seviyesine tam olarak döner (T13, T14).
- Yıkıcı `clear()`'ı çağırır. Dolu bir indeks, kimse `clear()` çağırmasa bile her şeyi geri verir (T15).

## 5. Karmaşıklık

n = saklanan kelime sayısı, L = işlemdeki kelimenin uzunluğu, b = kova sayısı (ilk büyümeden sonra n/2 ≤ b ≤ n).

| İşlem | Ortalama | En kötü | Neden |
|---|---|---|---|
| `put` (yeni kelime) | amortize O(L) | O(n·L) | hash O(L) + ortalama ≤ 2 düğümlük zincir + O(L) kopya; iki katına çıkma O(n) ama nadir |
| `put` (var olan kelime) | O(L) | O(n·L) | hash + zincir gezme + karşılaştırma |
| `find` | O(L) | O(n·L) | hash + zincir; parmak izi çoğu karşılaştırmayı atlatır |
| `erase` | O(L) | O(n·L) | aynı gezinti + O(1) çıkarma |
| `size` | O(1) | O(1) | saklanan sayaç |
| `clear` / yıkıcı | O(n + b) | O(n + b) | her düğüm ve dizi bir kez bırakılır |
| kurucu | O(1) | O(1) | bellek ayırmaz |
| **Bellek** | Σ(14 + Lᵢ) + 8·b + 40 bayt | | kelime başına bir blok + kova dizisi + nesnenin kendisi |

En kötü durum, kelimelerin neredeyse hepsi aynı kovaya düşerse olur. İyi bir 64 bitlik hash ile gerçek kelimelerde bu pratikte imkânsızdır.

## 6. Bellek, bayt bayt

5000 anahtarlı iş yükü için kesin model (ortalama kelime uzunluğu 14,6):

| Parça | Bayt |
|---|---|
| 5000 düğüm × (14 + L) | 143.214 |
| kova dizisi: 4096 × 8 | 32.768 |
| `StudentIndex` nesnesi (sanal tablo işaretçisi + 4 alan) | 40 |
| **tepe (model = ölçüm)** | **176.022** |

Kelime başına yaklaşık **35 bayt**; bunun 14,6 baytı kelimenin kendisi. Karşılaştırma için aynı iş yükünde:

- `Record` tutan düğüm + `std::hash`: 299.000 bayt
- açık adreslemeli `Record` dizisi: 674.000 bayt

## 7. Ölçümler

`./findfast --bench` ile 61 koşunun ortancası (Intel Xeon 2.3 GHz, `g++ -std=c++17 -O2`):

| İş yükü | Kurulum | Arama | Güncelleme | Tepe bayt |
|---|---|---|---|---|
| sentetik `--n 200` | 11,9 µs | 7,3 µs | 5,3 µs | 5.389 |
| sentetik `--n 1500` | 88,8 µs | 59,4 µs | 41,6 µs | 42.092 |
| sentetik `--n 5000 --queries 10000 --updates 5000` | 318,9 µs | 185,8 µs | 147,1 µs | 149.281 |
| `data/small.csv` benzeri CSV, 5000 anahtar | 378,5 µs | 229,3 µs | 171,3 µs | 176.022 |

Bütün tablolar ve onları üreten betikler [`research/`](../research) klasöründe.

## 8. Kurallara nasıl uyuyoruz?

| Kural (föy / şablon) | Bizdeki karşılığı |
|---|---|
| Yalnızca işaretli bölge değişti, `make_index` aynı | `research/tools/check_rules.py` otomatik kontrol ediyor: bölge dışı şablonla bayt bayt aynı |
| Hazır kap yok | Sadece ham diziler ve kendi düğümlerimiz; bölgede `vector`, `map`, `string`… yok |
| Kütüphane arama/sıralama algoritması yok | Hiç kullanılmıyor. `char_traits<char>::copy` sadece bayt kopyalar (şablondaki `Key` de kullanıyor) |
| İkinci arama yapısı, önbellek, sabit cevap yok | Tek bir hash tablosu. `find` salt okunur. Hiçbir şey belirli kelimelere ya da iş yükü boyutlarına bağlı değil |
| Giriş/çıkış, iş parçacığı yok | Yok |
| Bütün dinamik bellek takipli | Her ayırma `memory::allocate`, her bırakma `memory::deallocate`. Placement new yalnızca bu blokların içinde nesne kurar |
| Kelimeler sahipli kopya | `make_node` harfleri düğüme kopyalıyor |
| `find` bulamazsa `result` değişmez | `result` yalnızca bulununca yazılıyor |
| ListId 0 ve 4.294.967.295 sıradan değerler | Tam `uint32_t` olarak saklanıyor; hiçbir değer işaret olarak kullanılmıyor |
| `clear` iki kez, `clear` sonrası kullanım, `clear`'sız yıkım | T13–T15 ve bizim U07 testimiz kapsıyor |

## 9. Derleme, test, ölçüm komutları

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pedantic findfast.cpp -o findfast
./findfast --test all                        # SUMMARY 20/20
./findfast --test all --scale 5000 --seed 7  # gizli testlere benzer büyük deneme
./findfast --demo
./findfast --bench --n 5000 --queries 10000 --updates 5000
./findfast --bench --workload data/small.csv

g++ -std=c++17 -O2 -Wall -Wextra -pedantic student_tests.cpp -o student_tests
./student_tests                              # bizim 11 ek testimiz: SUMMARY 11/11
```

Windows'ta `./findfast` yerine `.\findfast.exe` yaz. Bütün testleri AddressSanitizer + UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`) ile ve clang++ ile de koştuk; hata yok.

## 10. Hocanın sorabileceği sorular

**Neden ağaç değil de hash tablosu?** Ağaç her işlemde yaklaşık log₂ n kelime karşılaştırması ister. Hash tablosu tek bir hash ve ortalamada ikiden az düğüm ziyareti ister. Prototipte AVL ağacını da ölçtük: n = 5000'de 3–5 kat yavaştı.

**Çakışma (collision) nedir, nasıl çözüyorsunuz?** İki farklı kelimenin aynı kovaya düşmesidir. Onları bağlı listede (ayrık zincirleme) tutup doğru kelimeyi bulana kadar karşılaştırıyoruz.

**Yük faktörü nedir, neden 2?** Kova başına düşen kelime sayısı. Küçük olursa zincirler kısalır ama kova dizisi büyür. 1, 1.5, 2, 2.5, 3 ve 4'ü ölçtük; birleşik puan en iyi 2'de çıktı.

**Kova sayısı neden 2'nin kuvveti?** Kova numarası hash'in üst bitleri olsun diye (`h >> shift_`). Yavaş bölme işlemine gerek kalmaz.

**Neden `h % boyut` değil de üst bitler?** Kaydırma tek bir komuttur ve hash'imizin üst bitleri iyi karışmıştır. Ayrıca alt bitler parmak izi için boşta kalır.

**Parmak izi ne kazandırıyor?** Zincirde başka kelimeler varsa her birini uzunluk ve harf karşılaştırması yerine tek bayta bakarak eliyoruz. Kelime başına 1 bayt karşılığında motoru yaklaşık %10 hızlandırdı.

**İki farklı kelimenin hash'i aynıysa?** Yine doğru çalışır. Hash ve parmak izi yalnızca **eleme** yapar; bir kelime ancak uzunluğu ve bütün harfleri tutunca kabul edilir.

**Kelimeyi neden kopyalıyorsunuz?** `string_view` harflerin sahibi değildir. Çağıran, `put` döndükten sonra metni yok edebilir (T08). Kendi kopyamız şart.

**Placement new nedir?** `::new (adres) T{...}`, zaten ayrılmış bir adreste nesne kurar; bellek ayırmaz. Şablondaki `memory::make` de bunu kullanıyor.

**Neden işaretçi dönüşümü değil de `char_traits<char>::copy`?** `char*`'ı `uint64_t*`'a çevirip okumak, adres hizalı değilse ya da bellekte başka türde veri varsa tanımsız davranıştır. Baytları kopyalamak her zaman geçerlidir ve derleyici tek bir okuma komutu üretir.

**Varint nedir, neden kullandınız?** Değişken uzunluklu tamsayı: bayt başına 7 bit, en üst bit "devamı var" demek. Normal kelimelerin uzunluğu 8 yerine 1 bayt tutuyor, yine de her uzunluk saklanabiliyor.

**`rehash` neden yeni diziyi istemeden önce eskisini bırakıyor?** Puan tepe belleğe göre. Normalde bir an iki dizi birden bellekte durur. Biz önce bütün düğümleri tek bir geçici listeye taşıdığımız için eski diziye ihtiyaç kalmıyor. Tepe değerimiz son tablonun boyutu kadar.

**`erase`'deki `Node** link` ne işe yarıyor?** O anki düğümü gösteren işaretçiyi gösterir: önce kovanın kendisini, sonra bir `next` alanını. `*link = node->next` yazınca düğüm zincirin neresinde olursa olsun, ayrı bir durum yazmadan çıkarılır.

**`clear` iki kez çağrılırsa ne olur?** İkinci çağrıda `bucket_count_ == 0` olduğu için döngü dönmez ve `deallocate(nullptr)` hemen döner. Çift serbest bırakma olmaz.

**Kurucu neden bellek ayırmıyor?** T13, `clear()`'dan sonra takipli belleğin boş nesne seviyesine eşit olmasını kontrol ediyor. Kurucu bellek ayırsaydı `clear` o ayırmayı aynen yeniden yapmak zorunda kalırdı. İlk `put`'ta ayırmak hem daha basit hem daha tutumlu.

**`put`'un karmaşıklığı nedir?** Ortalama amortize O(L): hash ve kopya O(L), beklenen zincir uzunluğu O(1). İki katına çıkma O(n) ama ancak n/2 eklemede bir oluyor. En kötü durumda, bütün kelimeler çakışırsa, O(n·L).

**Kelime başına ne kadar bellek?** Düğüm için 14 + L bayt, artı kova dizisinden 4–8 bayt. 5000 anahtarlı iş yükünde yaklaşık 35 bayt.

**Daha fazla zamanınız olsa neyi iyileştirirdiniz?** Milyonlarca kelime için 2 kat yerine 1,5 kat büyüme yük faktörünü daha dar bir aralıkta tutardı. Denedik: yaklaşık %2 bellek kazandırdı ama yaklaşık %8 zaman kaybettirdi, burada değmedi.

## 11. Terimler sözlüğü

| Terim | Anlamı |
|---|---|
| hash (özet) | Kelimeden hesaplanan, kelimeyi temsil eden büyük sayı |
| bucket (kova) | Hash tablosundaki bir göz; aynı göze düşen kelimeler zincir oluşturur |
| chaining (zincirleme) | Aynı kovadaki kelimeleri bağlı listede tutma yöntemi |
| load factor (yük faktörü) | Kova başına ortalama kelime sayısı |
| rehash | Tabloyu büyütüp bütün kelimeleri yeni kovalara dağıtma |
| fingerprint (parmak izi) | Hash'in küçük bir parçası; yanlış adayları hızla elemek için |
| varint | 7'şer bitlik baytlarla yazılan değişken uzunluklu sayı |
| amortized (amortize) | Pahalı işlemlerin maliyeti çok sayıda ucuz işleme yayılınca işlem başına ortalama |
| peak memory (tepe bellek) | Çalışma boyunca herhangi bir anda kullanılan en yüksek bellek |
| placement new | Hazır bellek alanında nesne kuran `new` biçimi; bellek ayırmaz |
| owned copy (sahipli kopya) | Bizim ayırıp bizim bıraktığımız, kelimenin kendi kopyası |
