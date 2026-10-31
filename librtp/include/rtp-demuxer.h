#ifndef _rtp_demuxer_h_
#define _rtp_demuxer_h_

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct rtp_demuxer_t;

/// @param[in] param rtp_demuxer_create input param
/// @param[in] packet rtp payload data
/// @param[in] bytes rtp payload length in byte
/// @param[in] timestamp rtp timestamp(relation at sample rate)
/// @param[in] flags rtp packet flags, RTP_PAYLOAD_FLAG_PACKET_xxx, see more @rtp-payload.h
/// @return 0-ok, other-error
typedef int (*rtp_demuxer_onpacket)(void* param, const void *packet, int bytes, uint32_t timestamp, int flags);

/// @param[in] jitter rtp reorder jitter(ms), e.g. 200(ms)
/// @param[in] frequency audio/video sample rate, e.g. video 90000, audio 48000
/// @param[in] payload rtp payload id, see more @rtp-profile.h
/// @param[in] encoding rtp payload encoding, see more @rtp-profile.h
struct rtp_demuxer_t* rtp_demuxer_create(int jitter, int frequency, int payload, const char* encoding, rtp_demuxer_onpacket onpkt, void* param);
int rtp_demuxer_destroy(struct rtp_demuxer_t** rtp);

/// @param[in] data a rtp/rtcp packet
/// @return >0-rtcp message, 0-ok, <0-error
int rtp_demuxer_input(struct rtp_demuxer_t* rtp, const void* data, int bytes);

/// 占位但不解包的 RTP payload type（共享 seq 的 FEC 包）。
/// 该 PT 的包照常入 jitter 队列（占 seq、计入 RR 收包），但不送 payload
/// 解码器；用于共享 seq 模式下让 FEC 包"占号无载荷"，避免被当成媒体丢包
/// 或进入 depacketizer。RFC 5109 FEC 包本就是同 SSRC 的合法 RTP 包，接收端
/// 按 PT 分流即可，不解包不改变协议行为。
/// @param[in] pt payload type；-1 或 >=128 关闭（默认关闭）
/// @return 0-ok, <0-error
int rtp_demuxer_set_skip_pt(struct rtp_demuxer_t* rtp, int pt);

/// 缺口分类器（共享 seq FEC 块图）：返回 1=媒体，2=非媒体(FEC 占位)，0=未知。
/// 仅当一段缺口全部返回 2 时推进解包器序号（占位无载荷）；否则保持上游
/// "缺号丢弃已组装 NAL"语义，避免把坏 NAL 送进解码器造成花屏。
int rtp_demuxer_set_gap_classify(struct rtp_demuxer_t* rtp,
                                 int (*fn)(void* ctx, uint16_t seq), void* ctx);

/// 缺口策略（配合 gap_classify 使用；默认 RTP_GAP_POLICY_STRICT，与上游一致）：
///   STRICT   = 缺号一律交给解包器处理（上游行为：丢弃已组装 NAL）
///   TOLERANT = 缺号一律推进序号（不丢 NAL；真实媒体丢失可能产生坏 NAL）
///   HYBRID   = 缺口含"确认媒体"时交给解包器丢弃；FEC 占位/未知则推进
#define RTP_GAP_POLICY_STRICT   0
#define RTP_GAP_POLICY_TOLERANT 1
#define RTP_GAP_POLICY_HYBRID   2
int rtp_demuxer_set_gap_policy(struct rtp_demuxer_t* rtp, int policy);

/// 运行期调整 jitter 重排阈值（ms）；上层可据实测恢复年龄自适应。
int rtp_demuxer_set_jitter(struct rtp_demuxer_t* rtp, int jitter_ms);

/// @return >0-rtcp report length, 0-don't need send rtcp
int rtp_demuxer_rtcp(struct rtp_demuxer_t* rtp, void* buf, int len);

/// @param[out] lost read lost packets by jitter
/// @param[out] late received after read
/// @param[out] misorder reorder packets
/// @param[out] duplicate exist in unread queue
void rtp_demuxer_stats(struct rtp_demuxer_t* rtp, int* lost, int* late, int* misorder, int* duplicate);

#if defined(__cplusplus)
}
#endif
#endif /* _rtp_demuxer_h_ */
