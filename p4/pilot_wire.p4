#include <core.p4>
#include <v1model.p4>

const bit<16> PILOT_ETHERTYPE = 0x88b5;
const bit<32> PILOT_MAGIC = 0x50494c54;
const bit<8> PILOT_VERSION = 1;
const bit<32> PILOT_FLAG_SWITCH_REQUEST = 0x00000001;

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

struct headers {
    ethernet_t ethernet;
    pilot_t pilot;
}

struct metadata {
    bit<1> is_switch_request;
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

    apply {
        meta.is_switch_request = 0;
        if (hdr.pilot.isValid() &&
            hdr.pilot.magic == PILOT_MAGIC &&
            hdr.pilot.version == PILOT_VERSION) {
            if ((hdr.pilot.flags & PILOT_FLAG_SWITCH_REQUEST) != 0) {
                meta.is_switch_request = 1;
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
