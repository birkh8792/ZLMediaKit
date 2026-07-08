/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 */

#ifndef ZLMEDIAKIT_JT1078SERVER_H
#define ZLMEDIAKIT_JT1078SERVER_H

#if defined(ENABLE_RTPPROXY)

#include "Network/Session.h"
#include "Network/TcpServer.h"
#include "RtpProcess.h"
#include "Jt1078.h"
#include "Common/MultiMediaSourceMuxer.h"

namespace mediakit {

/**
 * JT/T 1078 固定端口代理会话
 * 支持设备 TCP 推流(0x30316364) 与 HTTP GET 拉取 1078 码流
 */
class Jt1078ProxySession : public toolkit::Session {
public:
    Jt1078ProxySession(const toolkit::Socket::Ptr &sock);
    ~Jt1078ProxySession() override;

    void onRecv(const toolkit::Buffer::Ptr &buf) override;
    void onError(const toolkit::SockException &err) override;
    void onManager() override;

private:
    enum class ProxyMode { Unknown, Push, Pull };

    void handlePush(const char *data, size_t len);
    bool tryDetectMode();
    bool tryStartHttpPull();
    void sendHttpResponse(int code, const std::string &msg, bool with_body = false);
    void attachMediaReader();

private:
    ProxyMode _mode = ProxyMode::Unknown;
    std::string _buffer;
    MediaTuple _tuple;
    struct sockaddr_storage _addr {};
    RtpProcess::Ptr _process;
    toolkit::Ticker _ticker;
    std::string _pull_stream;
    bool _pull_started = false;
    MultiMediaSourceMuxer::RingType::RingReader::Ptr _pull_reader;
};

class Jt1078Server {
public:
    using Ptr = std::shared_ptr<Jt1078Server>;

    void start(uint16_t port, const std::string &local_ip = "::");

private:
    toolkit::TcpServer::Ptr _tcp_server;
};

} // namespace mediakit

#endif // defined(ENABLE_RTPPROXY)
#endif // ZLMEDIAKIT_JT1078SERVER_H
