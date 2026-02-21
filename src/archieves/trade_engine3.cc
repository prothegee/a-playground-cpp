/*
0 None
1 Execute
2 Write
3 Write & Execute
4 Read
5 Read & Execute
6 Read & Write
7 Read, Write, and Execute

BIT OWNER GROUP OTHERS
0   6     0     0

BIT mostly mean setuid, setgid, or sticky bit
*/

#include <iostream>
#include <string>
#include <vector>
#include <utility>
#include <random> 
#include <sstream>
#include <chrono>
#include <iomanip>
#include <atomic>
#include <csignal>
#include <filesystem>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>

#define ENGINE_UPDATE_INTERVAL_IN_SECOND 1 // in second

// TMPFS paths (in RAM)
#define ENGINE_BIZS_FILE_PATH "./data/trade_data.bin"
#define ENGINE_BIZS_TEMP_FILE_PATH "./data/trade_data.tmp"

// --------------------------------------------------------- //

namespace _fs = std::filesystem;

static std::random_device _rand;
static std::default_random_engine _rande(_rand());

int random_round_num(const int& min, const int& max) {
    std::uniform_int_distribution<int> eval(min, max);
    return eval(_rande);
}

double random_decimal_num(const double& min, const double& max) {
    std::uniform_real_distribution<double> eval(min, max);
    return eval(_rande);
}

int find_and_replace_all(std::string& source,
                         const std::string& query,
                         const std::string& replacement) {
    try {
        size_t position = 0;
        while ((position = source.find(query, position)) != std::string::npos) {
            source.replace(position, query.size(), replacement);
            position += replacement.size();
        }
        return 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return -1;
    }
}

std::string timestamp(const int& tz_offset = 0) {
    int tz = tz_offset * 3600;
    std::stringstream ss;

    if (tz <= -12) { tz = -12; }
    if (tz >= 14) { tz = 14; }

    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::time_t now_time_utc = now_time + tz;

    tm tm_buf;
    gmtime_r(&now_time_utc, &tm_buf);

    auto now_ns = std::chrono::time_point_cast<std::chrono::nanoseconds>(now);
    auto nanoseconds = now_ns.time_since_epoch() % std::chrono::seconds(1);

    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(9)
       << std::setfill('0') << nanoseconds.count();
    return ss.str();
}

// --------------------------------------------------------- //
// --------------------------------------------------------- //

typedef struct _BizTransaction_t {
    double value;
    std::string date_and_time;
} BizTransaction_t;

typedef struct _BizConfig_t {
    std::pair<double, double> min_max;
    std::pair<double, double> decrease_range;
    std::pair<double, double> increase_range;
} BizConfig_t;

typedef struct _Biz_t {
    int id;
    std::string name;
    double current_stock;
    double total_stock;
    BizConfig_t config;
    std::string data_dir;
} Biz_t;

// --------------------------------------------------------- //
// Data structure for file I/O (plain, no atomics)
constexpr int _MAX_BIZ_SIZE = 32;

struct FileBizData_t {
    int id;
    char name[512];
    double current_stock;
    double total_stock;
};

struct FileBizsData_t {
    int count;
    FileBizData_t bizs[_MAX_BIZ_SIZE];
};

// --------------------------------------------------------- //
// --------------------------------------------------------- //

static std::atomic<bool> _engine_is_running(true);

void signal_handler(int signum) {
    if (signum == 2) { // Ctrl+C
        std::cout << "\nSIG " << signum << ": shutting down gracefully at "
                  << timestamp() << "\n";
        _engine_is_running = false;
    }
}

// --------------------------------------------------------- //
// --------------------------------------------------------- //

enum EngineInitStatus_e : int {
    ENGINE_INIT_OK = 0,
    ENGINE_INIT_BIZS_EMPTY = 1
};

class EngineSystem_c {
    std::vector<Biz_t*> *_p_bizs;

    // --------------------------------------------------------- //
    // Write current data to tmpfs file atomically
    void _file_update() {
        // Cek apakah /dev/shm ada dan bisa ditulis
        struct stat st;
        if (stat("/dev/shm", &st) != 0) {
            std::cerr << "ERROR: /dev/shm not available! Make sure tmpfs is mounted.\n";
            return;
        }
        
        int fd = open(ENGINE_BIZS_TEMP_FILE_PATH,
                      O_WRONLY | O_CREAT | O_TRUNC, 0666);
        
        if (fd == -1) {
            std::cerr << "ERROR: cannot open temp file in tmpfs: " 
                      << strerror(errno) << std::endl;
            return;
        }
        
        FileBizsData_t file_data;
        int count = std::min(static_cast<int>(_p_bizs->size()), _MAX_BIZ_SIZE);
        file_data.count = count;
        
        for (int i = 0; i < count; i++) {
            Biz_t* p_biz = (*_p_bizs)[i];
            auto& dest = file_data.bizs[i];
            dest.id = p_biz->id;
            dest.current_stock = p_biz->current_stock;
            dest.total_stock = p_biz->total_stock;
            strncpy(dest.name, p_biz->name.c_str(), sizeof(dest.name) - 1);
            dest.name[sizeof(dest.name) - 1] = '\0';
        }
        
        ssize_t written = write(fd, &file_data, sizeof(file_data));
        
        if (written != sizeof(file_data)) {
            std::cerr << "ERROR: write to tmpfs failed: " 
                      << strerror(errno) << std::endl;
            close(fd);
            return;
        }
        
        // fsync tidak terlalu diperlukan di tmpfs (RAM), 
        // tapi tetap dipanggil untuk konsistensi
        fsync(fd);
        close(fd);
        
        if (rename(ENGINE_BIZS_TEMP_FILE_PATH, ENGINE_BIZS_FILE_PATH) != 0) {
            std::cerr << "ERROR: rename in tmpfs failed: " 
                      << strerror(errno) << std::endl;
        }
        
        // Optional: Beri hint ke kernel bahwa file akan sering dibaca
        int data_fd = open(ENGINE_BIZS_FILE_PATH, O_RDONLY);
        if (data_fd != -1) {
            posix_fadvise(data_fd, 0, 0, POSIX_FADV_WILLNEED);
            close(data_fd);
        }
    }

