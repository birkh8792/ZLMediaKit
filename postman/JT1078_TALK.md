# JT/T 1078 语音对讲对接说明

本文说明如何在 ZLMediaKit 中对接车载 JT1078 语音对讲，接口设计参考 ABLMediaServer，与 GB28181 对讲（`startSendRtpTalk`）相互独立。

## 前置条件

- 编译时开启 `ENABLE_RTPPROXY`
- `config.ini` 中配置好 `[rtp_proxy]`，例如：

```ini
[rtp_proxy]
1078Port=1078
jtt1078Version=2016
```

- 1078 对讲回传音频建议使用 **G711A (8000Hz, PT=6)**，与部标设备兼容性最好

## 与 GB28181 对讲的区别

| 项目 | GB28181 对讲 | JT1078 对讲 |
|------|-------------|-------------|
| API | `startSendRtpTalk` | `sendJtt1078Talk` |
| 回传格式 | PS/RTP | JT1078 裸流 (0x30316364) |
| 连接方式 | 复用设备 RTP 连接 | 复用设备 1078 推流连接 |
| 流命名 | 自定义 | `{SIM}_{ch}_talk` |

## 业务流程

```
平台                         ZLMediaKit                    车载设备
  |                              |                            |
  |-- openRtpServer(KeepOpen=3) ->|                            |
  |<- 返回 port=8356 ------------|                            |
  |-- 通知设备连接 8356 -------->|                            |
  |                              |<---- TCP 推送 1078 音频 ----|
  |                              |  注册流 1078/SIM_1_talk      |
  |-- 推 G711A 到 talk/audio_1 ->|                            |
  |-- sendJtt1078Talk ---------->|                            |
  |                              |---- 1078 音频回发 --------->|
  |-- stopJtt1078Talk ---------->|  (停止回发，不断开设备)      |
```

## API 说明

### 1. 创建 1078 对讲常开端口

**URL:** `GET /index/api/openRtpServer`

| 参数 | 必填 | 说明 |
|------|------|------|
| secret | 是 | API 密钥 |
| port | 是 | 监听端口，`0` 为随机 |
| tcp_mode | 是 | 建议 `1`（TCP 被动监听） |
| jtt1078_KeepOpenPortType | 对讲时填 `3` | 0=单次, 1=实况, 2=回放, **3=对讲**, 4=子码流 |
| app | 否 | 常开端口时默认 `1078` |
| stream_id | 否 | 常开端口时可省略 |
| send_app | 否 | 自动绑定：回传音频源 app |
| send_stream_id | 否 | 自动绑定：回传音频源 stream |
| only_track | 否 | 对讲建议 `1`（仅音频） |

**示例：**

```http
GET /index/api/openRtpServer?secret=035c73f7-bb6b-4889-a715-d9eb2d1925cc&port=0&tcp_mode=1&app=1078&jtt1078_KeepOpenPortType=3&send_app=talk&send_stream_id=audio_1
```

**响应：**

```json
{
  "code": 0,
  "port": 8356
}
```

**说明：**

- `jtt1078_KeepOpenPortType >= 1` 时自动启用多路复用（multiplex），同一端口可接入多台设备
- 常开端口**不需要**调用 `closeRtpServer` 关闭
- 若同时指定 `send_app` + `send_stream_id`，且发送流就绪，会自动启动对讲

### 2. 流命名规则

| KeepOpenPortType | 流名格式 |
|------------------|----------|
| 0 / 1 | `1078/{SIM}_{channel}` |
| 2 | `1078/{SIM}_{channel}_playback` |
| **3** | **`1078/{SIM}_{channel}_talk`** |
| 4 | `1078/{SIM}_{channel}_sub` |

`SIM` 为设备 SIM 卡号 BCD 十六进制字符串（如 `138001380000`），`channel` 为逻辑通道号。

同名流已存在时，会自动追加唯一后缀。

### 3. 手动启动 1078 语音对讲

**URL:** `GET /index/api/sendJtt1078Talk`

| 参数 | 必填 | 说明 |
|------|------|------|
| secret | 是 | API 密钥 |
| app | 是 | 设备接入流 app，通常 `1078` |
| stream | 是 | 设备接入流，如 `138001380000_1_talk` |
| send_app | 是 | 待发送音频源 app |
| send_stream_id | 是 | 待发送音频源 stream |
| jt1078_version | 否 | `2013` / `2016` / `2019`，默认读配置 |

