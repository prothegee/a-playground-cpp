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

#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

#define ENGINE_UPDATE_INTERVAL_IN_SECOND 1 // in second

#define ENGINE_BIZS_SHARED_MEM_NAME "/shared_bizs_mem_data"

// --------------------------------------------------------- //

// filesystem namespace
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
    int tz = tz_offset * 3600; // adjust with seconds hour
    std::string s;
    std::stringstream ss;

    // utc timezone is from -12 to 14
    if (tz <= -12) { tz = -12; }
    if (tz <= 14) { tz = 14; }

    auto now = std::chrono::system_clock::now();

    std::time_t now_time = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
    std::time_t now_time_utc = now_time + tz;

    tm tm_buf;

    gmtime_r(&now_time_utc, &tm_buf); // unix compatible

    auto now_ns = std::chrono::time_point_cast<std::chrono::nanoseconds>(now);
    auto nanoseconds = now_ns.time_since_epoch() % std::chrono::seconds(1);

    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(9)
           << std::setfill('0') << nanoseconds.count();

    return ss.str();
}

// TODO: data_writer_csv

// TODO: timestamp_naming_marker(const int& tz_offset = 0) {}

// --------------------------------------------------------- //
// --------------------------------------------------------- //

typedef struct _BizTransaction_t {
    double value;
    std::string date_and_time;
} BizTransaction_t;

