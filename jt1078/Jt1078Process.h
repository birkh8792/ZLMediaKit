/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 */

#ifndef ZLMEDIAKIT_JT1078PROCESS_H
#define ZLMEDIAKIT_JT1078PROCESS_H

#if defined(ENABLE_JT1078)

#include "Jt1078.h"
#include "Rtp/ProcessInterface.h"
#include "Common/MediaSource.h"
#include "Util/TimeTicker.h"
#include <list>

namespace mediakit {

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
    bool parseBuffer();
    void resetVideoBuffer();
    bool outputVideoFrame(uint8_t pt, uint64_t dts, bool key);
    bool outputAudioFrame(uint8_t pt, const char *data, size_t len, uint64_t dts);
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
    std::string _video_buffer;
    bool _video_track_added = false;
    bool _audio_track_added = false;
    bool _audio_track_ignored = false;
    bool _feeding = false;
    bool _stream_identified = false;
    bool _skip_http_header = false;
    int _video_pt = -1;
    uint64_t _last_dts = 0;
    toolkit::Ticker _track_wait_ticker;
    std::list<Frame::Ptr> _pending_frames;

    // seq 自适应检测
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
