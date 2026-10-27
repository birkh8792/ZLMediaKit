/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 */

#ifndef ZLMEDIAKIT_JT1078PROCESS_H
#define ZLMEDIAKIT_JT1078PROCESS_H

#if defined(ENABLE_JT1078)

#include "Jt1078.h"
#include "Rtp/ProcessInterface.h"
#include "Common/MediaSource.h"
#include "Common/Stamp.h"
#include "Util/TimeTicker.h"
#include <list>
#include <unordered_map>

namespace mediakit {

/**
 * JT/T 1078 demux
 *
 * 拼帧骨架对齐 https://gitee.com/lvxingjin/ZLM ：
 *   Atomic 直接出帧；First 清空缓存；Middle 追加；Last 追加后出帧；
 *   缓存 key = channel|PT|frame_type。
 *
 * 花屏/卡顿/无法解析增强：
 *   1) 解复用：下一包头已到却非魔数 → 伪同步，避免吞包头进 ES；
 *   2) 帧内按学习到的序号步长连续（常见 1，部分车载 2），步长跨帧复用；
 *   3) 兼容终端 seq 低字节溢出未进位（含 First：1024 写成 768）；
 *   4) 同帧 First 重传忽略；音频 seq 回退丢弃 + Stamp 校正 DTS。
 */
class Jt1078Process : public ProcessInterface {
public:
    using Ptr = std::shared_ptr<Jt1078Process>;
    using onStreamIdentified = std::function<void(const std::string &sim, uint8_t channel)>;

    Jt1078Process(const MediaInfo &media_info, MediaSinkInterface *sink);

    bool inputRtp(bool is_udp, const char *data, size_t data_len) override;
    void flush() override;

    void setOnStreamIdentified(onStreamIdentified cb);
    void setVersion(Jt1078Version version);
    Jt1078SeqMode getDetectedSeqMode() const { return _detected_seq_mode; }

private:
    struct VideoCache {
        std::string buffer;
        uint8_t pt = 98;
        uint64_t stamp = 0;
        bool key = false;
        bool expect_valid = false;
        uint16_t last_seq = 0;  // 已拼入的最后分片序号
        uint16_t seq_step = 0;  // 0=未学习；常见 1，部分车载视频为 2
        uint16_t start_seq = 0;
    };

    bool parseBuffer();
    void resetVideoCaches();
    static uint32_t videoCacheKey(uint8_t ch, uint8_t pt, uint8_t frame_type);
    uint64_t getDts(uint64_t stamp, bool is_audio);
    bool acceptAudioSeq(uint16_t seq);
    bool outputVideoFrame(uint8_t pt, uint64_t stamp, bool key, std::string &buffer);
    bool outputAudioFrame(uint8_t pt, const char *data, size_t len, uint64_t stamp, uint16_t seq);
    void handleVideoPayload(const Jt1078ParsedHeader &head, const char *payload);
    // 返回 false 表示该路缓存已因缺口废弃
    bool appendVideoSlice(VideoCache &cache, uint16_t seq, const char *payload, uint16_t payload_size);
    bool ensureTrack(CodecId codec, TrackType type);
    void maybeStartFeeding(bool force_single = false);
    void emitFrame(const Frame::Ptr &frame);
    Track::Ptr createAudioTrack(CodecId codec) const;
    size_t findHeaderPos() const;
    void skipHttpHeaderIfNeeded();
    void observeSeq(uint16_t seq, bool is_audio);

private:
    MediaInfo _media_info;
    MediaSinkInterface *_interface = nullptr;
    onStreamIdentified _on_stream_identified;
    Jt1078Version _version = Jt1078Version::Unknown;
    Jt1078SeqMode _detected_seq_mode = Jt1078SeqMode::Auto;
    std::string _buffer;
    std::unordered_map<uint32_t, VideoCache> _video_caches;
    std::string _last_emitted_video;
    bool _video_track_added = false;
    bool _audio_track_added = false;
    bool _audio_track_ignored = false;
    bool _feeding = false;
    bool _stream_identified = false;
    bool _skip_http_header = false;
    int _video_pt = -1;
    uint64_t _last_dts = 0;
    Stamp _video_stamp;
    Stamp _audio_stamp;
    bool _audio_synced_to_video = false;
    bool _has_audio_out_seq = false;
    uint16_t _last_audio_out_seq = 0;
    uint16_t _video_seq_step = 0; // 跨帧复用的视频分片步长（1 或 2）
    toolkit::Ticker _track_wait_ticker;
    std::list<Frame::Ptr> _pending_frames;

    bool _has_video_seq = false;
    bool _has_audio_seq = false;
    uint16_t _last_video_seq = 0;
    uint16_t _last_audio_seq = 0;
    uint16_t _last_shared_seq = 0;
    bool _has_shared_seq = false;
    int _shared_hits = 0;
    int _separate_hits = 0;
};

} // namespace mediakit

#endif // defined(ENABLE_JT1078)
#endif // ZLMEDIAKIT_JT1078PROCESS_H
