/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 */

#if defined(ENABLE_RTPPROXY)
#include "Jt1078Server.h"
#include "Common/config.h"
#include "Util/logger.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

Jt1078ProxySession::Jt1078ProxySession(const Socket::Ptr &sock) : Session(sock) {
    socklen_t addr_len = sizeof(_addr);
    getpeername(sock->rawFD(), (struct sockaddr *)&_addr, &addr_len);
    _tuple.vhost = DEFAULT_VHOST;
    _tuple.app = kJt1078AppName;
}

Jt1078ProxySession::~Jt1078ProxySession() = default;

void Jt1078ProxySession::onManager() {
    if (_mode == ProxyMode::Unknown && _ticker.createdTime() > 10 * 1000) {
        shutdown(SockException(Err_timeout, "jt1078 proxy mode detect timeout"));
    }
}

void Jt1078ProxySession::onError(const SockException &err) {
    if (_process) {
        _process->onDetach(err);
    }
    WarnP(this) << "jt1078 proxy closed: " << err;
}

void Jt1078ProxySession::sendHttpResponse(int code, const string &msg, bool with_body) {
    string response = "HTTP/1.1 " + to_string(code) + " " + msg + "\r\n";
    response += "Connection: keep-alive\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    if (with_body) {
        response += "Content-Type: application/octet-stream\r\n";
    }
    response += "\r\n";
    getSock()->send(response);
}

bool Jt1078ProxySession::tryDetectMode() {
    if (_buffer.size() < 4) {
        return false;
    }
    if (isJt1078(_buffer.data(), _buffer.size())) {
        _mode = ProxyMode::Push;
        InfoP(this) << "jt1078 push mode detected";
        return true;
    }
    if (_buffer.compare(0, 4, "GET ") == 0) {
        _mode = ProxyMode::Pull;
        InfoP(this) << "jt1078 http pull mode detected";
        return true;
    }
    shutdown(SockException(Err_other, "invalid jt1078 proxy data"));
    return false;
}

bool Jt1078ProxySession::tryStartHttpPull() {
    if (_pull_started) {
        return true;
    }
    auto header_end = _buffer.find("\r\n\r\n");
    if (header_end == string::npos) {
        return false;
    }

    auto line_end = _buffer.find("\r\n");
    if (line_end == string::npos) {
        return false;
    }
    auto request_line = _buffer.substr(0, line_end);
    auto pos1 = request_line.find(' ');
    auto pos2 = request_line.rfind(' ');
    if (pos1 == string::npos || pos2 == string::npos || pos2 <= pos1 + 1) {
        sendHttpResponse(400, "Bad Request");
        shutdown(SockException(Err_other, "bad http request"));
        return false;
    }

    auto path = request_line.substr(pos1 + 1, pos2 - pos1 - 1);
    auto qpos = path.find('?');
    if (qpos != string::npos) {
        path = path.substr(0, qpos);
    }
    if (!path.empty() && path[0] == '/') {
        path = path.substr(1);
    }

    string app = kJt1078AppName;
    string stream = path;
    auto slash = path.find('/');
    if (slash != string::npos) {
        app = path.substr(0, slash);
        stream = path.substr(slash + 1);
    }
    if (stream.empty()) {
        sendHttpResponse(404, "Not Found");
        shutdown(SockException(Err_other, "empty stream"));
        return false;
    }

    auto src = MediaSource::find(DEFAULT_VHOST, app, stream);
    if (!src) {
        sendHttpResponse(404, "Not Found");
        shutdown(SockException(Err_other, "stream not found"));
        return false;
    }

    string sim = "000000000000";
    uint8_t channel = 1;
    auto under = stream.rfind('_');
    if (under != string::npos) {
        sim = stream.substr(0, under);
        channel = (uint8_t)atoi(stream.substr(under + 1).data());
    }

    sendHttpResponse(200, "OK", true);
    _pull_started = true;

    GET_CONFIG(string, version_str, RtpProxy::kJt1078Version);
    auto muxer = src->getMuxer();
    if (!muxer) {
        shutdown(SockException(Err_other, "stream muxer not found"));
        return false;
    }
    _pull_reader = muxer->startSendJt1078OnSocket(getSock(), sim, channel, version_str);
    return true;
}

void Jt1078ProxySession::attachMediaReader() {}

void Jt1078ProxySession::handlePush(const char *data, size_t len) {
    if (!_process) {
        _process = RtpProcess::createProcess(_tuple);
        weak_ptr<Jt1078ProxySession> weak_self = static_pointer_cast<Jt1078ProxySession>(shared_from_this());
        _process->setOnDetach([weak_self](const SockException &ex) {
            if (auto strong_self = weak_self.lock()) {
                strong_self->safeShutdown(ex);
            }
        });
    }
    _process->inputJt1078(false, getSock(), data, len, (struct sockaddr *)&_addr);
}

void Jt1078ProxySession::onRecv(const Buffer::Ptr &buf) {
    _ticker.resetTime();
    if (_mode == ProxyMode::Push) {
        handlePush(buf->data(), buf->size());
        return;
    }
    if (_mode == ProxyMode::Pull) {
        return;
    }

    _buffer.append(buf->data(), buf->size());
    if (_buffer.size() > 256 * 1024) {
        shutdown(SockException(Err_other, "jt1078 proxy header too large"));
        return;
    }
    if (!tryDetectMode()) {
        return;
    }

    if (_mode == ProxyMode::Push) {
        handlePush(_buffer.data(), _buffer.size());
        _buffer.clear();
    } else if (_mode == ProxyMode::Pull) {
        tryStartHttpPull();
    }
}

void Jt1078Server::start(uint16_t port, const string &local_ip) {
    if (!port) {
        return;
    }
    _tcp_server = std::make_shared<TcpServer>();
    _tcp_server->start<Jt1078ProxySession>(port, local_ip);
    InfoL << "JT1078 proxy server started on " << local_ip << ":" << port;
}

} // namespace mediakit
#endif
