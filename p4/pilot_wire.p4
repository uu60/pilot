#include <core.p4>
#include <v1model.p4>

const bit<16> PILOT_ETHERTYPE = 0x88b5;
const bit<32> PILOT_MAGIC = 0x50494c54;
const bit<8> PILOT_VERSION = 1;
const bit<32> PILOT_FLAG_SWITCH_REQUEST = 0x00000001;
const bit<32> PILOT_FLAG_SWITCH_ENVELOPE = 0x00000002;
const bit<64> PILOT_REQUEST_MAGIC = 0x5041525352455154;
const bit<64> PILOT_ENVELOPE_MAGIC = 0x5041525345434550;

header ethernet_t {
    bit<48> dst_addr;
    bit<48> src_addr;
    bit<16> ether_type;
}

header pilot_t {
    bit<32> magic;
    bit<8> version;
    bit<8> msg_type;
    bit<16> header_len;
    bit<16> sender_rank;
    bit<16> receiver_rank;
    bit<32> tag;
    bit<32> payload_items;
    bit<32> payload_bytes;
    bit<32> flags;
    bit<32> reserved;
}

header pilot_request_t {
    bit<64> magic;
    bit<64> tag;
    bit<64> lane;
    bit<64> bmt_count;
    bit<64> payload_size;
}

header pilot_envelope_t {
    bit<64> magic;
    bit<64> payload_size;
    bit<64> trailer_words;
    bit<64> src_rank;
    bit<64> dst_rank;
    bit<64> tag;
    bit<64> lane;
}

header pilot_payload1_t {
    bit<64> value;
}

header pilot_payload2_t {
    bit<64> v0;
    bit<64> v1;
}

header pilot_payload4_t {
    bit<64> v0;
    bit<64> v1;
    bit<64> v2;
    bit<64> v3;
}

header pilot_payload8_t {
    bit<64> v0;
    bit<64> v1;
    bit<64> v2;
    bit<64> v3;
    bit<64> v4;
    bit<64> v5;
    bit<64> v6;
    bit<64> v7;
}

header pilot_payload16_t {
    bit<64> v0;
    bit<64> v1;
    bit<64> v2;
    bit<64> v3;
    bit<64> v4;
    bit<64> v5;
    bit<64> v6;
    bit<64> v7;
    bit<64> v8;
    bit<64> v9;
    bit<64> v10;
    bit<64> v11;
    bit<64> v12;
    bit<64> v13;
    bit<64> v14;
    bit<64> v15;
}

header pilot_payload32_t {
    bit<64> v0;
    bit<64> v1;
    bit<64> v2;
    bit<64> v3;
    bit<64> v4;
    bit<64> v5;
    bit<64> v6;
    bit<64> v7;
    bit<64> v8;
    bit<64> v9;
    bit<64> v10;
    bit<64> v11;
    bit<64> v12;
    bit<64> v13;
    bit<64> v14;
    bit<64> v15;
    bit<64> v16;
    bit<64> v17;
    bit<64> v18;
    bit<64> v19;
    bit<64> v20;
    bit<64> v21;
    bit<64> v22;
    bit<64> v23;
    bit<64> v24;
    bit<64> v25;
    bit<64> v26;
    bit<64> v27;
    bit<64> v28;
    bit<64> v29;
    bit<64> v30;
    bit<64> v31;
}

header pilot_bmt4_t {
    bit<64> a0;
    bit<64> b0;
    bit<64> c0;
    bit<64> a1;
    bit<64> b1;
    bit<64> c1;
    bit<64> a2;
    bit<64> b2;
    bit<64> c2;
    bit<64> a3;
    bit<64> b3;
    bit<64> c3;
}

header pilot_bmt16_t {
    bit<64> a0;
    bit<64> b0;
    bit<64> c0;
    bit<64> a1;
    bit<64> b1;
    bit<64> c1;
    bit<64> a2;
    bit<64> b2;
    bit<64> c2;
    bit<64> a3;
    bit<64> b3;
    bit<64> c3;
    bit<64> a4;
    bit<64> b4;
    bit<64> c4;
    bit<64> a5;
    bit<64> b5;
    bit<64> c5;
    bit<64> a6;
    bit<64> b6;
    bit<64> c6;
    bit<64> a7;
    bit<64> b7;
    bit<64> c7;
    bit<64> a8;
    bit<64> b8;
    bit<64> c8;
    bit<64> a9;
    bit<64> b9;
    bit<64> c9;
    bit<64> a10;
    bit<64> b10;
    bit<64> c10;
    bit<64> a11;
    bit<64> b11;
    bit<64> c11;
    bit<64> a12;
    bit<64> b12;
    bit<64> c12;
    bit<64> a13;
    bit<64> b13;
    bit<64> c13;
    bit<64> a14;
    bit<64> b14;
    bit<64> c14;
    bit<64> a15;
    bit<64> b15;
    bit<64> c15;
}

