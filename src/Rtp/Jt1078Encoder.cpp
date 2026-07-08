/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 */

#if defined(ENABLE_RTPPROXY)
#include "Jt1078Encoder.h"
#include "Extension/Factory.h"
#include "Util/logger.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

static void parseSimBcd(const string &sim, uint8_t *out, size_t out_len, size_t &written) {
    written = 0;
    for (size_t i = 0; i + 1 < sim.size() && written < out_len; i += 2) {
        auto nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'A' && c <= 'F') {
                return c - 'A' + 10;
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            return 0;
        };
        out[written++] = (nibble(sim[i]) << 4) | nibble(sim[i + 1]);
    }
}

Jt1078EncoderImp::Jt1078EncoderImp(Jt1078Version version, const string &sim, uint8_t channel, uint16_t fps)
    : _version(version), _sim(sim), _channel(channel), _fps(fps ? fps : 25) {
    parseSimBcd(_sim, _sim_bytes, sizeof(_sim_bytes), _sim_len);
    if (_version == Jt1078Version::V2019 && _sim_len < 10) {
        _sim_len = 10;
    }
}

bool Jt1078EncoderImp::addTrack(const Track::Ptr &track) {
    if (track->getTrackType() == TrackVideo) {
        _video_codec = track->getCodecId();
    } else if (track->getTrackType() == TrackAudio) {
        _audio_codec = track->getCodecId();
    }
    return true;
}

void Jt1078EncoderImp::resetTracks() {
    _video_codec = CodecInvalid;
    _audio_codec = CodecInvalid;
}

uint8_t Jt1078EncoderImp::getVideoPt(CodecId codec) const {
    return codec == CodecH265 ? 99 : 98;
}

uint8_t Jt1078EncoderImp::getAudioPt(CodecId codec) const {
    switch (codec) {
        case CodecG711U: return 7;
        case CodecAAC: return 19;
        default: return 6;
    }
}

bool Jt1078EncoderImp::isIFrame(const Frame::Ptr &frame) const {
    return frame->keyFrame() || frame->configFrame();
}

void Jt1078EncoderImp::sendVideoFrame(const Frame::Ptr &frame) {
    auto pt = getVideoPt(frame->getCodecId());
    bool i_frame = isIFrame(frame);
    if (i_frame) {
        _p_frame_count = 1;
    } else {
        ++_p_frame_count;
    }

    const char *data = frame->data() + frame->prefixSize();
    size_t remain = frame->size() - frame->prefixSize();
    size_t offset = 0;
    size_t packet_count = remain / kJt1078MaxPayload + ((remain % kJt1078MaxPayload) ? 1 : 0);
    size_t order = 0;

    while (remain > 0) {
        ++order;
        size_t chunk = remain > kJt1078MaxPayload ? kJt1078MaxPayload : remain;
        uint8_t packet_type = kJt1078PacketAtomic;
        if (packet_count > 1) {
            if (order == 1) {
                packet_type = kJt1078PacketFirst;
            } else if (remain <= kJt1078MaxPayload) {
                packet_type = kJt1078PacketLast;
            } else {
                packet_type = kJt1078PacketMiddle;
            }
        }

        BufferRaw::Ptr buf;
        if (_version == Jt1078Version::V2019) {
            buf = BufferRaw::create();
            buf->setCapacity(sizeof(Jt1078VideoHeader2019) + chunk);
            buf->setSize(sizeof(Jt1078VideoHeader2019) + chunk);
            auto *head = (Jt1078VideoHeader2019 *)buf->data();
            head->head[0] = 0x30;
            head->head[1] = 0x31;
            head->head[2] = 0x63;
            head->head[3] = 0x64;
            head->v = 2;
            head->p = 0;
            head->x = 0;
            head->cc = 1;
            head->m = 1;
            head->pt = pt;
            head->seq = htons(_seq++);
            memcpy(head->sim, _sim_bytes, _sim_len);
            head->ch = _channel;
            head->packet_type = packet_type;
            head->frame_type = i_frame ? kJt1078FrameI : kJt1078FrameP;
            jt1078WriteBe64(_video_ts, &head->timestamp);
            head->i_frame_interval = htons(_p_frame_count * (1000 / _fps));
            head->frame_interval = htons(1000 / _fps);
            head->payload_size = htons((uint16_t)chunk);
            memcpy(buf->data() + sizeof(Jt1078VideoHeader2019), data + offset, chunk);
        } else {
            buf = BufferRaw::create();
            buf->setCapacity(sizeof(Jt1078VideoHeader2013) + chunk);
            buf->setSize(sizeof(Jt1078VideoHeader2013) + chunk);
            auto *head = (Jt1078VideoHeader2013 *)buf->data();
            head->head[0] = 0x30;
            head->head[1] = 0x31;
            head->head[2] = 0x63;
            head->head[3] = 0x64;
            head->v = 2;
            head->p = 0;
            head->x = 0;
            head->cc = 1;
            head->m = 1;
            head->pt = pt;
            head->seq = htons(_seq++);
            memcpy(head->sim, _sim_bytes, min(_sim_len, (size_t)6));
            head->ch = _channel;
            head->packet_type = packet_type;
            head->frame_type = i_frame ? kJt1078FrameI : kJt1078FrameP;
            jt1078WriteBe64(_video_ts, &head->timestamp);
            head->i_frame_interval = htons(_p_frame_count * (1000 / _fps));
            head->frame_interval = htons(1000 / _fps);
            head->payload_size = htons((uint16_t)chunk);
            memcpy(buf->data() + sizeof(Jt1078VideoHeader2013), data + offset, chunk);
        }

        onPacket(std::move(buf));
        offset += chunk;
        remain -= chunk;
    }
    _video_ts += 1000 / _fps;
}

