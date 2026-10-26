/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 */

#if defined(ENABLE_JT1078)
#include "Jt1078.h"
#include <algorithm>

namespace mediakit {

static bool checkJt1078Magic(const char *data, size_t len) {
    return len >= 4 && (uint8_t)data[0] == 0x30 && (uint8_t)data[1] == 0x31 && (uint8_t)data[2] == 0x63 && (uint8_t)data[3] == 0x64;
}

bool isJt1078(const char *data, size_t len) {
    return checkJt1078Magic(data, len);
}

uint64_t jt1078ReadBe16(const void *data) {
    const auto *b = (const uint8_t *)data;
    return ((uint16_t)b[0] << 8) | (uint16_t)b[1];
}

uint64_t jt1078ReadBe64(const void *data) {
    const auto *b = (const uint8_t *)data;
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32)
         | ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) | ((uint64_t)b[6] << 8) | (uint64_t)b[7];
}

void jt1078WriteBe16(uint16_t val, void *data) {
    auto *b = (uint8_t *)data;
    b[0] = (val >> 8) & 0xFF;
    b[1] = val & 0xFF;
}

void jt1078WriteBe64(uint64_t val, void *data) {
    auto *b = (uint8_t *)data;
    b[0] = (val >> 56) & 0xFF;
    b[1] = (val >> 48) & 0xFF;
    b[2] = (val >> 40) & 0xFF;
    b[3] = (val >> 32) & 0xFF;
    b[4] = (val >> 24) & 0xFF;
    b[5] = (val >> 16) & 0xFF;
    b[6] = (val >> 8) & 0xFF;
    b[7] = val & 0xFF;
}

static bool validateCommonFields(const Jt1078ParsedHeader &h) {
    if (!(h.v == 2 && h.p == 0 && h.x == 0 && h.cc == 1)) {
        return false;
    }
    if (h.ch < 1 || h.ch > 16) {
        return false;
    }
    if (h.frame_type > 4 || h.packet_type > 3) {
        return false;
    }
    return true;
}

static bool fillFromBytes(const uint8_t *data, size_t len, size_t sim_len, Jt1078ParsedHeader &out) {
    // 最小为 other 头: 4 magic + 1 V/P/X/CC + 1 M/PT + 2 seq + sim + 1 ch + 1 type + 2 payload
    const size_t min_other = 4 + 1 + 1 + 2 + sim_len + 1 + 1 + 2;
    if (len < min_other) {
        return false;
    }
    out.v = (data[4] >> 6) & 0x03;
    out.p = (data[4] >> 5) & 0x01;
    out.x = (data[4] >> 4) & 0x01;
    out.cc = data[4] & 0x0F;
    out.m = (data[5] >> 7) & 0x01;
    out.pt = data[5] & 0x7F;
    out.seq = (uint16_t)jt1078ReadBe16(data + 6);
    out.sim_len = sim_len;
    memcpy(out.sim, data + 8, sim_len);
    size_t off = 8 + sim_len;
    out.ch = data[off++];
    out.frame_type = (data[off] >> 4) & 0x0F;
    out.packet_type = data[off] & 0x0F;
    ++off;

    if (!validateCommonFields(out)) {
        return false;
    }

    if (out.frame_type <= kJt1078FrameB) {
        // video: timestamp(8) + i_interval(2) + last_i(2) + payload_size(2)
        if (len < off + 8 + 2 + 2 + 2) {
            return false;
        }
        out.timestamp = jt1078ReadBe64(data + off);
        off += 8;
        out.i_frame_interval = (uint16_t)jt1078ReadBe16(data + off);
        off += 2;
        out.frame_interval = (uint16_t)jt1078ReadBe16(data + off);
        off += 2;
        out.payload_size = (uint16_t)jt1078ReadBe16(data + off);
        off += 2;
    } else if (out.frame_type == kJt1078FrameAudio) {
        if (len < off + 8 + 2) {
            return false;
        }
        out.timestamp = jt1078ReadBe64(data + off);
        off += 8;
        out.payload_size = (uint16_t)jt1078ReadBe16(data + off);
        off += 2;
    } else {
        if (len < off + 2) {
            return false;
        }
        out.payload_size = (uint16_t)jt1078ReadBe16(data + off);
        off += 2;
    }
    // JT/T 1078 单包体最大 950；0 或超限基本是伪同步头
    if (out.payload_size == 0 || out.payload_size > kJt1078MaxPayload) {
        return false;
    }
    out.header_len = off;
    out.version = (sim_len == 10) ? Jt1078Version::V2019 : Jt1078Version::V2013;
    return true;
}

