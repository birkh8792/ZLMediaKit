/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * JT/T 1078 video/audio stream protocol definitions.
 */

#ifndef ZLMEDIAKIT_JT1078_H
#define ZLMEDIAKIT_JT1078_H

#if defined(ENABLE_JT1078)

#include <cstdint>
#include <cstring>
#include <string>
#include "Extension/Frame.h"

namespace mediakit {

static constexpr char kJt1078AppName[] = "1078";
static constexpr size_t kJt1078MaxPayload = 950;
static constexpr size_t kJt1078CacheSize = 2 * 1024 * 1024;

// 固定头长度（显式布局，不依赖 bitfield 端序）
static constexpr size_t kJt1078VideoHead2013 = 30;
static constexpr size_t kJt1078AudioHead2013 = 26;
static constexpr size_t kJt1078OtherHead2013 = 18;
static constexpr size_t kJt1078VideoHead2019 = 34;
static constexpr size_t kJt1078AudioHead2019 = 30;
static constexpr size_t kJt1078OtherHead2019 = 22;

enum class Jt1078Version {
    Unknown = 0,
    V2013 = 1, // 2013/2016，SIM 6 字节
    V2019 = 2, // SIM 10 字节
};

enum class Jt1078SeqMode {
    Auto = 0,     // 发端默认 shared；收端自适应检测
    Shared = 1,   // 音视频共用一个 seq
    Separate = 2, // 音视频各自独立 seq
};

enum Jt1078FrameType {
    kJt1078FrameI = 0,
    kJt1078FrameP = 1,
    kJt1078FrameB = 2,
    kJt1078FrameAudio = 3,
    kJt1078FrameOther = 4,
};

enum Jt1078PacketType {
    kJt1078PacketAtomic = 0,
    kJt1078PacketFirst = 1,
    kJt1078PacketLast = 2,
    kJt1078PacketMiddle = 3,
};

// openRtpServer: jtt1078_stream_type（不含对讲）
enum Jt1078StreamType {
    kJt1078StreamOnce = 0,     // 单次实况
    kJt1078StreamLive = 1,     // 常开实况
    kJt1078StreamPlayback = 2, // 回放
    kJt1078StreamSub = 4,      // 子码流
};

struct Jt1078ParsedHeader {
    Jt1078Version version = Jt1078Version::Unknown;
    uint8_t v = 0;
    uint8_t p = 0;
    uint8_t x = 0;
    uint8_t cc = 0;
    uint8_t m = 0;
    uint8_t pt = 0;
    uint16_t seq = 0;
    uint8_t sim[10] = {0};
    size_t sim_len = 0;
    uint8_t ch = 0;
    uint8_t frame_type = 0;
    uint8_t packet_type = 0;
    uint64_t timestamp = 0;
    uint16_t i_frame_interval = 0;
    uint16_t frame_interval = 0;
    uint16_t payload_size = 0;
    size_t header_len = 0;
};

bool isJt1078(const char *data, size_t len);
Jt1078Version detectJt1078Version(const char *data, size_t len);
bool parseJt1078Header(const char *data, size_t len, Jt1078Version hint, Jt1078ParsedHeader &out);
std::string jt1078SimToString(const uint8_t *sim, size_t sim_len);
void jt1078ParseSimBcd(const std::string &sim, uint8_t *out, size_t out_len, size_t &written);
CodecId jt1078PtToCodec(uint8_t pt, TrackType &type);
uint8_t jt1078CodecToPt(CodecId codec, TrackType type);
uint64_t jt1078ReadBe16(const void *data);
uint64_t jt1078ReadBe64(const void *data);
void jt1078WriteBe16(uint16_t val, void *data);
void jt1078WriteBe64(uint64_t val, void *data);
Jt1078Version jt1078VersionFromString(const std::string &version);
Jt1078SeqMode jt1078SeqModeFromString(const std::string &mode);
std::string jt1078StreamSuffix(int stream_type);

} // namespace mediakit

#endif // defined(ENABLE_JT1078)
#endif // ZLMEDIAKIT_JT1078_H