**示例：**

```http
GET /index/api/sendJtt1078Talk?secret=035c73f7-bb6b-4889-a715-d9eb2d1925cc&app=1078&stream=138001380000_1_talk&send_app=talk&send_stream_id=audio_1
```

**成功响应：**

```json
{
  "code": 0,
  "msg": "Successfully sent voice intercom audio"
}
```

**常见错误：**

| msg | 原因 |
|-----|------|
| `1078 device intercom voice stream has not been connected yet` | 设备尚未接入 |
| `The voice to be sent is not ready` | 发送音频流不存在 |
| `The connected device is not a JT1078 device` | 目标流不是 1078 接入 |
| `Voice intercom has already been sent` | 已在对讲中，不可重复调用 |

### 4. 停止 1078 语音对讲

**URL:** `GET /index/api/stopJtt1078Talk`

| 参数 | 必填 | 说明 |
|------|------|------|
| secret | 是 | API 密钥 |
| app | 是 | 设备接入流 app |
| stream | 是 | 设备接入流 |

**示例：**

```http
GET /index/api/stopJtt1078Talk?secret=035c73f7-bb6b-4889-a715-d9eb2d1925cc&app=1078&stream=138001380000_1_talk
```

停止后设备推流连接保持，可再次调用 `sendJtt1078Talk` 恢复对讲。

## 发送音频源准备

对讲音频需先推入 ZLMediaKit，常见方式：

### 方式 A：RTMP 推 G711A

```bash
ffmpeg -re -f lavfi -i "sine=frequency=440:duration=3600" \
  -acodec pcm_alaw -ar 8000 -ac 1 \
  -f flv rtmp://127.0.0.1:1935/talk/audio_1
```

### 方式 B：WebRTC 对讲

使用 WebRTC Talk 插件推流到 `talk/audio_1`，再调用 `sendJtt1078Talk` 桥接到 1078 设备。

### 方式 C：GB28181 / RTP 收流

```http
GET /index/api/openRtpServer?secret=...&port=0&tcp_mode=1&app=talk&stream_id=audio_1&only_track=1
```

## Postman 测试

导入 [`ZLMediaKit.postman_collection.json`](./ZLMediaKit.postman_collection.json)，使用以下请求（按顺序）：

1. **创建1078对讲常开端口(openRtpServer jt1078 talk)** — 获取端口
2. **获取媒体列表(getMediaList)** — 确认设备流 `1078/{SIM}_{ch}_talk` 已上线
3. （外部）向 `talk/audio_1` 推 G711A 音频
4. **开始1078语音对讲(sendJtt1078Talk)** — 启动回发
5. **停止1078语音对讲(stopJtt1078Talk)** — 停止回发

Postman 环境变量：

| 变量 | 示例值 |
|------|--------|
| ZLMediaKit_URL | `http://127.0.0.1:80` |
| ZLMediaKit_secret | `035c73f7-bb6b-4889-a715-d9eb2d1925cc` |
| defaultVhost | `__defaultVhost__` |

## 1078 回发包格式

服务器经设备原连接回发的音频包结构：

- 魔数：`0x30 0x31 0x63 0x64`
- frame_type：`0x03`（音频）
- packet_type：`0x00`（原子包）
- PT：`6`=G711A, `7`=G711U, `19`=AAC
- G711 时间戳增量：`(payload_len / 80) * 10` ms

## 联调检查清单

- [ ] `openRtpServer` 返回有效 port
- [ ] 设备 TCP 连接后 `getMediaList` 可见 `1078/xxx_talk`
- [ ] `talk/audio_1` 发送流存在且为 G711A
- [ ] `sendJtt1078Talk` 返回 `code: 0`
- [ ] 抓包确认设备连接上有 1078 格式 outbound 音频
- [ ] `stopJtt1078Talk` 后 outbound 停止，设备连接未断

## 相关配置

| 配置项 | 位置 | 说明 |
|--------|------|------|
| 1078Port | `[rtp_proxy]` | 1078 固定代理端口（设备直推 1078 + HTTP 拉流） |
| jtt1078Version | `[rtp_proxy]` | 协议版本 2013/2016/2019 |
| secret | `[api]` | HTTP API 鉴权密钥 |
