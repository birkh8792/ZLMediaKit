/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * JT/T 1078 video/audio stream protocol definitions.
 */

#ifndef ZLMEDIAKIT_JT1078_H
#define ZLMEDIAKIT_JT1078_H

#if defined(ENABLE_RTPPROXY)

#include <cstdint>
#include <cstring>
#include <string>
#include "Extension/Frame.h"

namespace mediakit {

static constexpr char kJt1078AppName[] = "1078";
static constexpr size_t kJt1078MaxPayload = 950;
static constexpr size_t kJt1078CacheSize = 2 * 1024 * 1024;

enum class Jt1078Version {
    Unknown = 0,
    V2013 = 1, // 2013/2016
    V2019 = 2,
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

#pragma pack(push, 1)

struct Jt1078VideoHeader2013 {
    uint8_t head[4];
    uint8_t cc : 4;
    uint8_t x : 1;
    uint8_t p : 1;
    uint8_t v : 2;
    uint8_t pt : 7;
    uint8_t m : 1;
    uint16_t seq;
    uint8_t sim[6];
    uint8_t ch;
    uint8_t packet_type : 4;
    uint8_t frame_type : 4;
    int64_t timestamp;
    uint16_t i_frame_interval;
    uint16_t frame_interval;
    uint16_t payload_size;
};

struct Jt1078AudioHeader2013 {
    uint8_t head[4];
    uint8_t cc : 4;
    uint8_t x : 1;
    uint8_t p : 1;
    uint8_t v : 2;
    uint8_t pt : 7;
    uint8_t m : 1;
    uint16_t seq;
    uint8_t sim[6];
    uint8_t ch;
    uint8_t packet_type : 4;
    uint8_t frame_type : 4;
    int64_t timestamp;
    uint16_t payload_size;
};

struct Jt1078OtherHeader2013 {
    uint8_t head[4];
    uint8_t cc : 4;
    uint8_t x : 1;
    uint8_t p : 1;
    uint8_t v : 2;
    uint8_t pt : 7;
    uint8_t m : 1;
    uint16_t seq;
    uint8_t sim[6];
    uint8_t ch;
    uint8_t packet_type : 4;
    uint8_t frame_type : 4;
    uint16_t payload_size;
};

struct Jt1078VideoHeader2019 {
    uint8_t head[4];
    uint8_t cc : 4;
    uint8_t x : 1;
    uint8_t p : 1;
    uint8_t v : 2;
    uint8_t pt : 7;
    uint8_t m : 1;
    uint16_t seq;
    uint8_t sim[10];
    uint8_t ch;
    uint8_t packet_type : 4;
    uint8_t frame_type : 4;
    int64_t timestamp;
    uint16_t i_frame_interval;
    uint16_t frame_interval;
    uint16_t payload_size;
};

struct Jt1078AudioHeader2019 {
    uint8_t head[4];
    uint8_t cc : 4;
    uint8_t x : 1;
    uint8_t p : 1;
    uint8_t v : 2;
    uint8_t pt : 7;
    uint8_t m : 1;
    uint16_t seq;
    uint8_t sim[10];
    uint8_t ch;
    uint8_t packet_type : 4;
    uint8_t frame_type : 4;
    int64_t timestamp;
    uint16_t payload_size;
};

struct Jt1078OtherHeader2019 {
    uint8_t head[4];
    uint8_t cc : 4;
    uint8_t x : 1;
    uint8_t p : 1;
    uint8_t v : 2;
    uint8_t pt : 7;
    uint8_t m : 1;
    uint16_t seq;
    uint8_t sim[10];
    uint8_t ch;
    uint8_t packet_type : 4;
    uint8_t frame_type : 4;
    uint16_t payload_size;
};

#pragma pack(pop)

bool isJt1078(const char *data, size_t len);
Jt1078Version detectJt1078Version(const char *data, size_t len);
std::string jt1078SimToString(const uint8_t *sim, size_t sim_len);
CodecId jt1078PtToCodec(uint8_t pt, TrackType &type);
uint64_t jt1078ReadBe64(const void *data);
uint64_t jt1078WriteBe64(uint64_t val, void *data);

} // namespace mediakit

#endif // defined(ENABLE_RTPPROXY)
#endif // ZLMEDIAKIT_JT1078_H