struct headers {
    ethernet_t ethernet;
    pilot_t pilot;
    pilot_request_t request;
    pilot_envelope_t envelope;
    pilot_payload1_t payload1;
    pilot_payload2_t payload2;
    pilot_payload4_t payload4;
    pilot_payload8_t payload8;
    pilot_payload16_t payload16;
    pilot_payload32_t payload32;
    pilot_bmt4_t bmt4;
    pilot_bmt16_t bmt16;
}

struct metadata {
    bit<1> is_switch_request;
    bit<1> can_make_bmt4_envelope;
    bit<32> request_payload_size;
    bit<32> trailer_words;
}

parser ParserImpl(packet_in packet,
                  out headers hdr,
                  inout metadata meta,
                  inout standard_metadata_t standard_metadata) {
    state start {
        packet.extract(hdr.ethernet);
        transition select(hdr.ethernet.ether_type) {
            PILOT_ETHERTYPE: parse_pilot;
            default: accept;
        }
    }

    state parse_pilot {
        packet.extract(hdr.pilot);
        transition select(hdr.pilot.flags) {
            PILOT_FLAG_SWITCH_REQUEST: parse_request;
            default: accept;
        }
    }

    state parse_request {
        packet.extract(hdr.request);
        transition select(hdr.request.magic, hdr.request.payload_size, hdr.request.bmt_count) {
            (PILOT_REQUEST_MAGIC, 1, 4): parse_payload1;
            (PILOT_REQUEST_MAGIC, 2, 4): parse_payload2;
            (PILOT_REQUEST_MAGIC, 4, 4): parse_payload4;
            (PILOT_REQUEST_MAGIC, 8, 4): parse_payload8;
            (PILOT_REQUEST_MAGIC, 16, 4): parse_payload16;
            (PILOT_REQUEST_MAGIC, 32, 4): parse_payload32;
            (PILOT_REQUEST_MAGIC, 1, 16): parse_payload1;
            (PILOT_REQUEST_MAGIC, 2, 16): parse_payload2;
            (PILOT_REQUEST_MAGIC, 4, 16): parse_payload4;
            (PILOT_REQUEST_MAGIC, 8, 16): parse_payload8;
            (PILOT_REQUEST_MAGIC, 16, 16): parse_payload16;
            (PILOT_REQUEST_MAGIC, 32, 16): parse_payload32;
            default: accept;
        }
    }

    state parse_payload1 {
        packet.extract(hdr.payload1);
        transition accept;
    }

    state parse_payload2 {
        packet.extract(hdr.payload2);
        transition accept;
    }

    state parse_payload4 {
        packet.extract(hdr.payload4);
        transition accept;
    }

    state parse_payload8 {
        packet.extract(hdr.payload8);
        transition accept;
    }

    state parse_payload16 {
        packet.extract(hdr.payload16);
        transition accept;
    }

    state parse_payload32 {
        packet.extract(hdr.payload32);
        transition accept;
    }
}

control VerifyChecksumImpl(inout headers hdr, inout metadata meta) {
    apply {}
}

