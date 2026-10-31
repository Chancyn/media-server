#ifndef _rtp_payload_h_
#define _rtp_payload_h_

// https://en.wikipedia.org/wiki/RTP_audio_video_profile

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// RTP packet lost(miss packet before this frame)
#define RTP_PAYLOAD_FLAG_PACKET_LOST	0x0100 // some packets lost before the packet
#define RTP_PAYLOAD_FLAG_PACKET_CORRUPT 0x0200 // the packet data is corrupt

struct rtp_payload_t
{
	void* (*alloc)(void* param, int bytes);
	void (*free)(void* param, void *packet);

	/// @return 0-ok, other-error
	int (*packet)(void* param, const void *packet, int bytes, uint32_t timestamp, int flags);
};

/// Create RTP packet encoder
/// @param[in] payload RTP payload type, value: [0, 127] (see more about rtp-profile.h)
/// @param[in] name RTP payload name
/// @param[in] seq RTP header sequence number filed
/// @param[in] ssrc RTP header SSRC filed
/// @param[in] handler user-defined callback functions
/// @param[in] cbparam user-defined parameter
/// @return NULL-error, other-ok
void* rtp_payload_encode_create(int payload, const char* name, uint16_t seq, uint32_t ssrc, struct rtp_payload_t *handler, void* cbparam);
void rtp_payload_encode_destroy(void* encoder);

/// Get rtp last packet sequence number and timestamp
/// @param[in] encoder RTP packet encoder(create by rtp_payload_encode_create)
/// @param[in] seq RTP header sequence number (next packet to be sent)
/// @param[in] timestamp RTP header timestamp
void rtp_payload_encode_getinfo(void* encoder, uint16_t* seq, uint32_t* timestamp);

/// Reserve `count` consecutive RTP sequence numbers from the encoder's shared
/// sequence counter. Used to allocate sequence numbers for out-of-band packets
/// (e.g. RFC5109 ULPFEC) so that media and FEC packets share one RTP sequence
/// space (WebRTC convention).
/// @param[in] encoder RTP packet encoder(create by rtp_payload_encode_create)
/// @param[in] count number of sequence numbers to reserve (1..32767)
/// @param[out] first_seq first reserved sequence number
/// @return 0-ok, <0-unsupported or invalid argument (counter unchanged)
int rtp_payload_encode_reserve_seq(void* encoder, uint16_t count, uint16_t* first_seq);

/// Encode RTP packet
/// @param[in] encoder RTP packet encoder(create by rtp_payload_encode_create)
/// @param[in] data stream data
/// @param[in] bytes stream length in bytes
/// @param[in] timestamp RTP header timestamp
/// @return 0-ok, ENOMEM-alloc failed, <0-failed
int rtp_payload_encode_input(void* encoder, const void* data, int bytes, uint32_t timestamp);


/// Create RTP packet decoder
/// @param[in] payload RTP payload type, value: [0, 127] (see more about rtp-profile.h)
/// @param[in] name RTP payload name
/// @param[in] handler user-defined callback functions
/// @param[in] cbparam user-defined parameter
/// @return NULL-error, other-ok
void* rtp_payload_decode_create(int payload, const char* name, struct rtp_payload_t *handler, void* cbparam);
void rtp_payload_decode_destroy(void* decoder);

/// Decode RTP packet
/// @param[in] decoder RTP packet decoder(create by rtp_payload_decode_create)
/// @param[in] packet RTP packet, include rtp header(12 bytes)
/// @param[in] bytes RTP packet length in bytes
/// @return 1-packet handled, 0-packet discard, <0-failed
int rtp_payload_decode_input(void* decoder, const void* packet, int bytes);

/// 通知解包器：该 seq 已被非媒体包（如共享 seq 的 FEC）占用，无媒体载荷。
/// 仅推进 seq 连续性，避免解包器把该 seq 当丢包整段丢弃。
/// @return 0-ok, <0-error
int rtp_payload_decode_skip_seq(void* decoder, uint16_t seq);

/// 缺口策略：1=缺号不丢弃正在组装的 NAL（共享 seq，FEC 占位槽）；
/// 0=上游行为（缺号丢弃）。默认 0。
int rtp_payload_decode_set_tolerant(void* decoder, int on);

/// Set/Get rtp encode packet size(include rtp header)
void rtp_packet_setsize(int bytes);
int rtp_packet_getsize(void);

#ifdef __cplusplus
}
#endif
#endif /* !_rtp_payload_h_ */
