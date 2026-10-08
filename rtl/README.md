# PILOT MPC switch -- RTL

In-path FPGA switch for PILOT. It forwards PILOT frames between two servers
and a client over AXI-Stream, and answers triple requests with Beaver
multiplication triples pre-generated and AES-128-GCM-encrypted in the
background, one encrypted share per server.

**Version:** hardcoded AES + MAC keys (key provisioning shelved), HMAC request/response
authentication, boot-seed gate, 2026-10-07.

## Hierarchy

```
pilot_switch_top                     pilot_switch_top.sv   (top, packet FSM, seed gate)
 ├─ u_hmac      pilot_hmac           pilot_hmac.v          (HMAC-SHA256, request + response)
 ├─ u_sha_auth  sha256_core          sha256_core.v         (SHA-256, driven by u_hmac)
 │    ├─ sha256_w_mem                sha256_w_mem.v        (message schedule)
 │    └─ sha256_k_constants          sha256_k_constants.v  (round constants)
 └─ u_bmt   bmt_pregen               bmt_pregen.v          (triple generator + 2 FIFOs)
     ├─ u_prng  prng_xoshiro256pp    prng_xoshiro256pp.sv  (randomness, stand-in)
     └─ u_gcm   aes_gcm_encrypt_24b  aes_gcm_encrypt_24b.v (24-byte AES-128-GCM)
         ├─ u_aes   aes128_encrypt_pipeline_fpga   aes128_round_fpga.v
         │    ├─ aes128_key_expansion_fpga         aes128_key_expansion_fpga.v
         │    └─ aes128_round_fpga x10             aes128_round_fpga.v
         │         └─ S-box, ShiftRows, MixColumns, AddRoundKey   aes_func.v
         └─ u_ghash ghash_single_cycle_fpga        ghash_single_cycle_fpga.v
pilot_pkg  (package: constants, header layout)     pilot_pkg.sv
```

13 RTL files in all.

## Files

| File | Contents |
|---|---|
| `pilot_switch_top.sv` | Seed gate; ingest, parse, request authentication (`S_AUTH`), response build (header, payload copy, triple trailer), response MAC (`S_RMAC`), egress mux |
| `bmt_pregen.v` | Boot-seed gate; draws a/b, computes c, splits into shares, encrypts each share under its server's key, pushes to per-server FIFOs |
| `prng_xoshiro256pp.sv` | xoshiro256++, 64 bits per advance; seed load (bmt_pregen admits one nonzero seed per reset) |
| `aes_gcm_encrypt_24b.v` | AES-128-GCM for exactly 24 bytes, no AAD; outputs CT0, CT1 (upper 64 bits) and tag |
| `aes128_round_fpga.v` | One AES round, and the 11-cycle unrolled pipeline `aes128_encrypt_pipeline_fpga` |
| `aes128_key_expansion_fpga.v` | Combinational AES-128 key schedule |
| `aes_func.v` | S-box, GF(2^8) x2/x3, SubBytes, ShiftRows, MixColumns, AddRoundKey, SubWord |
| `ghash_single_cycle_fpga.v` | Karatsuba GF(2^128) multiplier and 2-stage GHASH |
| `pilot_pkg.sv` | Ethertype, magic numbers, ranks, header structs |
| `pilot_hmac.v` | HMAC-SHA256 (RFC 2104), 32-byte key, message read block by block from the switch buffer |
| `sha256_core.v`, `sha256_w_mem.v`, `sha256_k_constants.v` | SHA-256 core (Secworks, BSD-2-Clause; keep the license headers) |

## Top-level parameters

| Parameter | Default | Meaning |
|---|---|---|
| `MAX_FRAME_BYTES` | 8192 | Frame buffer size (in and out) |
| `BMT_FIFO_DEPTH` | 128 | Pre-generated triples per server |
| `BMT_BUNDLE_SIZE` | 64 | Triple trailer padded to this many entries (0 = no padding) |
| `DATA_W` | 512 | AXI-Stream width |
| `AES_KEY_S0`, `AES_KEY_S1` | placeholders | Per-server AES-128 keys. **Override at integration**; keep real values out of version control. Elaboration fails if equal. |
| `MAC_KEY_S0`, `MAC_KEY_S1` | placeholders | Per-server HMAC-SHA256 keys (256-bit). Override at integration. |
| `SEED_GATE` | 1 | 1: no request service or triple generation until a nonzero seed arrives. 0: start after reset from a fixed default seed (testing only). |

## Ports

- `clk`, `rst_n` (active-low, asynchronous)
- `seed_valid`, `seed_data[255:0]`: **required** once per reset (SEED_GATE=1).
  The first `seed_valid` with nonzero `seed_data` after reset is latched and
  loaded into the PRNG; a zero seed is ignored (all-zero is xoshiro's fixed
  point), and so is every later seed until the next reset. Drive it from a
  fresh entropy source on every boot.
- Ports 0/1/2 (server 0, server 1, client): AXI-Stream in `s*_t{data,keep,last,valid,ready}`
  and out `m*_t{data,keep,last,valid,ready}`

