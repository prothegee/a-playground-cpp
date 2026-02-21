# RINGKASAN PERBANDINGAN TEKNIK I/O CEPAT

| Teknik | Speed I/O ⚡ | Fault Tolerance 🛡️ | Persistensi | Kompleksitas | Use Case |
|--------|:------------:|:-------------------:|:------------:|:-------------:|----------|
| **mmap** | 9 / 10 | 5 / 10 | Opsional | Sederhana | File besar, akses random |
| **Page Cache** | 8 / 10 | 7 / 10 | Ya (file) | Sederhana | File yang sering diakses |
| **Zero-Copy** | 10 / 10 | 3 / 10 | Tidak | Kompleks | Transfer data cepat |
| **Redis** | 9 / 10 | 6-8 / 10 | Opsional | Sederhana | Cache terdistribusi |
| **tmpfs** | 10 / 10 | 1 / 10 | Tidak | Sangat mudah | File sementara di RAM |
| **PMEM** | 9 / 10 | 9 / 10 | Ya | Kompleks | Data persisten cepat |
| **App Cache** | 8 / 10 | 4 / 10 | Tidak | Sederhana | Hot data lokal |

---

## DETAIL LENGKAP

### 1. **mmap** (Memory-Mapped Files)
- **Speed**: 9/10 - Mapping langsung ke virtual memory, akses seperti RAM
- **Fault Tolerance**: 5/10 - Tergantung file backing (bisa 10 jika read-only dan file aman)
- **Persistensi**: Opsional (jika MAP_SHARED + file backing)
- **Kompleksitas**: Sederhana
- **Use Case**: File besar, akses random
- **Kelebihan**: Zero-copy antara kernel-userspace untuk akses data
- **Kekurangan**: Ukuran terbatas address space (32-bit)

### 2. **Page Cache** (Kernel Cache)
- **Speed**: 8/10 - Cepat karena kernel cache, masih ada overhead syscall
- **Fault Tolerance**: 7/10 - Data aman karena ada di file disk
- **Persistensi**: Ya (file di disk)
- **Kompleksitas**: Sederhana (transparan ke aplikasi)
- **Use Case**: File yang sering diakses
- **Kelebihan**: Otomatis dikelola kernel, tidak perlu kode khusus
- **Kekurangan**: Tidak ada kontrol langsung, bisa ter-flush

### 3. **Zero-Copy** (sendfile/splice)
- **Speed**: 10/10 - Maksimal, tanpa copy ke userspace
- **Fault Tolerance**: 3/10 - Tidak ada persistensi, data langsung dikirim
- **Persistensi**: Tidak
- **Kompleksitas**: Kompleks
- **Use Case**: Transfer data cepat (network ke disk atau sebaliknya)
- **Kelebihan**: Efisien CPU, throughput tinggi
- **Kekurangan**: Kasus penggunaan terbatas

### 4. **Redis** (In-Memory Database)
- **Speed**: 9/10 - In-memory, sangat cepat, minus network overhead
- **Fault Tolerance**: 6-8/10
  - Tanpa persistensi: 6
  - Dengan AOF/RDB: 8
- **Persistensi**: Opsional (AOF, RDB snapshot)
- **Kompleksitas**: Sederhana (client library tersedia)
- **Use Case**: Cache terdistribusi, session store
- **Kelebihan**: Data struktur kaya, replikasi
- **Kekurangan**: Network latency, memory usage

### 5. **tmpfs** (RAM-based Filesystem)
- **Speed**: 10/10 - RAM murni, tanpa overhead block layer
- **Fault Tolerance**: 1/10 - Data hilang saat reboot/unmount
- **Persistensi**: Tidak
- **Kompleksitas**: Sangat mudah (seperti file biasa)
- **Use Case**: File sementara di RAM, IPC cepat
- **Kelebihan**: API file standar, kecepatan RAM
- **Kekurangan**: Volatile, ukuran terbatas RAM

