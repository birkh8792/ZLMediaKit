/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 */

#ifndef ZLMEDIAKIT_JT1078ENCODER_H
#define ZLMEDIAKIT_JT1078ENCODER_H

#if defined(ENABLE_RTPPROXY)

#include "Jt1078.h"
#include "Common/MediaSink.h"
#include "RtpCache.h"

namespace mediakit {

class Jt1078EncoderImp : public MediaSinkInterface {
public:
    Jt1078EncoderImp(Jt1078Version version, const std::string &sim, uint8_t channel, uint16_t fps = 25);

    bool addTrack(const Track::Ptr &track) override;
    void resetTracks() override;
    bool inputFrame(const Frame::Ptr &frame) override;

protected:
    virtual void onPacket(toolkit::Buffer::Ptr packet) = 0;

private:
    void sendVideoFrame(const Frame::Ptr &frame);
    void sendAudioFrame(const Frame::Ptr &frame);
    uint8_t getVideoPt(CodecId codec) const;
    uint8_t getAudioPt(CodecId codec) const;
    bool isIFrame(const Frame::Ptr &frame) const;

private:
    Jt1078Version _version = Jt1078Version::V2013;
    std::string _sim;
    uint8_t _channel = 1;
    uint16_t _fps = 25;
    uint16_t _seq = 0;
    uint64_t _video_ts = 0;
    uint64_t _audio_ts = 0;
    int _p_frame_count = 0;
    CodecId _video_codec = CodecInvalid;
    CodecId _audio_codec = CodecInvalid;
    uint8_t _sim_bytes[10] = {0};
    size_t _sim_len = 6;
};

class RtpCacheJt1078 : public RtpCache, public Jt1078EncoderImp {
public:
    RtpCacheJt1078(onFlushed cb, Jt1078Version version, const std::string &sim, uint8_t channel, uint16_t fps = 25);

    bool inputFrame(const Frame::Ptr &frame) override;
    void flush() override;

protected:
    void onPacket(toolkit::Buffer::Ptr packet) override;
};

} // namespace mediakit

#endif // defined(ENABLE_RTPPROXY)
#endif // ZLMEDIAKIT_JT1078ENCODER_H
