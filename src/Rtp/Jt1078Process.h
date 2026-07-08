/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 */

#ifndef ZLMEDIAKIT_JT1078PROCESS_H
#define ZLMEDIAKIT_JT1078PROCESS_H

#if defined(ENABLE_RTPPROXY)

#include "Jt1078.h"
#include "ProcessInterface.h"
#include "Common/MediaSource.h"

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

private:
    bool parseBuffer();
    void resetVideoBuffer();
    bool outputVideoFrame(uint8_t pt, uint64_t dts, bool key);
    bool outputAudioFrame(uint8_t pt, const char *data, size_t len, uint64_t dts);
    bool ensureTrack(CodecId codec, TrackType type);
    size_t findHeaderPos() const;
    void skipHttpHeaderIfNeeded();

private:
    MediaInfo _media_info;
    MediaSinkInterface *_interface = nullptr;
    onStreamIdentified _on_stream_identified;
    Jt1078Version _version = Jt1078Version::Unknown;
    std::string _buffer;
    std::string _video_buffer;
    bool _video_track_added = false;
    bool _audio_track_added = false;
    bool _stream_identified = false;
    bool _skip_http_header = false;
    int _video_pt = -1;
    uint64_t _last_dts = 0;
};

} // namespace mediakit

#endif // defined(ENABLE_RTPPROXY)
#endif // ZLMEDIAKIT_JT1078PROCESS_H
