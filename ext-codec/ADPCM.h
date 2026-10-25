/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_ADPCM_H
#define ZLMEDIAKIT_ADPCM_H

#include "Extension/Frame.h"
#include "Extension/Track.h"

namespace mediakit {

/**
 * ADPCM 音频通道（JT/T 1078 PT=26 等场景透传）
 * 无专用 RTP 封包规范时走 CommonRtp；RTMP 使用 FLV SoundFormat=ADPCM
 */
class ADPCMTrack : public AudioTrackImp {
public:
    using Ptr = std::shared_ptr<ADPCMTrack>;
    ADPCMTrack(int sample_rate = 8000, int channels = 1, int sample_bit = 16)
        : AudioTrackImp(CodecADPCM, sample_rate ? sample_rate : 8000, channels ? channels : 1, sample_bit ? sample_bit : 16) {}

private:
    Track::Ptr clone() const override { return std::make_shared<ADPCMTrack>(*this); }
};

} // namespace mediakit
#endif // ZLMEDIAKIT_ADPCM_H
