/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 */

#if defined(ENABLE_RTPPROXY)
#include "Jt1078Process.h"
#include "Common/MediaSink.h"
#include "Extension/Factory.h"
#include "Util/logger.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

Jt1078Process::Jt1078Process(const MediaInfo &media_info, MediaSinkInterface *sink) {
    _media_info = media_info;
    _interface = sink;
    _buffer.reserve(64 * 1024);
    _video_buffer.reserve(512 * 1024);
}

void Jt1078Process::setOnStreamIdentified(onStreamIdentified cb) {
    _on_stream_identified = std::move(cb);
}

void Jt1078Process::setVersion(Jt1078Version version) {
    _version = version;
}

void Jt1078Process::flush() {
    if (!_video_buffer.empty()) {
        resetVideoBuffer();
    }
}

bool Jt1078Process::inputRtp(bool is_udp, const char *data, size_t data_len) {
    if (!data || !data_len) {
        return false;
    }
    _buffer.append(data, data_len);
    if (_buffer.size() > kJt1078CacheSize) {
        WarnL << "JT1078 buffer overflow, clear cache: " << _media_info.shortUrl();
        _buffer.clear();
        resetVideoBuffer();
        return false;
    }
    skipHttpHeaderIfNeeded();
    return parseBuffer();
}

void Jt1078Process::skipHttpHeaderIfNeeded() {
    if (_skip_http_header) {
        return;
    }
    static const char kHttpEnd[] = "\r\n\r\n";
    auto pos = _buffer.find(kHttpEnd);
    if (pos != string::npos) {
        _buffer.erase(0, pos + 4);
        _skip_http_header = true;
        InfoL << "JT1078 skip http header, remain: " << _buffer.size();
    } else if (_buffer.size() > 256 * 1024) {
        // 非 HTTP 1078 拉流，直接解析
        _skip_http_header = true;
    }
}

size_t Jt1078Process::findHeaderPos() const {
    for (size_t i = 0; i + 3 < _buffer.size(); ++i) {
        if (isJt1078(_buffer.data() + i, _buffer.size() - i)) {
            return i;
        }
    }
    return string::npos;
}

void Jt1078Process::resetVideoBuffer() {
    _video_buffer.clear();
}

bool Jt1078Process::ensureTrack(CodecId codec, TrackType type) {
    if (type == TrackVideo) {
        if (_video_track_added) {
            return true;
        }
        auto track = Factory::getTrackByCodecId(codec);
        if (!track) {
            return false;
        }
        _video_track_added = _interface->addTrack(track);
        if (_video_track_added) {
            _interface->addTrackCompleted();
        }
        return _video_track_added;
    }
    if (_audio_track_added) {
        return true;
    }
    Track::Ptr track;
    if (codec == CodecG711A || codec == CodecG711U) {
        track = Factory::getTrackByCodecId(codec, 8000, 1);
    } else {
        track = Factory::getTrackByCodecId(codec);
    }
    if (!track) {
        return false;
    }
    _audio_track_added = _interface->addTrack(track);
    if (_audio_track_added) {
        _interface->addTrackCompleted();
    }
    return _audio_track_added;
}

bool Jt1078Process::outputVideoFrame(uint8_t pt, uint64_t dts, bool key) {
    if (_video_buffer.empty()) {
        return false;
    }
    TrackType track_type = TrackInvalid;
    auto codec = jt1078PtToCodec(pt, track_type);
    if (codec == CodecInvalid || !ensureTrack(codec, track_type)) {
        resetVideoBuffer();
        return false;
    }
    _last_dts = dts;
    auto frame = Factory::getFrameFromPtr(codec, _video_buffer.data(), _video_buffer.size(), dts, dts);
    if (frame) {
        _interface->inputFrame(frame);
    }
    resetVideoBuffer();
    return true;
}

bool Jt1078Process::outputAudioFrame(uint8_t pt, const char *data, size_t len, uint64_t dts) {
    if (!data || !len) {
        return false;
    }
    TrackType track_type = TrackInvalid;
    auto codec = jt1078PtToCodec(pt, track_type);
    if (codec == CodecInvalid || !ensureTrack(codec, track_type)) {
        return false;
    }
    _last_dts = dts;
    auto frame = Factory::getFrameFromPtr(codec, (char *)data, len, dts, dts);
    if (frame) {
        _interface->inputFrame(frame);
    }
    return true;
}

static void identifyStream(const uint8_t *sim, size_t sim_len, uint8_t ch, bool &identified, Jt1078Process::onStreamIdentified &cb) {
    if (identified || !cb) {
        return;
    }
    identified = true;
    cb(jt1078SimToString(sim, sim_len), ch);
}

