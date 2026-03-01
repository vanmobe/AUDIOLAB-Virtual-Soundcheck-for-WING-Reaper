#!/usr/bin/env python3
import rtmidi
import time

print("Listening for MIDI input from Wing...")
print("Press a Wing button now (listening for 5 seconds)...\n")

midiIn = rtmidi.MidiIn()
ports = midiIn.get_ports()

# Try to open Wing port
wing_port = None
for i, port in enumerate(ports):
    if 'WING' in port.upper():
        wing_port = i
        print(f"Opening {port}")
        break

if wing_port is not None:
    midiIn.open_port(wing_port)
else:
    print("Wing device not found in input ports:")
    for i, port in enumerate(ports):
        print(f"  {i}: {port}")
    exit(1)

start = time.time()
while time.time() - start < 5:
    msg = midiIn.get_message()
    if msg:
        midi_msg, deltatime = msg
        # msg format: [status, data1, data2]
        if len(midi_msg) >= 2:
            status = midi_msg[0]
            cc_num = midi_msg[1]
            cc_val = midi_msg[2] if len(midi_msg) > 2 else 0
            print(f"Status: {status} (0x{status:02X}), CC#: {cc_num}, Value: {cc_val}")
    time.sleep(0.01)

print("\nDone")
