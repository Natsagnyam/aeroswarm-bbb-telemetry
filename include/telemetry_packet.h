#ifndef TELEMETRY_PACKET_H
#define TELEMETRY_PACKET_H

#include <cstdint>

#pragma pack(push, 1)

// CCSDS-inspired Space Packet Primary Header (6 bytes)
struct SpacePacketHeader {
    uint16_t packet_id;      // Version (3 bits), Type (1 bit), Secondary Header Flag (1 bit), APID (11 bits)
    uint16_t sequence_ctrl;  // Grouping Flags (2 bits), Sequence Count (14 bits)
    uint16_t packet_length;  // Total packet length minus 6 bytes
};

// Application Telemetry Payload
struct FlightTelemetryPayload {
    uint32_t timestamp_ms;
    uint8_t  flight_state;       // 0: NOMINAL, 1: TRACKING, 2: EVASIVE, 3: SAFE
    float    threat_distance_m;
    uint8_t  cpu_load_percent;
    float    battery_voltage;
};

// Combined Frame
struct SpaceTelemetryPacket {
    SpacePacketHeader header;
    FlightTelemetryPayload payload;
};

#pragma pack(pop)

#endif // TELEMETRY_PACKET_H
