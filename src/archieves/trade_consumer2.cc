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
#include <random> 
#include <sstream>
#include <chrono>
#include <iomanip>
#include <atomic>
#include <csignal>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

#define ENGINE_UPDATE_INTERVAL_IN_SECOND 1 // in second

// File path where engine writes data
#define ENGINE_BIZS_FILE_PATH "./data/trade_data.bin"

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
// Data structures must match those used by engine
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

static std::atomic<bool> _consumer_is_running(true);

void signal_handler(int signum) {
    if (signum == 2) { // Ctrl+C
        std::cout << "\nSIG " << signum << ": shutting down gracefully at "
                  << timestamp() << "\n";
        _consumer_is_running = false;
    }
}

// --------------------------------------------------------- //
// --------------------------------------------------------- //

class EngineConsumer_c {
    FileBizsData_t _file_data;  // local copy of last read data

    void _clear_screen_lines(int n_lines) {
        for (int i = 0; i < n_lines; i++) {
            std::cout << "\033[A\033[2K";
        }
    }

    bool _read_file() {
        int fd = open(ENGINE_BIZS_FILE_PATH, O_RDONLY);
        if (fd == -1) {
            return false;  // file not yet created
        }

        FileBizsData_t tmp;
        ssize_t n = read(fd, &tmp, sizeof(tmp));
        close(fd);

        if (n != sizeof(tmp)) {
            return false;  // incomplete read
        }

        _file_data = tmp;
        return true;
    }

    void _update_display() {
        if (!_read_file()) {
            std::cout << "NOTE: no data available yet (waiting for engine)\n";
            return;
        }

        int current_count = _file_data.count;
        if (current_count <= 0) {
            std::cout << "NOTE: no biz data available\n";
            return;
        }

        // Calculate total lines for clearing
        int total_lines = 1; // header
        for (int i = 0; i < current_count; i++) {
            total_lines += 6;
        }
        total_lines += 1; // final separator

        static bool first_run = true;
        if (!first_run) {
            _clear_screen_lines(total_lines);
        } else {
            first_run = false;
        }

        std::cout << "TIME                : " << timestamp() << "\n";

        for (int i = 0; i < current_count; i++) {
            const auto& biz = _file_data.bizs[i];
            std::cout << "#=================================================#\n";
            std::cout << "id                  : " << biz.id << "\n";
            std::cout << "name                : " << biz.name << "\n";
            std::cout << "total stock         : " << std::fixed << std::setprecision(2)
                      << biz.total_stock << "\n";
            std::cout << "current stock /share: " << std::fixed << std::setprecision(2)
                      << biz.current_stock << "\n";
            std::cout << "#-------------------------------------------------#\n";
        }

        std::cout.flush();
    }

public:
    EngineConsumer_c() : _file_data{} {}

    bool initialize() {
        // Nothing special to initialize, file will be opened on each read
        return true;
    }

    void run() {
        while (_consumer_is_running) {
            _update_display();
            std::this_thread::sleep_for(
                std::chrono::seconds(ENGINE_UPDATE_INTERVAL_IN_SECOND));
        }
    }
};

// --------------------------------------------------------- //
// --------------------------------------------------------- //

int main() {
    std::signal(SIGINT, signal_handler);
    std::cout << "RUN: trading_sys - trader_consumer (file-based I/O)\n";

    EngineConsumer_c consumer;
    if (!consumer.initialize()) {
        return -1;
    }
    consumer.run();

    return 0;
}