bool parseJt1078Header(const char *data, size_t len, Jt1078Version hint, Jt1078ParsedHeader &out) {
    if (!checkJt1078Magic(data, len)) {
        return false;
    }
    const auto *bytes = (const uint8_t *)data;
    if (hint == Jt1078Version::V2019) {
        return fillFromBytes(bytes, len, 10, out);
    }
    if (hint == Jt1078Version::V2013) {
        return fillFromBytes(bytes, len, 6, out);
    }
    // 自动探测：先试 2013，再试 2019
    if (fillFromBytes(bytes, len, 6, out)) {
        return true;
    }
    return fillFromBytes(bytes, len, 10, out);
}

Jt1078Version detectJt1078Version(const char *data, size_t len) {
    Jt1078ParsedHeader h;
    if (!parseJt1078Header(data, len, Jt1078Version::Unknown, h)) {
        return Jt1078Version::Unknown;
    }
    return h.version;
}

std::string jt1078SimToString(const uint8_t *sim, size_t sim_len) {
    static const char hex[] = "0123456789ABCDEF";
    std::string ret;
    ret.reserve(sim_len * 2);
    for (size_t i = 0; i < sim_len; ++i) {
        ret.push_back(hex[(sim[i] >> 4) & 0x0F]);
        ret.push_back(hex[sim[i] & 0x0F]);
    }
    return ret;
}

void jt1078ParseSimBcd(const std::string &sim, uint8_t *out, size_t out_len, size_t &written) {
    written = 0;
    memset(out, 0, out_len);
    auto nibble = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') {
            return (uint8_t)(c - '0');
        }
        if (c >= 'A' && c <= 'F') {
            return (uint8_t)(c - 'A' + 10);
        }
        if (c >= 'a' && c <= 'f') {
            return (uint8_t)(c - 'a' + 10);
        }
        return 0;
    };
    for (size_t i = 0; i + 1 < sim.size() && written < out_len; i += 2) {
        out[written++] = (uint8_t)((nibble(sim[i]) << 4) | nibble(sim[i + 1]));
    }
}

CodecId jt1078PtToCodec(uint8_t pt, TrackType &type) {
    switch (pt) {
        case 98:
            type = TrackVideo;
            return CodecH264;
        case 99:
            type = TrackVideo;
            return CodecH265;
        case 6:
            type = TrackAudio;
            return CodecG711A;
        case 7:
            type = TrackAudio;
            return CodecG711U;
        case 8:
            type = TrackAudio;
            return CodecG722;
        case 9:
            type = TrackAudio;
            return CodecG729;
        case 4:
            type = TrackAudio;
            return CodecG723;
        case 5:
            type = TrackAudio;
            return CodecG728;
        case 14:
            type = TrackAudio;
            return CodecMP3;
        case 19:
            type = TrackAudio;
            return CodecAAC;
        case 26:
            type = TrackAudio;
            return CodecADPCM;
        default:
            type = TrackInvalid;
            return CodecInvalid;
    }
}

uint8_t jt1078CodecToPt(CodecId codec, TrackType type) {
    if (type == TrackVideo) {
        return codec == CodecH265 ? 99 : 98;
    }
    switch (codec) {
        case CodecG711U: return 7;
        case CodecG722: return 8;
        case CodecG729: return 9;
        case CodecG723: return 4;
        case CodecG728: return 5;
        case CodecMP3: return 14;
        case CodecAAC: return 19;
        case CodecADPCM: return 26;
        default: return 6; // G711A
    }
}

Jt1078Version jt1078VersionFromString(const std::string &version) {
    if (version == "2019") {
        return Jt1078Version::V2019;
    }
    return Jt1078Version::V2013;
}

Jt1078SeqMode jt1078SeqModeFromString(const std::string &mode) {
    if (mode == "separate") {
        return Jt1078SeqMode::Separate;
    }
    if (mode == "shared") {
        return Jt1078SeqMode::Shared;
    }
    return Jt1078SeqMode::Auto;
}

std::string jt1078StreamSuffix(int stream_type) {
    switch (stream_type) {
        case kJt1078StreamPlayback: return "_playback";
        case kJt1078StreamSub: return "_sub";
        default: return "";
    }
}

} // namespace mediakit
#endif // defined(ENABLE_JT1078)
