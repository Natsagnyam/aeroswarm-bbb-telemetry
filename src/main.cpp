
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <cmath>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <fstream>

#include "telemetry_packet.h"

#define PORT 8765
#define GROUND_STATION_IP "192.168.7.1"

static volatile uint32_t* gpio1_datain = nullptr;

bool initDirectMMIO() {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        std::perror("[MMIO Error] Failed to open /dev/mem");
        return false;
    }

    size_t page_size = sysconf(_SC_PAGE_SIZE);

    // 1. Unlocks P9_12 pinmux control register (0x44E10878)
    void* ctrl_base = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x44E10000);
    if (ctrl_base != MAP_FAILED) {
        volatile uint32_t* pin_reg = (volatile uint32_t*)((char*)ctrl_base + 0x878);
        *pin_reg = 0x37; // Mode 7 + RXACTIVE
        munmap(ctrl_base, page_size);
    }

    // 2. Map GPIO1 module base (0x4804C000)
    void* gpio1_base = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x4804C000);
    close(fd);

    if (gpio1_base == MAP_FAILED) {
        std::perror("[MMIO Error] GPIO1 module mmap failed");
        return false;
    }

    volatile uint32_t* gpio1_oe = (volatile uint32_t*)((char*)gpio1_base + 0x134);
    *gpio1_oe |= (1 << 28); // Set pin 28 as input

    gpio1_datain = (volatile uint32_t*)((char*)gpio1_base + 0x138);
    std::cout << "[MMIO Setup] High-speed hardware pin mapping initialized.\n";
    return true;
}

bool readPushButtonMMIO() {
    if (!gpio1_datain) return false;
    return ((*gpio1_datain & (1 << 28)) == 0); // Active low when grounded
}

// Zero-allocation CPU load reader from /proc/stat
uint8_t getSystemCpuLoad() {
    static unsigned long long prev_user = 0, prev_nice = 0, prev_system = 0, prev_idle = 0;
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0;

    std::ifstream stat_file("/proc/stat");
    if (!stat_file.is_open()) return 25;

    char buffer[128];
    stat_file.getline(buffer, sizeof(buffer));
    stat_file.close();

    // Parse "cpu  user nice system idle iowait irq softirq..."
    sscanf(buffer, "cpu %llu %llu %llu %llu %llu %llu %llu", 
           &user, &nice, &system, &idle, &iowait, &irq, &softirq);

    unsigned long long active_prev = prev_user + prev_nice + prev_system;
    unsigned long long total_prev = active_prev + prev_idle;

    unsigned long long active_curr = user + nice + system;
    unsigned long long total_curr = active_curr + idle;

    unsigned long long total_delta = total_curr - total_prev;
    unsigned long long active_delta = active_curr - active_prev;

    prev_user = user; prev_nice = nice; prev_system = system; prev_idle = idle;

    if (total_delta == 0) return 25;
    int cpu_load = static_cast<int>((active_delta * 100) / total_delta);
    if (cpu_load < 0) cpu_load = 0;
    if (cpu_load > 100) cpu_load = 100;

    return static_cast<uint8_t>(cpu_load);
}

int main() {
    std::cout << "[BBB Flight Computer] Initializing Dynamic Telemetry Engine...\n";
    
    if (!initDirectMMIO()) {
        std::cerr << "[Critical] Failed to initialize hardware MMIO.\n";
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, GROUND_STATION_IP, &dest_addr.sin_addr);

    uint16_t seq_count = 0;
    auto start_time = std::chrono::steady_clock::now();

    std::cout << "[BBB Flight Computer] Live UDP Telemetry Stream Running...\n";

    while (true) {
        SpaceTelemetryPacket pkt{};
        pkt.header.packet_id = htons(0x0810); 
        pkt.header.sequence_ctrl = htons(seq_count++ & 0x3FFF);
        pkt.header.packet_length = htons(sizeof(FlightTelemetryPayload) - 1);

        auto now = std::chrono::steady_clock::now();
        pkt.payload.timestamp_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());

        double elapsed_sec = std::chrono::duration<double>(now - start_time).count();

        // 1. Check physical hardware override switch on P9_12
        bool is_safe_mode = readPushButtonMMIO();

        if (is_safe_mode) {
            pkt.payload.flight_state = 3; // SAFE_MODE_NATSKA
            std::cout << "[HARDWARE ALERT] P9_12 Pulled LOW! Forcing SAFE_MODE_NATSKA. [Seq: " << seq_count << "]\n";
        } else {
            // Cycle flight states dynamically: 0 (NOMINAL), 1 (TRACKING_TARGET), 2 (EVASIVE_MANEUVER)
            pkt.payload.flight_state = static_cast<uint8_t>((seq_count / 20) % 3);
        }

        // 2. Dynamic Threat Distance (Simulates smooth radar proximity wave between 15m and 85m)
        pkt.payload.threat_distance_m = static_cast<float>(50.0 + 35.0 * std::sin(elapsed_sec * 0.8));

        // 3. Real System CPU Load parsed from /proc/stat
        pkt.payload.cpu_load_percent = getSystemCpuLoad();

        // 4. Realistic Battery Bus Voltage (Subtle oscillation around 23.8V with slight sag)
        pkt.payload.battery_voltage = static_cast<float>(23.8 - 0.2 * std::sin(elapsed_sec * 0.2));

        sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return 0;
}
