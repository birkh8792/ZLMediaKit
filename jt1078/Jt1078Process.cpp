/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * 拼帧骨架对齐 https://gitee.com/lvxingjin/ZLM ；
 * 解复用：下一包头已到却非魔数 → 伪同步，硬吃会吞包头进 ES；
 * 拼帧：seq 连续校验；同帧 First 重传忽略（重置会假缺口丢 P 帧致参考花屏）。
 */

#if defined(ENABLE_JT1078)
#include "Jt1078Process.h"
#include "Common/MediaSink.h"
#include "Common/config.h"
#include "Extension/Factory.h"
#include "Extension/Track.h"
#include "Util/logger.h"
#include "Util/util.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

static bool findStartCode(const char *data, size_t len, size_t &prefix) {
    if (len >= 4 && (uint8_t)data[0] == 0 && (uint8_t)data[1] == 0 && (uint8_t)data[2] == 0 && (uint8_t)data[3] == 1) {
        prefix = 4;
        return true;
    }
    if (len >= 3 && (uint8_t)data[0] == 0 && (uint8_t)data[1] == 0 && (uint8_t)data[2] == 1) {
        prefix = 3;
        return true;
    }
    return false;
}

static int16_t seqDiff(uint16_t got, uint16_t expect) {
    return (int16_t)(got - expect);
}

Jt1078Process::Jt1078Process(const MediaInfo &media_info, MediaSinkInterface *sink) {
    _media_info = media_info;
    _interface = sink;
    _buffer.reserve(64 * 1024);
}

void Jt1078Process::setOnStreamIdentified(onStreamIdentified cb) {
    _on_stream_identified = std::move(cb);
}

void Jt1078Process::setVersion(Jt1078Version version) {
    _version = version;
}

void Jt1078Process::flush() {
    resetVideoCaches();
}

void Jt1078Process::resetVideoCaches() {
    _video_caches.clear();
}

uint32_t Jt1078Process::videoCacheKey(uint8_t ch, uint8_t pt, uint8_t frame_type) {
    return ((uint32_t)ch << 24) | ((uint32_t)pt << 16) | ((uint32_t)frame_type << 8);
}

uint64_t Jt1078Process::getDts(uint64_t stamp) {
    if (!stamp) {
        return getCurrentMillisecond();
    }
    if (!_stamp_base) {
        _stamp_base = stamp;
        _dts_base = getCurrentMillisecond();
    }
    return _dts_base + stamp - _stamp_base;
}

