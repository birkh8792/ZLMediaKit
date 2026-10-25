/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 */

#if defined(ENABLE_JT1078)
#include "Jt1078Process.h"
#include "Common/MediaSink.h"
#include "Common/config.h"
#include "Extension/Factory.h"
#include "Extension/Track.h"
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

bool Jt1078Process::inputRtp(bool /*is_udp*/, const char *data, size_t data_len) {
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

void Jt1078Process::observeSeq(uint16_t seq, bool is_audio) {
    if (_detected_seq_mode != Jt1078SeqMode::Auto) {
        return;
    }
    if (is_audio) {
        if (_has_audio_seq) {
            uint16_t expect = (uint16_t)(_last_audio_seq + 1);
            if (seq == expect) {
                ++_separate_hits;
            }
        }
        if (_has_shared_seq) {
            uint16_t expect = (uint16_t)(_last_shared_seq + 1);
            if (seq == expect) {
                ++_shared_hits;
            }
        }
        _last_audio_seq = seq;
        _has_audio_seq = true;
    } else {
        if (_has_video_seq) {
            uint16_t expect = (uint16_t)(_last_video_seq + 1);
            if (seq == expect) {
                ++_separate_hits;
            }
        }
        if (_has_shared_seq) {
            uint16_t expect = (uint16_t)(_last_shared_seq + 1);
            if (seq == expect) {
                ++_shared_hits;
            }
        }
        _last_video_seq = seq;
        _has_video_seq = true;
    }
    _last_shared_seq = seq;
    _has_shared_seq = true;

    if (_has_audio_seq && _has_video_seq && (_shared_hits + _separate_hits) >= 16) {
        _detected_seq_mode = (_separate_hits > _shared_hits) ? Jt1078SeqMode::Separate : Jt1078SeqMode::Shared;
        InfoL << "JT1078 seq mode detected: " << (_detected_seq_mode == Jt1078SeqMode::Separate ? "separate" : "shared")
              << ", stream=" << _media_info.shortUrl();
    }
}

Track::Ptr Jt1078Process::createAudioTrack(CodecId codec) const {
    // G711/AAC/MP3/ADPCM 走 Factory 插件；其余部标音频用通用轨
    switch (codec) {
        case CodecG711A:
        case CodecG711U:
        case CodecADPCM:
            return Factory::getTrackByCodecId(codec, 8000, 1);
        case CodecAAC:
        case CodecMP3:
            return Factory::getTrackByCodecId(codec);
        default:
            return std::make_shared<AudioTrackImp>(codec, 8000, 1, 16);
    }
}

void Jt1078Process::emitFrame(const Frame::Ptr &frame) {
    if (!frame) {
        return;
    }
    if (!_feeding) {
        GET_CONFIG(uint32_t, kMaxUnreadyFrame, General::kUnreadyFrameCache);
        if (_pending_frames.size() >= kMaxUnreadyFrame) {
            _pending_frames.pop_front();
        }
        _pending_frames.emplace_back(Frame::getCacheAbleFrame(frame));
        maybeStartFeeding(false);
        return;
    }
    _interface->inputFrame(frame);
}

void Jt1078Process::maybeStartFeeding(bool force_single) {
    if (_feeding) {
        return;
    }
    GET_CONFIG(uint32_t, kMaxAddTrackMS, General::kWaitAddTrackMS);
    const bool both_ready = _video_track_added && _audio_track_added;
    // 仅一路：音轨已放弃，或等待对端轨超时（默认与 wait_add_track_ms 一致）
    const bool only_video = _video_track_added && (_audio_track_ignored || !_audio_track_added);
    const bool only_audio = _audio_track_added && !_video_track_added;
    const bool single_timeout = (only_video || only_audio) && _track_wait_ticker.elapsedTime() > kMaxAddTrackMS;
    if (!both_ready && !force_single && !single_timeout && !(_video_track_added && _audio_track_ignored)) {
        return;
    }
    _feeding = true;
    _interface->addTrackCompleted();
    for (auto &frame : _pending_frames) {
        _interface->inputFrame(frame);
    }
    _pending_frames.clear();
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
            if (!_audio_track_added && !_audio_track_ignored) {
                _track_wait_ticker.resetTime();
            }
            maybeStartFeeding(false);
        }
        return _video_track_added;
    }
    if (_audio_track_added) {
        return true;
    }
    if (_audio_track_ignored) {
        return false;
    }
    auto track = createAudioTrack(codec);
    if (!track) {
        _audio_track_ignored = true;
        maybeStartFeeding(true);
        return false;
    }
    if (!_interface->addTrack(track)) {
        // 已被 MediaSink 判 ready（例如旧逻辑/超时），不再重试以免刷屏
        WarnL << "JT1078 add audio track failed: " << getCodecName(codec) << ", stream=" << _media_info.shortUrl();
        _audio_track_ignored = true;
        maybeStartFeeding(true);
        return false;
    }
    _audio_track_added = true;
    if (!_video_track_added) {
        _track_wait_ticker.resetTime();
    }
    maybeStartFeeding(false);
    return true;
}

bool Jt1078Process::outputVideoFrame(uint8_t pt, uint64_t dts, bool /*key*/) {
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
    emitFrame(frame);
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
    emitFrame(frame);
    return true;
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

        Jt1078ParsedHeader head;
        if (!parseJt1078Header(_buffer.data(), _buffer.size(), _version, head)) {
            // 魔数对但头字段非法：跳过 1 字节继续搜
            if (_buffer.size() > 4 && isJt1078(_buffer.data(), _buffer.size())) {
                _buffer.erase(0, 1);
                continue;
            }
            break;
        }
        if (_version == Jt1078Version::Unknown) {
            _version = head.version;
        }

        if (_buffer.size() < head.header_len + head.payload_size) {
            break;
        }

        if (!_stream_identified && _on_stream_identified) {
            _stream_identified = true;
            _on_stream_identified(jt1078SimToString(head.sim, head.sim_len), head.ch);
        }

        observeSeq(head.seq, head.frame_type == kJt1078FrameAudio);

        auto payload = _buffer.data() + head.header_len;
        if (head.frame_type <= kJt1078FrameB) {
            if (_video_pt < 0) {
                _video_pt = head.pt;
            }
            _video_buffer.append(payload, head.payload_size);
            if (head.packet_type == kJt1078PacketAtomic || head.packet_type == kJt1078PacketLast) {
                outputVideoFrame(head.pt, head.timestamp, head.frame_type == kJt1078FrameI);
            } else if (head.packet_type != kJt1078PacketFirst && head.packet_type != kJt1078PacketMiddle) {
                resetVideoBuffer();
            }
        } else if (head.frame_type == kJt1078FrameAudio) {
            static const uint8_t kAudioPrefix[] = {0x00, 0x01, 0xa0, 0x00};
            if (head.payload_size > 4 && head.payload_size <= 324
                && (head.payload_size == 324 || head.payload_size == 164 || memcmp(payload, kAudioPrefix, 4) == 0)) {
                outputAudioFrame(head.pt, payload + 4, head.payload_size - 4, head.timestamp);
            } else {
                outputAudioFrame(head.pt, payload, head.payload_size, head.timestamp);
            }
        }
        // other 类型忽略
        _buffer.erase(0, head.header_len + head.payload_size);
        parsed = true;
    }
    return parsed;
}

} // namespace mediakit
#endif // defined(ENABLE_JT1078)
