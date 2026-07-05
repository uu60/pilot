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

`pilot_wire.p4` currently parses Ethernet plus the Pilot header, marks switch-request packets, forwards by `receiver_rank`, and has a limited BMv2 data-plane BMT append prototype.

The current P4 program can transform bounded request shapes into an envelope:

```text
payload_size in {1,2,4,8,16,32} words
bmt_count in {4,16} triples
```

The BMT source is selected by the control plane through `p4/bmt_zero.cli`. It currently emits all-zero triples for functional validation only; this is not a secure BMT source.

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
simple_switch_CLI < p4/bmt_zero.cli
```

## Current BMv2 Status

Verified on `ppdsa-a6000.luddy.indiana.edu` with `p4lang/p4c:latest`:

```text
p4c compilation: PASS
BMv2 rank_forward table programming: PASS
Pilot TAP path through BMv2 forwarding: starts and forwards Pilot packets
Limited BMv2 BMT append for payload_size=1,bmt_count=4: PASS with rows=1,bundle=4
Bounded BMv2 BMT append for payload_size<=32,bmt_count=4: PASS with rows=8,bundle=4
Bounded BMv2 BMT append for payload_size<=32,bmt_count=16: PASS with rows=8,bundle=16
Bounded BMv2 BMT append with two lanes: PASS with rows=8,bundle=16,parallelism=2
```

The bounded P4 prototype transforms only switch requests with:

```text
payload_size in {1,2,4,8,16,32}
bmt_count in {4,16}
```

Requests outside those shapes are forwarded without transformation, so the receiver sees a request where it expects an envelope and logs:

```text
Invalid pilot envelope packet.
```

The C++ `pilot_tap_switch` remains the reference full switch implementation because it calls:

```text
InPathSwitchSimulator::forwardRequest()
```

To make BMv2 a complete replacement, the P4/BMv2 path needs to generalize this fixed-shape prototype. Practical options are:

```text
1. P4 registers/tables populated by the control plane with pre-generated BMT shares.
2. A BMv2 extern that implements the reference BMT append behavior.
3. A hybrid controller path that intercepts switch-request packets and reinjects envelope packets.
```

Option 1 is closest to FPGA/P4 hardware constraints. Option 2 is closest to the current C++ reference logic but is less portable to real hardware.