bool Jt1078Process::inputRtp(bool /*is_udp*/, const char *data, size_t data_len) {
    if (!data || !data_len) {
        return false;
    }
    _buffer.append(data, data_len);
    if (_buffer.size() > kJt1078CacheSize) {
        WarnL << "JT1078 buffer overflow, clear cache: " << _media_info.shortUrl();
        _buffer.clear();
        resetVideoCaches();
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

bool Jt1078Process::appendVideoSlice(VideoCache &cache, uint16_t seq, const char *payload, uint16_t payload_size) {
    if (!cache.expect_valid) {
        return false;
    }
    const auto diff = seqDiff(seq, cache.expect_seq);
    if (diff == 0) {
        cache.buffer.append(payload, payload_size);
        cache.expect_seq = (uint16_t)(seq + 1);
        return true;
    }
    if (diff < 0) {
        // 旧片/精确重传：丢弃，继续等期望序号
        return true;
    }
    // 向前缺口：本帧已不可恢复，整帧丢弃。硬拼会把非法 NAL 送进解码器导致花屏。
    return false;
}

void Jt1078Process::handleVideoPayload(const Jt1078ParsedHeader &head, const char *payload) {
    if (_video_pt < 0) {
        _video_pt = head.pt;
    }
    const bool is_key = (head.frame_type == kJt1078FrameI);
    const auto key = videoCacheKey(head.ch, head.pt, head.frame_type);

    switch (head.packet_type) {
        case kJt1078PacketAtomic: {
            string buf;
            buf.assign(payload, head.payload_size);
            outputVideoFrame(head.pt, head.timestamp, is_key, buf);
            break;
        }
        case kJt1078PacketFirst: {
            auto &cache = _video_caches[key];
            // 同帧 First 重传：忽略，保留已拼分片。
            // 设备常见模式 First, Mid, First(重传), Mid...；若此处重置，
            // expect 回退会把后续合法片判成缺口并丢整帧，GOP 内参考花屏。
            if (cache.expect_valid && cache.start_seq == head.seq && cache.stamp == head.timestamp) {
                break;
            }
            // 新 First：清空该路缓存并建立期望序号（对齐 ZLM）
            cache.buffer.assign(payload, head.payload_size);
            cache.pt = head.pt;
            cache.stamp = head.timestamp;
            cache.key = is_key;
            cache.start_seq = head.seq;
            cache.expect_seq = (uint16_t)(head.seq + 1);
            cache.expect_valid = true;
            break;
        }
        case kJt1078PacketMiddle: {
            auto it = _video_caches.find(key);
            if (it == _video_caches.end() || !it->second.expect_valid) {
                break;
            }
            if (!appendVideoSlice(it->second, head.seq, payload, head.payload_size)) {
                // 缺口：丢弃本帧，不输出坏 ES（下一 First 再开新帧）
                _video_caches.erase(it);
            }
            break;
        }
        case kJt1078PacketLast: {
            auto it = _video_caches.find(key);
            if (it == _video_caches.end() || !it->second.expect_valid) {
                break;
            }
            if (!appendVideoSlice(it->second, head.seq, payload, head.payload_size)) {
                _video_caches.erase(it);
                break;
            }
            outputVideoFrame(head.pt, head.timestamp, is_key || it->second.key, it->second.buffer);
            _video_caches.erase(key);
            break;
        }
        default:
            _video_caches.erase(key);
            break;
    }
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

bool Jt1078Process::outputVideoFrame(uint8_t pt, uint64_t stamp, bool /*key*/, string &buffer) {
    if (buffer.empty()) {
        return false;
    }
    if (!_last_emitted_video.empty() && _last_emitted_video == buffer) {
        buffer.clear();
        return false;
    }
    TrackType track_type = TrackInvalid;
    auto codec = jt1078PtToCodec(pt, track_type);
    if (codec == CodecInvalid || !ensureTrack(codec, track_type)) {
        buffer.clear();
        return false;
    }

    const uint64_t dts = getDts(stamp);
    _last_dts = dts;

    Frame::Ptr frame;
    size_t prefix = 0;
    if ((codec == CodecH264 || codec == CodecH265) && !findStartCode(buffer.data(), buffer.size(), prefix)) {
        string payload;
        payload.reserve(4 + buffer.size());
        payload.append("\x00\x00\x00\x01", 4);
        payload.append(buffer);
        frame = Factory::getFrameFromPtr(codec, payload.data(), payload.size(), dts, dts);
        frame = Frame::getCacheAbleFrame(frame);
    } else {
        frame = Factory::getFrameFromPtr(codec, buffer.data(), buffer.size(), dts, dts);
    }
    emitFrame(frame);
    _last_emitted_video.swap(buffer);
    buffer.clear();
    return true;
}

bool Jt1078Process::outputAudioFrame(uint8_t pt, const char *data, size_t len, uint64_t stamp) {
    if (!data || !len) {
        return false;
    }
    TrackType track_type = TrackInvalid;
    auto codec = jt1078PtToCodec(pt, track_type);
    if (codec == CodecInvalid || !ensureTrack(codec, track_type)) {
        return false;
    }
    const uint64_t dts = getDts(stamp);
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
            if (_buffer.size() > 4 && isJt1078(_buffer.data(), _buffer.size())) {
                _buffer.erase(0, 1);
                continue;
            }
            break;
        }
        if (_version == Jt1078Version::Unknown) {
            _version = head.version;
        }

        const size_t packet_size = head.header_len + head.payload_size;
        if (_buffer.size() < packet_size) {
            break;
        }
        // 包边界校验：缓冲里若还有后续数据，切完本包后必须落到下一包魔数。
        // 伪同步头会把后续包头吞进 ES，是 dump 实测花屏的主因。
        if (_buffer.size() > packet_size) {
            if (_buffer.size() < packet_size + 4) {
                // 后续不足一个魔数，等更多 TCP 数据再确认边界
                break;
            }
            if (!isJt1078(_buffer.data() + packet_size, 4)) {
                _buffer.erase(0, 1);
                continue;
            }
        }

        if (!_stream_identified && _on_stream_identified) {
            _stream_identified = true;
            _on_stream_identified(jt1078SimToString(head.sim, head.sim_len), head.ch);
        }

        observeSeq(head.seq, head.frame_type == kJt1078FrameAudio);

        auto payload = _buffer.data() + head.header_len;
        if (head.frame_type <= kJt1078FrameB) {
            handleVideoPayload(head, payload);
        } else if (head.frame_type == kJt1078FrameAudio) {
            static const uint8_t kAudioPrefix[] = {0x00, 0x01, 0xa0, 0x00};
            if (head.payload_size > 4 && head.payload_size <= 324
                && (head.payload_size == 324 || head.payload_size == 164 || memcmp(payload, kAudioPrefix, 4) == 0)) {
                outputAudioFrame(head.pt, payload + 4, head.payload_size - 4, head.timestamp);
            } else {
                outputAudioFrame(head.pt, payload, head.payload_size, head.timestamp);
            }
        }
        _buffer.erase(0, packet_size);
        parsed = true;
    }
    return parsed;
}

} // namespace mediakit
#endif // defined(ENABLE_JT1078)