### 6. **PMEM** (Persistent Memory)
- **Speed**: 9/10 - Mendekati DRAM, ada overhead flush instructions
- **Fault Tolerance**: 9/10 - Data tetap ada meski power loss
- **Persistensi**: Ya (native persistent)
- **Kompleksitas**: Kompleks
- **Use Case**: Data persisten cepat, database
- **Kelebihan**: Byte-addressable, fast recovery
- **Kekurangan**: Hardware mahal, programming model baru

### 7. **App Cache** (Application-Level Cache)
- **Speed**: 8/10 - Di heap memory, akses langsung
- **Fault Tolerance**: 4/10 - Data hilang saat proses crash/restart
- **Persistensi**: Tidak
- **Kompleksitas**: Sederhana
- **Use Case**: Hot data lokal, temporary objects
- **Kelebihan**: Full kontrol, zero overhead
- **Kekurangan**: Manajemen manual, tidak shared

---

## MATRIKS PERBANDINGAN

```
Speed I/O
   10 │  ● Zero-Copy  ● tmpfs
      │      ● PMEM
    9 │  ● mmap  ● Redis
      │
    8 │  ● Page Cache  ● App Cache
      │
    7 │
      │
    6 │
      │
    5 │
      └──────────────────────────── Fault Tolerance
      0  1  2  3  4  5  6  7  8  9 10
         ● tmpfs
               ● Zero-Copy
                    ● App Cache
                         ● mmap
                              ● Page Cache
                                   ● Redis
                                        ● PMEM
```

---

## REKOMENDASI BERDASARKAN KEBUTUHAN

| Kebutuhan | Rekomendasi | Alasan |
|-----------|-------------|---------|
| **Max Speed, Data Boleh Hilang** | tmpfs atau Zero-Copy | 10/10 speed, fault tolerance rendah |
| **Speed Tinggi + Persistensi** | PMEM atau mmap + file | 9/9 atau 9/5 dengan backup |
| **Distributed Cache** | Redis | 9/8 dengan AOF |
| **Sederhana, File-based** | Page Cache | 8/7, balance |
| **IPC Cepat antar proses** | mmap atau tmpfs | 9/5 atau 10/1 |
| **Data Sensitif (tidak boleh hilang)** | PMEM atau Redis + AOF | 9/9 atau 9/8 |
| **Memory-limited** | App Cache | 8/4, sesuai kebutuhan |

---

## UNTUK TRADING SYSTEM ANDA

Berdasarkan kebutuhan trading system yang real-time:

### Pilihan Terbaik:
1. **Shared Memory (mmap)** - percobaan 3 (9/5)
2. **tmpfs + mmap** - Hybrid (10/1) tapi data hilang saat reboot
3. **tmpfs + file backup periodik** - Balance (10/7)

### Implementasi Hybrid (tmpfs + backup):
```cpp
// Data di tmpfs untuk speed
#define TMPFS_PATH "/dev/shm/trade_data.bin"

// Backup periodik ke disk untuk fault tolerance
void backup_to_disk() {
    // Copy dari tmpfs ke disk setiap 10 detik
    copy_file("/dev/shm/trade_data.bin", "./backup/trade_data.bin");
}
```

### Arsitektur yang Disarankan:
```
[Trading Engine]
       │
       ▼
[tmpfs: /dev/shm/trade_data.bin] ← Baca/tulis ultra-cepat
       │
       │ (backup async setiap N detik)
       ▼
[Backup Disk: ./backup/] ← Layer persistensi
       │
       │ (replikasi opsional)
       ▼
[Redis Cluster] ← Cache terdistribusi/failover
```

> 💡 **Tip:** Untuk sistem trading yang mission-critical, kombinasikan **tmpfs untuk kecepatan** + **backup async periodik** + **Redis untuk replikasi** untuk mencapai kecepatan ~10/10 dengan fault tolerance ~8/10.

---

*Catatan: Semua skor adalah estimasi relatif berdasarkan workload tipikal. Performa aktual dapat bervariasi tergantung pada hardware, OS, dan detail implementasi spesifik.*
