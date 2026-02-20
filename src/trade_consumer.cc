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
// #include <vector>
// #include <utility>
#include <random> 
#include <sstream>
#include <chrono>
#include <iomanip>
#include <atomic>
#include <csignal>
// #include <filesystem>
#include <thread>

#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

#define ENGINE_UPDATE_INTERVAL_IN_SECOND 1 // in second

#define ENGINE_BIZS_SHARED_MEM_NAME "/shared_bizs_mem_data"

// --------------------------------------------------------- //

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

// --------------------------------------------------------- //
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

static std::atomic<bool> _consumer_is_running(true);

void signal_handler(int signum) {
    if (signum == 2) { // ctrl+c
        std::cout << "\nSIG " << signum << ": shutting down gracefully at "
                  << timestamp() << "\n"; // maybe log exit time for record
        _consumer_is_running = false;
    }
}

// --------------------------------------------------------- //
// --------------------------------------------------------- //

class EngineConsumer_c {
    // file descriptor
    int _fd;
    size_t _fd_size;
    void* _mapped = MAP_FAILED;
    SharedBizsData_t* _p_shared_bizs_data;

    // --------------------------------------------------------- //

    void _clear_screen_lines(int n_lines) {
        for (int i = 0; i < n_lines; i++) {
            // clear entire line, move up one line
            std::cout << "\033[A\033[2K";
        }
    }

    void _update_display() {
        if (!_p_shared_bizs_data) return;

        int current_count = _p_shared_bizs_data->count.load();

        if (current_count <= 0) {
            std::cout << "NOTE: no biz data available";
            return;
        }

        int total_lines = 1; // header line
        // calculate total lines needed for display
        for (int i = 0; i < current_count; i++) {
            total_lines += 6;
        }
        // final separator
        total_lines += 1;

        // clear previous output, except first run
        static bool first_run = true;
        if (!first_run) {
            _clear_screen_lines(total_lines);
        } else {
            first_run = false;
        }

        // header display
        std::cout << "TIME                : " << timestamp() << "\n";

        // biz display
        for (int i = 0; i < current_count; i++) {
            const auto& biz = _p_shared_bizs_data->bizs[i];
            std::cout << "#=================================================#\n";
            std::cout << "id                  : " << biz.id << "\n";
            std::cout << "name                : " << biz.name << "\n";
            std::cout << "total stock         : " << std::fixed << std::setprecision(2)
                                                  << biz.total_stock << "\n";
            std::cout << "current stock /share: " << std::fixed << std::setprecision(2)
                                                  << biz.current_stock << "\n";
            std::cout << "#-------------------------------------------------#\n";
        }

        // immediate display
        std::cout.flush();
    }

public:
    EngineConsumer_c() : _fd(-1)
                       , _fd_size(sizeof(SharedBizsData_t))
                       , _mapped(MAP_FAILED)
                       , _p_shared_bizs_data(nullptr) {};
    ~EngineConsumer_c() {
        if (_mapped != MAP_FAILED) {
            munmap(_mapped, _fd_size);
            _mapped = MAP_FAILED;
            _p_shared_bizs_data = nullptr;
        }
    };

    // --------------------------------------------------------- //

    bool initialize() {
        // TODO: open existing shared memory
        _fd = shm_open(ENGINE_BIZS_SHARED_MEM_NAME, O_RDONLY, 0666);

        if (_fd == -1) {
            std::cerr << "ERROR: shm_open failed, check if main engine is running\n";
            return false;
        }

        _mapped = mmap(nullptr, _fd_size, PROT_READ, MAP_SHARED, _fd, 0);
        close(_fd);

        if (_mapped == MAP_FAILED) {
            std::cerr << "ERROR: mmap failed\n";
            return false;
        }

        _p_shared_bizs_data = static_cast<SharedBizsData_t*>(_mapped);

        return true;
    }

    void run() {
        for (;;) {
            _update_display();

            // update for each 1 second
            std::this_thread::sleep_for(
                std::chrono::seconds(ENGINE_UPDATE_INTERVAL_IN_SECOND)
            );

            // exit the consumer engine
            if (!_consumer_is_running) { break; }
        }
    }
}; // EngineConsumer_c

// --------------------------------------------------------- //
// --------------------------------------------------------- //

int main() {
    std::signal(SIGINT, signal_handler);
    std::cout << "RUN: trading_sys - trader_consumer\n";

    // NOTE:
    // don't call shm_unlink()
    // unless you want to destroy that shared mem
    EngineConsumer_c* p_consumer = new EngineConsumer_c();

    if (!p_consumer->initialize()) {
        return -1;
    }

    p_consumer->run();

    delete p_consumer;

    return 0;
}