bool Jt1078Process::parseBuffer() {
    bool parsed = false;
    while (true) {
        auto pos = findHeaderPos();
        if (pos == string::npos) {
            if (_buffer.size() > 1024) {
                _buffer.clear();
            }
            break;
        }
        if (pos > 0) {
            _buffer.erase(0, pos);
        }

        if (_version == Jt1078Version::Unknown) {
            _version = detectJt1078Version(_buffer.data(), _buffer.size());
            if (_version == Jt1078Version::Unknown) {
                _buffer.erase(0, 1);
                continue;
            }
        }

        if (_version == Jt1078Version::V2013) {
            if (_buffer.size() < sizeof(Jt1078VideoHeader2013)) {
                break;
            }
            auto *base = (Jt1078VideoHeader2013 *)_buffer.data();
            if (!isJt1078((char *)base, _buffer.size())) {
                _buffer.erase(0, 1);
                continue;
            }

            identifyStream(base->sim, 6, base->ch, _stream_identified, _on_stream_identified);

            size_t head_len = 0;
            uint16_t payload_size = 0;
            uint8_t pt = 0;
            uint8_t frame_type = 0;
            uint8_t packet_type = 0;
            uint64_t dts = 0;

            if (base->frame_type <= kJt1078FrameB) {
                head_len = sizeof(Jt1078VideoHeader2013);
                payload_size = ntohs(base->payload_size);
                pt = base->pt;
                frame_type = base->frame_type;
                packet_type = base->packet_type;
                dts = jt1078ReadBe64(&base->timestamp);
            } else if (base->frame_type == kJt1078FrameAudio) {
                auto *audio = (Jt1078AudioHeader2013 *)_buffer.data();
                head_len = sizeof(Jt1078AudioHeader2013);
                payload_size = ntohs(audio->payload_size);
                pt = audio->pt;
                frame_type = audio->frame_type;
                packet_type = audio->packet_type;
                dts = jt1078ReadBe64(&audio->timestamp);
            } else {
                auto *other = (Jt1078OtherHeader2013 *)_buffer.data();
                head_len = sizeof(Jt1078OtherHeader2013);
                payload_size = ntohs(other->payload_size);
                frame_type = other->frame_type;
            }

            if (_buffer.size() < head_len + payload_size) {
                break;
            }

            auto payload = _buffer.data() + head_len;
            if (frame_type <= kJt1078FrameB) {
                if (_video_pt < 0) {
                    _video_pt = pt;
                }
                _video_buffer.append(payload, payload_size);
                if (packet_type == kJt1078PacketAtomic || packet_type == kJt1078PacketLast) {
                    outputVideoFrame(pt, dts, frame_type == kJt1078FrameI);
                } else if (packet_type == kJt1078PacketFirst || packet_type == kJt1078PacketMiddle) {
                    // keep buffering
                } else {
                    resetVideoBuffer();
                }
            } else if (frame_type == kJt1078FrameAudio) {
                static const uint8_t kAudioPrefix[] = {0x00, 0x01, 0xa0, 0x00};
                if (payload_size > 4 && payload_size <= 324
                    && (payload_size == 324 || payload_size == 164 || memcmp(payload, kAudioPrefix, 4) == 0)) {
                    outputAudioFrame(pt, payload + 4, payload_size - 4, dts);
                } else {
                    outputAudioFrame(pt, payload, payload_size, dts);
                }
            }
            _buffer.erase(0, head_len + payload_size);
            parsed = true;
            continue;
        }

        if (_version == Jt1078Version::V2019) {
            if (_buffer.size() < sizeof(Jt1078VideoHeader2019)) {
                break;
            }
            auto *base = (Jt1078VideoHeader2019 *)_buffer.data();
            if (!isJt1078((char *)base, _buffer.size())) {
                _buffer.erase(0, 1);
                continue;
            }

            identifyStream(base->sim, 10, base->ch, _stream_identified, _on_stream_identified);

            size_t head_len = 0;
            uint16_t payload_size = 0;
            uint8_t pt = 0;
            uint8_t frame_type = 0;
            uint8_t packet_type = 0;
            uint64_t dts = 0;

            if (base->frame_type <= kJt1078FrameB) {
                head_len = sizeof(Jt1078VideoHeader2019);
                payload_size = ntohs(base->payload_size);
                pt = base->pt;
                frame_type = base->frame_type;
                packet_type = base->packet_type;
                dts = jt1078ReadBe64(&base->timestamp);
            } else if (base->frame_type == kJt1078FrameAudio) {
                auto *audio = (Jt1078AudioHeader2019 *)_buffer.data();
                head_len = sizeof(Jt1078AudioHeader2019);
                payload_size = ntohs(audio->payload_size);
                pt = audio->pt;
                frame_type = audio->frame_type;
                packet_type = audio->packet_type;
                dts = jt1078ReadBe64(&audio->timestamp);
            } else {
                auto *other = (Jt1078OtherHeader2019 *)_buffer.data();
                head_len = sizeof(Jt1078OtherHeader2019);
                payload_size = ntohs(other->payload_size);
                frame_type = other->frame_type;
            }

            if (_buffer.size() < head_len + payload_size) {
                break;
            }

            auto payload = _buffer.data() + head_len;
            if (frame_type <= kJt1078FrameB) {
                if (_video_pt < 0) {
                    _video_pt = pt;
                }
                _video_buffer.append(payload, payload_size);
                if (packet_type == kJt1078PacketAtomic || packet_type == kJt1078PacketLast) {
                    outputVideoFrame(pt, dts, frame_type == kJt1078FrameI);
                } else if (packet_type != kJt1078PacketFirst && packet_type != kJt1078PacketMiddle) {
                    resetVideoBuffer();
                }
            } else if (frame_type == kJt1078FrameAudio) {
                static const uint8_t kAudioPrefix[] = {0x00, 0x01, 0xa0, 0x00};
                if (payload_size > 4 && payload_size <= 324
                    && (payload_size == 324 || payload_size == 164 || memcmp(payload, kAudioPrefix, 4) == 0)) {
                    outputAudioFrame(pt, payload + 4, payload_size - 4, dts);
                } else {
                    outputAudioFrame(pt, payload, payload_size, dts);
                }
            }
            _buffer.erase(0, head_len + payload_size);
            parsed = true;
            continue;
        }
        break;
    }
    return parsed;
}

} // namespace mediakit
#endif // defined(ENABLE_RTPPROXY)
