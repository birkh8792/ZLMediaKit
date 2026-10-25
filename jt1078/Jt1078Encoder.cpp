/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 */

#if defined(ENABLE_JT1078)
#include "Jt1078Encoder.h"
#include "Util/logger.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

Jt1078EncoderImp::Jt1078EncoderImp(Jt1078Version version, const string &sim, uint8_t channel, uint16_t fps, Jt1078SeqMode seq_mode)
    : _version(version), _seq_mode(seq_mode == Jt1078SeqMode::Auto ? Jt1078SeqMode::Shared : seq_mode), _sim(sim),
      _channel(channel), _fps(fps ? fps : 25) {
    jt1078ParseSimBcd(_sim, _sim_bytes, sizeof(_sim_bytes), _sim_len);
    _sim_len = (_version == Jt1078Version::V2019) ? 10 : 6;
}

void Jt1078EncoderImp::setSeqMode(Jt1078SeqMode mode) {
    _seq_mode = (mode == Jt1078SeqMode::Auto) ? Jt1078SeqMode::Shared : mode;
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

uint16_t Jt1078EncoderImp::nextSeq(bool is_audio) {
    if (_seq_mode == Jt1078SeqMode::Separate) {
        return is_audio ? _audio_seq++ : _video_seq++;
    }
    return _seq++;
}

void Jt1078EncoderImp::writeCommonHeader(uint8_t *data, uint8_t pt, uint16_t seq, uint8_t frame_type, uint8_t packet_type) {
    data[0] = 0x30;
    data[1] = 0x31;
    data[2] = 0x63;
    data[3] = 0x64;
    // V=2 P=0 X=0 CC=1
    data[4] = 0x81;
    data[5] = (uint8_t)(0x80 | (pt & 0x7F)); // M=1
    jt1078WriteBe16(seq, data + 6);
    size_t sim_bytes = (_version == Jt1078Version::V2019) ? 10 : 6;
    memcpy(data + 8, _sim_bytes, sim_bytes);
    size_t off = 8 + sim_bytes;
    data[off++] = _channel;
    data[off] = (uint8_t)(((frame_type & 0x0F) << 4) | (packet_type & 0x0F));
}

void Jt1078EncoderImp::sendVideoFrame(const Frame::Ptr &frame) {
    auto pt = jt1078CodecToPt(frame->getCodecId(), TrackVideo);
    bool i_frame = frame->keyFrame() || frame->configFrame();
    if (i_frame) {
        _p_frame_count = 1;
    } else {
        ++_p_frame_count;
    }

    const char *frame_data = frame->data() + frame->prefixSize();
    size_t remain = frame->size() - frame->prefixSize();
    size_t offset = 0;
    size_t packet_count = remain / kJt1078MaxPayload + ((remain % kJt1078MaxPayload) ? 1 : 0);
    if (remain == 0) {
        return;
    }
    size_t order = 0;
    size_t head_len = (_version == Jt1078Version::V2019) ? kJt1078VideoHead2019 : kJt1078VideoHead2013;

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

        auto buf = BufferRaw::create();
        buf->setCapacity(head_len + chunk);
        buf->setSize(head_len + chunk);
        auto *data = (uint8_t *)buf->data();
        writeCommonHeader(data, pt, nextSeq(false), i_frame ? kJt1078FrameI : kJt1078FrameP, packet_type);
        size_t off = 8 + ((_version == Jt1078Version::V2019) ? 10 : 6) + 2;
        jt1078WriteBe64(_video_ts, data + off);
        off += 8;
        jt1078WriteBe16((uint16_t)(_p_frame_count * (1000 / _fps)), data + off);
        off += 2;
        jt1078WriteBe16((uint16_t)(1000 / _fps), data + off);
        off += 2;
        jt1078WriteBe16((uint16_t)chunk, data + off);
        off += 2;
        memcpy(data + off, frame_data + offset, chunk);

        onPacket(std::move(buf));
        offset += chunk;
        remain -= chunk;
    }
    _video_ts += 1000 / _fps;
}

void Jt1078EncoderImp::sendAudioFrame(const Frame::Ptr &frame) {
    auto pt = jt1078CodecToPt(frame->getCodecId(), TrackAudio);
    const char *frame_data = frame->data() + frame->prefixSize();
    size_t len = frame->size() - frame->prefixSize();
    size_t head_len = (_version == Jt1078Version::V2019) ? kJt1078AudioHead2019 : kJt1078AudioHead2013;

    auto buf = BufferRaw::create();
    buf->setCapacity(head_len + len);
    buf->setSize(head_len + len);
    auto *data = (uint8_t *)buf->data();
    writeCommonHeader(data, pt, nextSeq(true), kJt1078FrameAudio, kJt1078PacketAtomic);
    size_t off = 8 + ((_version == Jt1078Version::V2019) ? 10 : 6) + 2;
    jt1078WriteBe64(_audio_ts, data + off);
    off += 8;
    jt1078WriteBe16((uint16_t)len, data + off);
    off += 2;
    memcpy(data + off, frame_data, len);
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

RtpCacheJt1078::RtpCacheJt1078(onFlushed cb, Jt1078Version version, const string &sim, uint8_t channel, uint16_t fps, Jt1078SeqMode seq_mode)
    : RtpCache(std::move(cb)), Jt1078EncoderImp(version, sim, channel, fps, seq_mode) {}

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
#endif // defined(ENABLE_JT1078)
