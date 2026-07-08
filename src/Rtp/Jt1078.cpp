/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 */

#if defined(ENABLE_RTPPROXY)
#include "Jt1078.h"
#include "Extension/Frame.h"

namespace mediakit {

static bool checkJt1078Magic(const char *data, size_t len) {
    return len >= 4 && (uint8_t)data[0] == 0x30 && (uint8_t)data[1] == 0x31 && (uint8_t)data[2] == 0x63 && (uint8_t)data[3] == 0x64;
}

bool isJt1078(const char *data, size_t len) {
    return checkJt1078Magic(data, len);
}

static bool validateJt1078RtpFields(const uint8_t cc, const uint8_t x, const uint8_t p, const uint8_t v, const uint8_t ch,
                                    const uint8_t frame_type, const uint8_t packet_type) {
    if (!(v == 2 && p == 0 && x == 0 && cc == 1)) {
        return false;
    }
    if (ch < 1 || ch > 16) {
        return false;
    }
    if (frame_type > 4) {
        return false;
    }
    if (packet_type > 3) {
        return false;
    }
    return true;
}

Jt1078Version detectJt1078Version(const char *data, size_t len) {
    if (len < sizeof(Jt1078VideoHeader2013)) {
        return Jt1078Version::Unknown;
    }
    if (!checkJt1078Magic(data, len)) {
        return Jt1078Version::Unknown;
    }
    auto *head2013 = (Jt1078VideoHeader2013 *)data;
    if (validateJt1078RtpFields(head2013->cc, head2013->x, head2013->p, head2013->v, head2013->ch, head2013->frame_type,
                                head2013->packet_type)) {
        return Jt1078Version::V2013;
    }
    if (len < sizeof(Jt1078VideoHeader2019)) {
        return Jt1078Version::Unknown;
    }
    auto *head2019 = (Jt1078VideoHeader2019 *)data;
    if (validateJt1078RtpFields(head2019->cc, head2019->x, head2019->p, head2019->v, head2019->ch, head2019->frame_type,
                                head2019->packet_type)) {
        return Jt1078Version::V2019;
    }
    return Jt1078Version::Unknown;
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

uint64_t jt1078ReadBe64(const void *data) {
    const auto *b = (const uint8_t *)data;
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32)
         | ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) | ((uint64_t)b[6] << 8) | (uint64_t)b[7];
}

uint64_t jt1078WriteBe64(uint64_t val, void *data) {
    auto *b = (uint8_t *)data;
    b[0] = (val >> 56) & 0xFF;
    b[1] = (val >> 48) & 0xFF;
    b[2] = (val >> 40) & 0xFF;
    b[3] = (val >> 32) & 0xFF;
    b[4] = (val >> 24) & 0xFF;
    b[5] = (val >> 16) & 0xFF;
    b[6] = (val >> 8) & 0xFF;
    b[7] = val & 0xFF;
    return val;
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
        case 19:
            type = TrackAudio;
            return CodecAAC;
        default:
            type = TrackInvalid;
            return CodecInvalid;
    }
}

} // namespace mediakit
#endif // defined(ENABLE_RTPPROXY)
