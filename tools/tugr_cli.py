import serial
import time
import argparse
import sys
import msvcrt
import json
import struct


# ==================== PACKET GENERATION ====================
def calc_checksum(data):
    return sum(data) & 0xFFFF


def generate_packet(type_id, source_id, dest_zone, order_id, line="", parts="", hop=0):
    # struct MeshPacket {
    #   uint8_t type;
    #   uint32_t sourceId;
    #   uint32_t destZone;
    #   uint32_t orderId;
    #   uint8_t hopCount;
    #   char line[28];
    #   char parts[60];
    #   uint16_t checksum;
    # };
    # Total Size calculation: 1+4+4+4+1+28+60+2 = 104 bytes

    # Pack without checksum first
    fmt = "<BIIIB28s60sH"
    # Note: Strings need to be encoded and padded
    line_b = line.encode("utf-8")
    parts_b = parts.encode("utf-8")

    # Init Checksum 0
    payload = struct.pack(
        "<BIIIB28s60s", type_id, source_id, dest_zone, order_id, hop, line_b, parts_b
    )

    # Calc Checksum
    chk = sum(payload) & 0xFFFF

    # Final Pack
    final_pkt = struct.pack(
        "<BIIIB28s60sH",
        type_id,
        source_id,
        dest_zone,
        order_id,
        hop,
        line_b,
        parts_b,
        chk,
    )

    return final_pkt.hex().upper()


# ==================== CLI LOGIC ====================


def send_command(ser, cmd, duration=0.5):
    # 1. Flush old data to prevent stale reads
    ser.reset_input_buffer()

    # 2. Send Command
    full_cmd = f"{cmd}\n"
    ser.write(full_cmd.encode("utf-8"))

    # 3. Read continuously for 'duration' seconds
    # This prevents buffer overflows and prints output immediately per step
    start_time = time.time()
    has_response = False

    while (time.time() - start_time) < duration:
        if ser.in_waiting:
            try:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                if line:
                    print(f"DEV: {line}")
                    has_response = True
            except:
                pass
        time.sleep(0.05)

    if not has_response:
        print("DEV: <NO RESPONSE>")


def interactive_mode(ser):
    print("--- INTERACTIVE MODE (FULL SYSTEM) ---")
    print("KEYS:")
    print("  [0-9]: Simulate Button Press (0=Mode, 1=Sel, 6=Clr, 7=Up, 8=Dn)")
    print("  [M]: Switch Mode (Cycle)")
    print("  [I]: Inject 'Order' Packet (Mock Line Station)")
    print("  [A]: Inject 'ACK' Packet (Mock Collector)")
    print("  [P]: Query State")
    print("  [Q]: Quit")

    while True:
        if msvcrt.kbhit():
            key = msvcrt.getch()

            if key.isdigit():
                btn = int(key)
                print(f">> PRESS BTN {btn}")
                send_command(ser, f"BTN:PRESS {btn}")
            elif key.lower() == b"m":
                print(">> MODE SWITCH")
                send_command(ser, "BTN:PRESS 0", wait_time=2.0)
            elif key.lower() == b"p":
                print(">> QUERY STATE")
                send_command(ser, "SYS:STATE")
            elif key.lower() == b"i":
                print(">> INJECTING ORDER...")
                oid = int(time.time())
                hex_data = generate_packet(1, 999, 1, oid, "MOCK LINE", "TEST PART")
                send_command(ser, f"RADIO:INJECT {hex_data}")
            elif key.lower() == b"a":
                print(">> INJECTING ACK...")
                oid = int(time.time())
                hex_data = generate_packet(2, 888, 1, oid)
                send_command(ser, f"RADIO:INJECT {hex_data}")
            elif key.lower() == b"q":
                break