    // --------------------------------------------------------- //
    void _dummy_runtime() {
        std::vector<std::thread> threads;

        for (auto* p_biz : *_p_bizs) {
            threads.emplace_back([p_biz]() {
                double updated_value = 0.00, stock_pershare = 0.00;
                int action = random_round_num(0, 100); // 0: decrease, 1: increase, else: none

                switch (action) {
                    case 0: // decrease
                        updated_value = random_decimal_num(
                            p_biz->config.decrease_range.first,
                            p_biz->config.decrease_range.second
                        );
                        stock_pershare = random_decimal_num(100'000.00, 300'000.00);
                        p_biz->total_stock -= updated_value;
                        p_biz->current_stock = stock_pershare;
                        break;

                    case 1: // increase
                        updated_value = random_decimal_num(
                            p_biz->config.increase_range.first,
                            p_biz->config.increase_range.second
                        );
                        stock_pershare = random_decimal_num(100'000.00, 300'000.00);
                        p_biz->total_stock += p_biz->current_stock;
                        p_biz->current_stock = stock_pershare;
                        break;

                    default:
                        break;
                }
            });
        }

        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }

        _file_update();  // write updated data to tmpfs
    }

    void _run() {
        while (_engine_is_running) {
            _dummy_runtime();
            std::this_thread::sleep_for(
                std::chrono::seconds(ENGINE_UPDATE_INTERVAL_IN_SECOND));
        }
    }

public:
    EngineSystem_c() : _p_bizs(new std::vector<Biz_t*>()) {}
    ~EngineSystem_c() {
        if (_p_bizs) {
            for (auto* p_biz : *_p_bizs) delete p_biz;
            delete _p_bizs;
        }
        
        // Bersihkan file di tmpfs saat engine mati
        unlink(ENGINE_BIZS_FILE_PATH);
        unlink(ENGINE_BIZS_TEMP_FILE_PATH);
    }

    // --------------------------------------------------------- //

    EngineInitStatus_e initialize_and_run(int& exit_cb) {
        if (!_p_bizs || _p_bizs->empty()) {
            std::cerr << "ERROR: biz entity is empty\n";
            exit_cb = static_cast<int>(ENGINE_INIT_BIZS_EMPTY);
            return ENGINE_INIT_BIZS_EMPTY;
        }

        // Verifikasi tmpfs tersedia
        struct stat st;
        if (stat("/dev/shm", &st) != 0) {
            std::cerr << "ERROR: /dev/shm not found! Please ensure tmpfs is mounted.\n";
            std::cerr << "Try: sudo mount -t tmpfs tmpfs /dev/shm\n";
            exit_cb = -1;
            return ENGINE_INIT_BIZS_EMPTY;
        }

        std::cout << "INFO: Using tmpfs at /dev/shm for fast I/O\n";
        std::cout << "INFO: Data files: " << ENGINE_BIZS_FILE_PATH << "\n";

        _run();
        return ENGINE_INIT_OK;
    }

    int add_biz_entity(Biz_t* p_biz) {
        if (!p_biz) return -1;

        // Buat folder data di disk untuk persistensi (optional)
        _fs::path dir = _fs::current_path() / "data" / p_biz->name;
        if (!_fs::exists(dir)) {
            _fs::create_directories(dir);
        }
        p_biz->data_dir = dir.string();
        
        _p_bizs->emplace_back(p_biz);
        return 1;
    }
};

// --------------------------------------------------------- //
// --------------------------------------------------------- //

int main() {
    std::signal(SIGINT, signal_handler);
    std::cout << "RUN: trading_sys - trader_engine (TMPFS version)\n";
    std::cout << "================================================\n";

    int status = 0;
    EngineSystem_c *p_engine = new EngineSystem_c();

    auto starter_value = random_decimal_num(100'000.00, 300'000.00);
    std::vector<Biz_t*> bizs = {
        new Biz_t{1, "BIZ1", random_decimal_num(30.00, 90.00), starter_value,
                  { {300.00, 3'000'000.00}, {15.00, 30.00}, {90.00, 180.00} }, ""},
        new Biz_t{2, "BIZ2", random_decimal_num(30.00, 90.00), starter_value,
                  { {600.00, 6'000'000.00}, {15.00, 30.00}, {90.00, 180.00} }, ""},
        new Biz_t{3, "BIZ3", random_decimal_num(30.00, 90.00), starter_value,
                  { {900.00, 9'000'000.00}, {15.00, 30.00}, {90.00, 180.00} }, ""}
    };

    for (auto* p_biz : bizs) {
        p_engine->add_biz_entity(p_biz);
    }
    bizs.clear();

    p_engine->initialize_and_run(status);
    delete p_engine;

    std::cout << "Engine stopped. Cleanup complete.\n";
    return status;
}