typedef struct _BizConfig_t {
    std::pair<double, double> min_max; // 1st is min, 2nd is max; just for holder
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

// @note this is shared per data object
typedef struct _SharedBizData_t {
    int id;
    char name[512];
    double current_stock;
    double total_stock;
} SharedBizData_t;

constexpr int _MAX_BIZ_SIZE = 32;
// @note array/vector data from SharedBizData_t
typedef struct _SharedBizsData_t {
    std::atomic<int> count;
    SharedBizData_t bizs[_MAX_BIZ_SIZE]; // some alocated memory need to be initialize at compile time
} SharedBizsData_t;
// typedef std::vector<SharedBizData_t> SharedBizsData_t;

// --------------------------------------------------------- //
// --------------------------------------------------------- //

static std::atomic<bool> _engine_is_running(true);

void signal_handler(int signum) {
    if (signum == 2) { // ctrl+c
        std::cout << "\nSIG " << signum << ": shutting down gracefully at "
                  << timestamp() << "\n"; // maybe log exit time for record
        _engine_is_running = false;
    }
}

// --------------------------------------------------------- //
// --------------------------------------------------------- //

// default engine enum
enum EngineInitStatus_e : int {
    ENGINE_INIT_OK = 0,
    ENGINE_INIT_BIZS_EMPTY = 1
};

class EngineSystem_c {
    std::vector<Biz_t*> *_p_bizs;

    SharedBizsData_t* _p_shared_bizs_data;

    bool _shared_bizs_memory_ready;

    // --------------------------------------------------------- //

    bool _shared_memory_init() {
        // file descriptor
        int fd = shm_open(ENGINE_BIZS_SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
        size_t fd_size = sizeof(SharedBizsData_t);

        if (fd == -1) { return false; }

        // create the correct size of shared mem data
        ftruncate(fd, fd_size);

        _p_shared_bizs_data = static_cast<SharedBizsData_t*>(
            mmap(nullptr,
                sizeof(SharedBizsData_t),
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                fd,
                0
            )
        );

        _shared_bizs_memory_ready = (_p_shared_bizs_data != MAP_FAILED);

        if (_p_shared_bizs_data) {
            _p_shared_bizs_data->count = 0;
        }

        close(fd);

        return _shared_bizs_memory_ready;
    }

    void _shared_memory_update() {
        // don't update if biz mem data is null or it's not ready
        // map shared mem only once at startup
        // but this section only update when available/ready
        if (!_shared_bizs_memory_ready || !_p_shared_bizs_data) return;

        // settingup for shared mem update
        int count = std::min(static_cast<int>(
            _p_bizs->size()),
            _MAX_BIZ_SIZE
        );
        _p_shared_bizs_data->count = count;

        // iter and write to mem
        for (int i = 0; i < count; i++) {
            Biz_t* p_biz = (*_p_bizs)[i];
            auto& mem_dest = _p_shared_bizs_data->bizs[i];

            mem_dest.id = p_biz->id;
            mem_dest.current_stock = p_biz->current_stock;
            mem_dest.total_stock = p_biz->total_stock;
            // skipped: config
            // skipped: data dir
            // name:
            // - don't leak it
            // - need to be use safe copy for primitive type
            strncpy(mem_dest.name, p_biz->name.c_str(),
                    sizeof(mem_dest.name) - 1);
            mem_dest.name[sizeof(mem_dest.name) - 1] = '\0';
        }
    }

    // --------------------------------------------------------- //

    // @brief will process stock value
    //
    // @note use thread
    void _dummy_runtime() {
        // use array thread?
        std::vector<std::thread> threads;

        // do adjustment stock for each biz
        // - assume receive data from somewhere
        // - use multiple threads that available
        for (auto* p_biz : *_p_bizs) {
            threads.emplace_back([p_biz]() {
                double updated_value = 0.00, stock_pershare = 0.00;
                int increase_or_decrease = random_round_num(0, 100); // 2/100

                switch (increase_or_decrease) {
                    case 0: 
                        // decrease stock
                        updated_value = random_decimal_num(
                            p_biz->config.decrease_range.first,
                            p_biz->config.decrease_range.second
                        );

                        stock_pershare = random_decimal_num(100'000.00, 300'000.00);

                        p_biz->total_stock -= updated_value;
                        p_biz->current_stock = stock_pershare;

                        // // min prevention
                        // // just because minus value is crazy
                        // if (p_biz->current_stock <= p_biz->config.min_max.first) {
                        //     p_biz->current_stock = p_biz->config.min_max.first;
                        // }
                    break;

                    case 1:
                        // increase stock
                        updated_value = random_decimal_num(
                            p_biz->config.increase_range.first,
                            p_biz->config.increase_range.second
                        );

                        stock_pershare = random_decimal_num(100'000.00, 300'000.00);

                        p_biz->total_stock += p_biz->current_stock;
                        p_biz->current_stock = stock_pershare;

                        // // max prevention
                        // // just because minus value is crazy
                        // if (p_biz->current_stock >= p_biz->config.min_max.second) {
                        //     p_biz->current_stock = p_biz->config.min_max.second;
                        // }
                    break;

                    default:
                        // nothing todo
                        // unless has do something to prevent
                    break;
                }
            });
        }

        // wait to all threads task completed
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }

        _shared_memory_update();

        // // update for each 1 second
        // std::this_thread::sleep_for(
        //         std::chrono::seconds(ENGINE_UPDATE_INTERVAL_IN_SECOND));
    }

    void _run() {
        // int tmp_limiter = 100;
        for (;;) {
            _dummy_runtime();

            // exit the engine
            if (!_engine_is_running) { break; }
        }
    }

public:
    EngineSystem_c() : _p_bizs(new std::vector<Biz_t*>())
                     , _p_shared_bizs_data(nullptr)
                     , _shared_bizs_memory_ready(false) {
    };
    ~EngineSystem_c() {
        if (_p_shared_bizs_data && _shared_bizs_memory_ready) {
            munmap(_p_shared_bizs_data, sizeof(SharedBizsData_t));
        }

        if (_p_bizs || !_p_bizs->empty()) {
            for (auto* p_biz : *_p_bizs) {
                if (p_biz) { delete p_biz; }
            }
            delete _p_bizs; 
        }
    };

    // --------------------------------------------------------- //

    // could: do some load config

    EngineInitStatus_e initialize_and_run(int& exit_cb) {
        if (!_p_bizs || _p_bizs->empty()) {
            std::cerr << "ERROR: biz entity is empty or fail to load\n";
            exit_cb = static_cast<int>(ENGINE_INIT_BIZS_EMPTY);
            return ENGINE_INIT_BIZS_EMPTY;
        }

        // create .cache dir in here
        _fs::path dir = _fs::current_path() / ".cache";
        if (!_fs::exists(dir)) {
            _fs::create_directories(dir);
        }

        // kinda naive approach to init shared mem
        if (!_shared_memory_init()) {
            std::cout << "WARN: something not right when mem is init\n";
            // could be break or continue
        }

        _run();

        return ENGINE_INIT_OK;
    }

    // could be read config engine from file

    // --------------------------------------------------------- //

    int add_biz_entity(Biz_t* p_biz) {
        if (!p_biz) {
            std::cerr << "biz entity is null\n";
            return -1;
        }

        // create data/{BIZ_NAME} dir
        _fs::path dir = _fs::current_path() / "data" / p_biz->name; 
        if (!_fs::exists(dir)) {
            _fs::create_directories(dir);
        }

        p_biz->data_dir = dir.string();

        // store it to engine container
        _p_bizs->emplace_back(p_biz);

        return 1; // 1 as true
    }
};

// RESERVED

// --------------------------------------------------------- //
// --------------------------------------------------------- //

int main() {
    std::signal(SIGINT, signal_handler);
    std::cout << "RUN: trading_sys - trader_engine\n";

    int status = 0;

    EngineSystem_c *p_engine = new EngineSystem_c();

    auto starter_value = random_decimal_num(100'000.00, 300'000.00);
    std::vector<Biz_t*> bizs = {
        new Biz_t{
            .id = 1,
            .name = "BIZ1",
            .current_stock = random_decimal_num(30.00, 90.00),
            .total_stock = starter_value,
            .config = BizConfig_t{
                .min_max = { 300.00, 3'000'000.00 },
                .decrease_range = { 15.00, 30.00 },
                .increase_range = { 90.00, 180.00 }
            }
        },
        new Biz_t{
            .id = 2,
            .name = "BIZ2",
            .current_stock = random_decimal_num(30.00, 90.00),
            .total_stock = starter_value,
            .config = BizConfig_t{
                .min_max = { 600.00, 6'000'000.00 },
                .decrease_range = { 15.00, 30.00 },
                .increase_range = { 90.00, 180.00 }
            }
        },
        new Biz_t{
            .id = 3,
            .name = "BIZ3",
            .current_stock = random_decimal_num(30.00, 90.00),
            .total_stock = starter_value,
            .config = BizConfig_t{
                .min_max = { 900.00, 9'000'000.00 },
                .decrease_range = { 15.00, 30.00 },
                .increase_range = { 90.00, 180.00 }
            }
        }
    };
    for (auto* p_biz : bizs) {
        p_engine->add_biz_entity(p_biz);
    }
    bizs.clear(); // now empty

    p_engine->initialize_and_run(status);

    delete p_engine;

    return status;
}

