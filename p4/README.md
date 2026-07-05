# Pilot P4/BMv2 Prototype

This directory is the starting point for moving the in-path switch behavior toward a P4/FPGA-friendly data plane.

The C++ TAP and TCP software-switch paths now share the same `PilotWireMessageCodec` header:

```text
ethernet
pilot header, 32 bytes, network byte order
payload
```

Pilot header layout:

```text
0   bit<32> magic          0x50494c54, "PILT"
4   bit<8>  version        1
5   bit<8>  msg_type       1=int64, 2=vector<int64>, 3=string
6   bit<16> header_len     32
8   bit<16> sender_rank
10  bit<16> receiver_rank
12  bit<32> tag
16  bit<32> payload_items  word count for int/vector payloads
20  bit<32> payload_bytes
24  bit<32> flags          bit0=switch request, bit1=switch envelope
28  bit<32> reserved
```

`pilot_wire.p4` currently parses Ethernet plus the Pilot header, marks switch-request packets, and forwards by `receiver_rank`.

The current P4 program does not yet append BMT trailers. The next BMv2 step is to add a BMT data source, either with registers populated by the control plane or a BMv2 extern. The C++ switch remains the reference behavior for `InPathSwitchSimulator::forwardRequest()`.

Example BMv2 flow:

```bash
p4c --target bmv2 --arch v1model p4/pilot_wire.p4 -o build-p4

simple_switch \
  -i 0@tap-sw0 \
  -i 1@tap-sw1 \
  -i 2@tap-sw2 \
  build-p4/pilot_wire.json
```

Then install rank forwarding entries with `simple_switch_CLI`, mapping each `receiver_rank` to the BMv2 port connected to that rank.

For the default three-rank test topology:

```text
rank 0, server0 -> BMv2 port 0
rank 1, server1 -> BMv2 port 1
rank 2, client  -> BMv2 port 2
```

the CLI commands are in `p4/rank_forwarding.cli`:

```bash
simple_switch_CLI < p4/rank_forwarding.cli
```

## Current BMv2 Status

Verified on `ppdsa-a6000.luddy.indiana.edu` with `p4lang/p4c:latest`:

```text
p4c compilation: PASS
BMv2 rank_forward table programming: PASS
Pilot TAP path through BMv2 forwarding: starts and forwards Pilot packets
Full BMT sort with rows=8: FAILS/TIMES OUT
```

The failure is expected at this stage. BMv2 currently forwards packets whose `flags` contain `PILOT_FLAG_SWITCH_REQUEST`, but it does not transform the request into a Pilot envelope and does not append BMT trailer words. The receiver therefore sees a request where it expects an envelope and logs:

```text
Invalid pilot envelope packet.
```

The C++ `pilot_tap_switch` remains the reference full switch implementation because it calls:

```text
InPathSwitchSimulator::forwardRequest()
```

To make BMv2 a complete replacement, the P4/BMv2 path needs a BMT data source and packet rewrite path. Practical options are:

```text
1. P4 registers populated by the control plane with pre-generated BMT shares.
2. A BMv2 extern that implements the reference BMT append behavior.
3. A hybrid controller path that intercepts switch-request packets and reinjects envelope packets.
```

Option 1 is closest to FPGA/P4 hardware constraints. Option 2 is closest to the current C++ reference logic but is less portable to real hardware.