def run_test(ser):
    print("========================================")
    print("   TUGR FULL SYSTEM VERIFICATION SUITE   ")
    print("========================================")

    # ==================== PHASE 1: LINE MODE ====================
    print("\n[PHASE 1] LINE STATION MODE VERIFICATION")

    print("1. Setting Mode to LINE STATION (Wait 5s for E-Ink refresh)...")
    send_command(ser, "MODE:SET 2", duration=5.0)

    print("2. Simulating Button Press (Create Order)...")
    send_command(ser, "BTN:PRESS 2", duration=5.0)

    print("3. Verifying Order Created...")
    send_command(ser, "SYS:STATE", duration=1.0)

    # ==================== PHASE 2: TUGGER MODE ====================
    print("\n[PHASE 2] TUGGER MODE VERIFICATION")

    print("1. Setting Mode to TUGGER (Wait 5s)...")
    send_command(ser, "MODE:SET 0", duration=5.0)

    print("2. Injecting Mock Order (Simulates Line Request)...")
    oid = 100001
    hex_data = generate_packet(1, 999, 1, oid, "TEST LINE", "TEST PART")
    send_command(ser, f"RADIO:INJECT {hex_data}", duration=5.0)

    print("3. Verifying Order Received (Check State)...")
    send_command(ser, "SYS:STATE", duration=1.0)

    print("4. Pressing SELECT to ACK (Simulate Driver Accept)...")
    send_command(ser, "BTN:PRESS 1", duration=5.0)

    print("5. Verifying ACK Status...")
    send_command(ser, "SYS:STATE", duration=1.0)

    # ==================== PHASE 3: COLLECTOR MODE ====================
    print("\n[PHASE 3] COLLECTOR MODE VERIFICATION")

    print("1. Setting Mode to COLLECTOR (Wait 5s)...")
    send_command(ser, "MODE:SET 1", duration=5.0)

    print("2. Injecting Mock Order (Simulates Network Traffic)...")
    oid = 200002
    hex_data = generate_packet(1, 888, 1, oid, "COLL TEST", "DATA GATHER")
    send_command(ser, f"RADIO:INJECT {hex_data}", duration=5.0)

    print("3. Verifying Collector State (Should track order)...")
    send_command(ser, "SYS:STATE", duration=1.0)

    print("\n   (Wait 2s for stability before Provisioning check...)")
    time.sleep(2.0)

    # ==================== PHASE 4: PROVISIONING ====================
    print("\n[PHASE 4] PROVISIONING CHECK")
    print("1. Checking Pairing Code (Device ID)...")
    ser.reset_input_buffer()
    send_command(ser, "SYS:STATE", duration=2.0)

    print("   (Look for 'id' in the JSON above - this is the Pairing Code)")

    # ==================== PHASE 5: ADD DEVICE (PROVISIONING LOGIC) ====================
    print("\n[PHASE 5] ADD DEVICE (PROVISIONING LOGIC)")

    print("1. Setting Mode to COLLECTOR (Wait 5s)...")
    send_command(ser, "MODE:SET 1", duration=5.0)

    print("2. Injecting PACKET from UNKNOWN Device (Should be BLOCKED)...")
    oid = 300001
    hex_data = generate_packet(1, 55555, 1, oid, "UNKNOWN LINE", "BAD DATA")
    send_command(ser, f"RADIO:INJECT {hex_data}", duration=5.0)

    print("3. Adding Device 55555 to Allow List...")
    send_command(ser, "PROV:ADD 55555", duration=5.0)

    print("4. Injecting PACKET from KNOWN Device (Should be AUTHORIZED)...")
    oid = 300002
    hex_data = generate_packet(1, 55555, 1, oid, "KNOWN LINE", "GOOD DATA")
    send_command(ser, f"RADIO:INJECT {hex_data}", duration=5.0)

    print("\n========================================")
    print("   TEST SUITE COMPLETE")
    print("========================================")


def verify_add(ser):
    print("========================================")
    print("   TUGR PROVISIONING VERIFICATION ONLY   ")
    print("========================================")

    # 1. Set Mode to Collector
    print("\n1. Setting Mode to COLLECTOR (Wait 5s)...")
    send_command(ser, "MODE:SET 1", duration=5.0)

    # 2. Inject Unknown Packet
    print("2. Injecting PACKET from UNKNOWN Device (Expect: PROV:BLOCK)...")
    oid = 300001
    hex_data = generate_packet(1, 55555, 1, oid, "UNKNOWN LINE", "BAD DATA")
    send_command(ser, f"RADIO:INJECT {hex_data}", duration=5.0)

    # 3. Add Device
    print("3. Adding Device 55555 to Allow List...")
    send_command(ser, "PROV:ADD 55555", duration=5.0)

    # 4. Inject Known Packet
    print("4. Injecting PACKET from KNOWN Device (Expect: PROV:AUTH)...")
    oid = 300002
    hex_data = generate_packet(1, 55555, 1, oid, "KNOWN LINE", "GOOD DATA")
    send_command(ser, f"RADIO:INJECT {hex_data}", duration=5.0)

    print("\n========================================")
    print("   VERIFICATION COMPLETE")
    print("========================================")


def main():
    parser = argparse.ArgumentParser(description="Tugr Serial CLI")
    parser.add_argument("port", help="COM Port (e.g., COM3)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument(
        "--test", action="store_true", help="Run automated test sequence"
    )
    parser.add_argument(
        "--verify-add", action="store_true", help="Run focused Add Device verification"
    )

    args = parser.parse_args()

    try:
        # TIMEOUT INCREASED to 2.0s to ensure long JSON lines aren't cut off
        ser = serial.Serial(args.port, args.baud, timeout=2.0)
        time.sleep(2)  # Wait for connection
        print(f"Connected to {args.port}")

        if args.verify_add:
            verify_add(ser)
        elif args.test:
            run_test(ser)
        else:
            interactive_mode(ser)

        ser.close()
    except Exception as e:
        print(f"Error: {e}")


if __name__ == "__main__":
    main()
