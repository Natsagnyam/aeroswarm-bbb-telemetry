import asyncio
import websockets
import socket
import struct
import json

# UDP_IP = "127.0.0.1"
UDP_IP = "0.0.0.0"
UDP_PORT = 8765
WS_IP = "127.0.0.1"
WS_PORT = 8766

connected_clients = set()
STATES = ["NOMINAL", "TRACKING_TARGET", "EVASIVE_MANEUVER", "SAFE_MODE_NATSKA"]

async def websocket_handler(websocket):
    connected_clients.add(websocket)
    print(f"[WebSocket] Client connected: {websocket.remote_address}")
    try:
        await websocket.wait_closed()
    finally:
        connected_clients.remove(websocket)

async def udp_receiver(queue):
    loop = asyncio.get_running_loop()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    sock.setblocking(False)

    print(f"[UDP Bridge] Listening on {UDP_IP}:{UDP_PORT}...")

    while True:
        try:
            data = await loop.sock_recv(sock, 1024)
            if len(data) == 20: # 6 header + 14 payload bytes
                # CCSDS Header (Big-Endian !HHH)
                pkt_id, seq_ctrl, pkt_len = struct.unpack("!HHH", data[:6])
                # Payload (Little-Endian <IBfBf)
                ts, state_idx, threat_dist, cpu, voltage = struct.unpack("<IBfBf", data[6:])
                
                seq_num = seq_ctrl & 0x3FFF
                state_str = STATES[state_idx] if state_idx < len(STATES) else "UNKNOWN"

                telemetry_json = {
                    "flight_state": state_str,
                    "threat_distance_meters": round(threat_dist, 2),
                    "cpu_load_percent": cpu,
                    "battery_voltage": round(voltage, 2),
                    "sequence_number": seq_num,
                    "timestamp_ms": ts
                }

                await queue.put(json.dumps(telemetry_json))
        except Exception as e:
            await asyncio.sleep(0.01)

async def broadcast_manager(queue):
    while True:
        message = await queue.get()
        if connected_clients:
            await asyncio.gather(
                *[client.send(message) for client in connected_clients],
                return_exceptions=True
            )

async def main():
    queue = asyncio.Queue()
    ws_server = await websockets.serve(websocket_handler, WS_IP, WS_PORT)
    print(f"[WebSocket] Bridge listening on ws://{WS_IP}:{WS_PORT}")

    await asyncio.gather(
        udp_receiver(queue),
        broadcast_manager(queue),
        ws_server.wait_closed()
    )

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