control IngressImpl(inout headers hdr,
                    inout metadata meta,
                    inout standard_metadata_t standard_metadata) {
    action drop() {
        mark_to_drop(standard_metadata);
    }

    action set_egress(bit<9> port) {
        standard_metadata.egress_spec = port;
    }

    action set_zero_bmt4() {
        hdr.bmt4.setValid();
        hdr.bmt4.a0 = 0;
        hdr.bmt4.b0 = 0;
        hdr.bmt4.c0 = 0;
        hdr.bmt4.a1 = 0;
        hdr.bmt4.b1 = 0;
        hdr.bmt4.c1 = 0;
        hdr.bmt4.a2 = 0;
        hdr.bmt4.b2 = 0;
        hdr.bmt4.c2 = 0;
        hdr.bmt4.a3 = 0;
        hdr.bmt4.b3 = 0;
        hdr.bmt4.c3 = 0;
        meta.can_make_bmt4_envelope = 1;
        meta.trailer_words = 12;
    }

    action set_zero_bmt16() {
        hdr.bmt16.setValid();
        hdr.bmt16.a0 = 0;
        hdr.bmt16.b0 = 0;
        hdr.bmt16.c0 = 0;
        hdr.bmt16.a1 = 0;
        hdr.bmt16.b1 = 0;
        hdr.bmt16.c1 = 0;
        hdr.bmt16.a2 = 0;
        hdr.bmt16.b2 = 0;
        hdr.bmt16.c2 = 0;
        hdr.bmt16.a3 = 0;
        hdr.bmt16.b3 = 0;
        hdr.bmt16.c3 = 0;
        hdr.bmt16.a4 = 0;
        hdr.bmt16.b4 = 0;
        hdr.bmt16.c4 = 0;
        hdr.bmt16.a5 = 0;
        hdr.bmt16.b5 = 0;
        hdr.bmt16.c5 = 0;
        hdr.bmt16.a6 = 0;
        hdr.bmt16.b6 = 0;
        hdr.bmt16.c6 = 0;
        hdr.bmt16.a7 = 0;
        hdr.bmt16.b7 = 0;
        hdr.bmt16.c7 = 0;
        hdr.bmt16.a8 = 0;
        hdr.bmt16.b8 = 0;
        hdr.bmt16.c8 = 0;
        hdr.bmt16.a9 = 0;
        hdr.bmt16.b9 = 0;
        hdr.bmt16.c9 = 0;
        hdr.bmt16.a10 = 0;
        hdr.bmt16.b10 = 0;
        hdr.bmt16.c10 = 0;
        hdr.bmt16.a11 = 0;
        hdr.bmt16.b11 = 0;
        hdr.bmt16.c11 = 0;
        hdr.bmt16.a12 = 0;
        hdr.bmt16.b12 = 0;
        hdr.bmt16.c12 = 0;
        hdr.bmt16.a13 = 0;
        hdr.bmt16.b13 = 0;
        hdr.bmt16.c13 = 0;
        hdr.bmt16.a14 = 0;
        hdr.bmt16.b14 = 0;
        hdr.bmt16.c14 = 0;
        hdr.bmt16.a15 = 0;
        hdr.bmt16.b15 = 0;
        hdr.bmt16.c15 = 0;
        meta.can_make_bmt4_envelope = 1;
        meta.trailer_words = 48;
    }

    table rank_forward {
        key = {
            hdr.pilot.receiver_rank: exact;
        }
        actions = {
            set_egress;
            drop;
            NoAction;
        }
        size = 4;
        default_action = drop();
    }

    table bmt_source {
        key = {
            hdr.request.bmt_count: exact;
        }
        actions = {
            set_zero_bmt4;
            set_zero_bmt16;
            NoAction;
        }
        size = 2;
        default_action = NoAction();
    }

    apply {
        meta.is_switch_request = 0;
        meta.can_make_bmt4_envelope = 0;
        meta.request_payload_size = 0;
        meta.trailer_words = 0;
        if (hdr.pilot.isValid() &&
            hdr.pilot.magic == PILOT_MAGIC &&
            hdr.pilot.version == PILOT_VERSION) {
            if ((hdr.pilot.flags & PILOT_FLAG_SWITCH_REQUEST) != 0) {
                meta.is_switch_request = 1;
            }

            if (meta.is_switch_request == 1 &&
                hdr.request.isValid() &&
                (hdr.payload1.isValid() ||
                 hdr.payload2.isValid() ||
                 hdr.payload4.isValid() ||
                 hdr.payload8.isValid() ||
                 hdr.payload16.isValid() ||
                 hdr.payload32.isValid())) {
                meta.request_payload_size = (bit<32>) hdr.request.payload_size;
                bmt_source.apply();
                if (meta.can_make_bmt4_envelope == 1) {
                    hdr.envelope.setValid();
                    hdr.envelope.magic = PILOT_ENVELOPE_MAGIC;
                    hdr.envelope.payload_size = (bit<64>) meta.request_payload_size;
                    hdr.envelope.trailer_words = (bit<64>) meta.trailer_words;
                    hdr.envelope.src_rank = (bit<64>) hdr.pilot.sender_rank;
                    hdr.envelope.dst_rank = (bit<64>) hdr.pilot.receiver_rank;
                    hdr.envelope.tag = (bit<64>) hdr.pilot.tag;
                    hdr.envelope.lane = hdr.request.lane;
                    hdr.request.setInvalid();
                    hdr.pilot.payload_items = 7 + meta.request_payload_size + meta.trailer_words;
                    hdr.pilot.payload_bytes = hdr.pilot.payload_items * 8;
                    hdr.pilot.flags = PILOT_FLAG_SWITCH_ENVELOPE;
                }
            }

            rank_forward.apply();
        }
    }
}

control EgressImpl(inout headers hdr,
                   inout metadata meta,
                   inout standard_metadata_t standard_metadata) {
    apply {}
}

control ComputeChecksumImpl(inout headers hdr, inout metadata meta) {
    apply {}
}

control DeparserImpl(packet_out packet, in headers hdr) {
    apply {
        packet.emit(hdr.ethernet);
        packet.emit(hdr.pilot);
        packet.emit(hdr.envelope);
        packet.emit(hdr.request);
        packet.emit(hdr.payload1);
        packet.emit(hdr.payload2);
        packet.emit(hdr.payload4);
        packet.emit(hdr.payload8);
        packet.emit(hdr.payload16);
        packet.emit(hdr.payload32);
        packet.emit(hdr.bmt4);
        packet.emit(hdr.bmt16);
    }
}

V1Switch(
    ParserImpl(),
    VerifyChecksumImpl(),
    IngressImpl(),
    EgressImpl(),
    ComputeChecksumImpl(),
    DeparserImpl()
) main;
