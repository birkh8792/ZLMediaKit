/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 */

#ifndef ZLMEDIAKIT_JT1078SERVER_H
#define ZLMEDIAKIT_JT1078SERVER_H

#if defined(ENABLE_JT1078)

#include "Network/Session.h"
#include "Network/TcpServer.h"
#include "Rtp/RtpProcess.h"
#include "Jt1078.h"
#include "Common/MultiMediaSourceMuxer.h"

namespace mediakit {

/**
 * JT/T 1078 固定端口代理会话（可选；rtp_proxy.1078Port!=0 时启用）
 * 收流主路径请用 openRtpServer + RtpSession 自动识别。
 * 本会话额外支持：设备 TCP 推流、HTTP GET 拉 1078 裸流。
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

private:
    ProxyMode _mode = ProxyMode::Unknown;
    std::string _buffer;
    MediaTuple _tuple;
    struct sockaddr_storage _addr {};
    RtpProcess::Ptr _process;
    toolkit::Ticker _ticker;
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

#endif // defined(ENABLE_JT1078)
#endif // ZLMEDIAKIT_JT1078SERVER_H