## Behaviour summary

- Frames are accepted one at a time, from the first port with `tvalid`.
- Frames longer than `MAX_FRAME_BYTES` are discarded to `tlast`. Frames
  shorter than 26 bytes are dropped.
- A frame is a **request** if it is at least 86 bytes, from and to a server
  rank, a vector message with the request flag and request magic. Requests
  are dropped if no seed has arrived yet, if the payload (+16-byte TAG) is
  too large for the response or truncated, if the HMAC TAG is wrong, or if
  SEQ (bytes 42..45) is not above the last one accepted from that server.
  Otherwise the response goes to the *other* server: rebuilt header,
  payload copied, then `BMT_BUNDLE_SIZE` x 52-byte trailer entries
  (IV 12 B, CT0 16 B, CT1 8 B, tag 16 B), real triples first, zero padding
  after, then a 16-byte RTAG (HMAC with the destination server's MAC key)
  over bytes 14..end, with a per-destination RSEQ in bytes 42..45.
- Any other frame is forwarded unchanged to the port its receiver rank names.

## Build

- Vivado: add all 13 files; top is `pilot_switch_top`; set `AES_KEY_S0` /
  `AES_KEY_S1`, `MAC_KEY_S0` / `MAC_KEY_S1` as generics. Do not define
  `HMAC_STUB` (formal only).
- Do **not** define `FORMAL` (it pulls in the property modules) or
  `PILOT_FIXED_KEYS` (mutation-campaign only) for synthesis or simulation.
- `pilot_switch_top.sv` and `bmt_pregen.v` include property files under
  `` `ifdef FORMAL ``; those live in the `formal/` directory, not here.

- The three SHA-256 files set `` `default_nettype none `` and do not reset it
  (`pilot_hmac.v` does). If a file compiled after them reports implicit-net
  errors, compile the SHA files later or add `` `default_nettype wire `` at
  their end.

## Bugs fixed in this RTL

1. `bmt_pregen`: FIFOs desynchronised when one was full (`both_full` -> `any_full`).
2. (verification stub, not in this folder) AES-GCM stub accepted `start` a cycle early.
3. Unbounded ingest overwrote the header -> oversized-frame guard + `S_DISCARD`.
4. Byte counters one bit too narrow -> `CNT_W = ADDR_W + 1`.
5. Pop strobe one cycle stale -> `pop_inflight` gating.
6. Runt/truncated frames parsed on stale buffer bytes -> minimum-length checks
   and truncated-payload reject in `S_PARSE`.

7. Same triples and GCM IVs after every reset (PRNG ran from constant reset
   state) -> seed gate in `pilot_switch_top` (`keys_valid`, requests dropped
   until seeded) plus a one-seed-per-reset gate inside `bmt_pregen`
   (`seeded`; nothing is drawn, encrypted or pushed before it).
8. An all-zero seed froze the PRNG at zero (all triples zero, one repeated
   GCM IV) -> zero seeds ignored at both gates.

Also: parse-time `MAX_PAYLOAD_WORDS` reject; `pilot_pkg` functions inlined
at their call sites (Yosys workaround).

## Known issues (open)

- **Seed quality is the integrator's job.** The gate guarantees no triple is
  produced before a nonzero seed arrives and that the seed cannot be replaced
  without a reset. It cannot tell a fresh seed from a repeated one: feed it
  from a hardware entropy source on every boot, never a constant.
- **Drop counters are not visible.** `oversize_drops`, `runt_drops` and
  `unprovisioned_drops`, `auth_drops`, `replay_drops` are internal only and are removed by synthesis. Expose them or delete them.
- **`prng_xoshiro256pp` is a stand-in** for a real entropy source and is not
  cryptographically secure.

## Verification status (summary)

Formal proofs and mutation campaigns live in `formal/`, `mcy_switch/` and
`mcy_switch_buf/`.

- `prng_xoshiro256pp`: proven (unbounded, k-induction) against a reference
  model of xoshiro256++ -- reset value, seed load, state update, output
  function, hold, and no all-zero state after a nonzero seed (`prng.sby`).
- `bmt_pregen` seed gate: `bmt_seed.sby` -- nothing drawn, encrypted or pushed
  before seeding; zero seed rejected; one seed per reset; `seeded` sticky.
- `pilot_switch_top`: the 2026-10-07 seed-gate change (zero seed ignored,
  one seed per reset, `SEED_GATE=0` loads a default seed) has been checked
  only with a small bounded check of the gate; the full property sweep with
  your HMAC property files is still to be rerun on this version.
- `pilot_hmac` + `sha256_core`: known-answer simulation against Python
  `hmac` passes 11/11 (RFC 4231 case 2, and random 32-byte keys with message
  lengths 0, 1, 47, 55, 56, 63, 64, 119, 120, 200 bytes).
- Full design (all 13 files, real HMAC) elaborates with Yosys
  `check -assert`: 0 problems.
- `aes_gcm_encrypt_24b` is proven with its datapath abstracted; a known-answer
  simulation is still to do.