void Jt1078EncoderImp::sendAudioFrame(const Frame::Ptr &frame) {
    auto pt = getAudioPt(frame->getCodecId());
    const char *data = frame->data() + frame->prefixSize();
    size_t len = frame->size() - frame->prefixSize();

    BufferRaw::Ptr buf;
    if (_version == Jt1078Version::V2019) {
        buf = BufferRaw::create();
        buf->setCapacity(sizeof(Jt1078AudioHeader2019) + len);
        buf->setSize(sizeof(Jt1078AudioHeader2019) + len);
        auto *head = (Jt1078AudioHeader2019 *)buf->data();
        head->head[0] = 0x30;
        head->head[1] = 0x31;
        head->head[2] = 0x63;
        head->head[3] = 0x64;
        head->v = 2;
        head->p = 0;
        head->x = 0;
        head->cc = 1;
        head->m = 1;
        head->pt = pt;
        head->seq = htons(_seq++);
        memcpy(head->sim, _sim_bytes, _sim_len);
        head->ch = _channel;
        head->packet_type = kJt1078PacketAtomic;
        head->frame_type = kJt1078FrameAudio;
        jt1078WriteBe64(_audio_ts, &head->timestamp);
        head->payload_size = htons((uint16_t)len);
        memcpy(buf->data() + sizeof(Jt1078AudioHeader2019), data, len);
    } else {
        buf = BufferRaw::create();
        buf->setCapacity(sizeof(Jt1078AudioHeader2013) + len);
        buf->setSize(sizeof(Jt1078AudioHeader2013) + len);
        auto *head = (Jt1078AudioHeader2013 *)buf->data();
        head->head[0] = 0x30;
        head->head[1] = 0x31;
        head->head[2] = 0x63;
        head->head[3] = 0x64;
        head->v = 2;
        head->p = 0;
        head->x = 0;
        head->cc = 1;
        head->m = 1;
        head->pt = pt;
        head->seq = htons(_seq++);
        memcpy(head->sim, _sim_bytes, min(_sim_len, (size_t)6));
        head->ch = _channel;
        head->packet_type = kJt1078PacketAtomic;
        head->frame_type = kJt1078FrameAudio;
        jt1078WriteBe64(_audio_ts, &head->timestamp);
        head->payload_size = htons((uint16_t)len);
        memcpy(buf->data() + sizeof(Jt1078AudioHeader2013), data, len);
    }
    onPacket(std::move(buf));
    _audio_ts += 20;
}

bool Jt1078EncoderImp::inputFrame(const Frame::Ptr &frame) {
    if (frame->getTrackType() == TrackVideo) {
        sendVideoFrame(frame);
        return true;
    }
    if (frame->getTrackType() == TrackAudio) {
        sendAudioFrame(frame);
        return true;
    }
    return false;
}

RtpCacheJt1078::RtpCacheJt1078(onFlushed cb, Jt1078Version version, const string &sim, uint8_t channel, uint16_t fps)
    : RtpCache(std::move(cb)), Jt1078EncoderImp(version, sim, channel, fps) {}

bool RtpCacheJt1078::inputFrame(const Frame::Ptr &frame) {
    return Jt1078EncoderImp::inputFrame(frame);
}

void RtpCacheJt1078::flush() {
    RtpCache::flush();
}

void RtpCacheJt1078::onPacket(Buffer::Ptr packet) {
    input(0, std::move(packet), true);
}

} // namespace mediakit
#endif
