/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "ADPCM.h"
#include "Extension/Factory.h"
#include "Extension/CommonRtp.h"
#include "Extension/CommonRtmp.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

namespace {

CodecId getCodec() {
    return CodecADPCM;
}

Track::Ptr getTrackByCodecId(int sample_rate, int channels, int sample_bit) {
    return std::make_shared<ADPCMTrack>(sample_rate, channels, sample_bit);
}

Track::Ptr getTrackBySdp(const SdpTrack::Ptr &track) {
    return std::make_shared<ADPCMTrack>(track->_samplerate, track->_channel, 16);
}

RtpCodec::Ptr getRtpEncoderByCodecId(uint8_t /*pt*/) {
    return std::make_shared<CommonRtpEncoder>();
}

RtpCodec::Ptr getRtpDecoderByCodecId() {
    return std::make_shared<CommonRtpDecoder>(CodecADPCM);
}

RtmpCodec::Ptr getRtmpEncoderByTrack(const Track::Ptr &track) {
    return std::make_shared<CommonRtmpEncoder>(track);
}

RtmpCodec::Ptr getRtmpDecoderByTrack(const Track::Ptr &track) {
    return std::make_shared<CommonRtmpDecoder>(track);
}

Frame::Ptr getFrameFromPtr(const char *data, size_t bytes, uint64_t dts, uint64_t pts) {
    return std::make_shared<FrameFromPtr>(CodecADPCM, (char *)data, bytes, dts, pts);
}

} // namespace

CodecPlugin adpcm_plugin = { getCodec,
                             getTrackByCodecId,
                             getTrackBySdp,
                             getRtpEncoderByCodecId,
                             getRtpDecoderByCodecId,
                             getRtmpEncoderByTrack,
                             getRtmpDecoderByTrack,
                             getFrameFromPtr };

} // namespace mediakit
