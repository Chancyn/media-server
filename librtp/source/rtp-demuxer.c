#include "rtp-demuxer.h"
#include "rtp-internal.h"
#include "rtp-payload.h"
#include "rtp-packet.h"
#include "rtp-queue.h"
#include "rtp-param.h"
#include "rtp.h"
#include "rtcp-header.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>

struct rtp_demuxer_t
{
    uint32_t ssrc;
    uint64_t clock; // rtcp clock
    
    uint8_t* ptr;
    int cap, max;

    rtp_queue_t* queue;
    void* payload;
    void* rtp;
    
    rtp_demuxer_onpacket onpkt;
    void* param;

    int skip_pt; // >=0: 该 PT 入队占位但不解包（共享 seq FEC）

    // 缺口分类（共享 seq FEC 块图）：1=媒体 2=非媒体(FEC 占位) 0=未知。
    // 仅当缺口全部为 2 时推进解包器序号；否则按上游丢弃（防坏 NAL 花屏）。
    int (*gap_classify)(void* ctx, uint16_t seq);
    void* gap_classify_ctx;
    int gap_policy; // RTP_GAP_POLICY_xxx
    uint16_t last_seq;
    int last_seq_valid;
};

static int rtp_onpacket(void* param, const void *packet, int bytes, uint32_t timestamp, int flags)
{
    struct rtp_demuxer_t* rtp;
    rtp = (struct rtp_demuxer_t*)param;
    
    // TODO: rtp timestamp -> pts/dts
    
    return rtp->onpkt ? rtp->onpkt(rtp->param, packet, bytes, timestamp, flags) : -1;
}

static void rtp_on_rtcp(void* param, const struct rtcp_msg_t* msg)
{
    //struct rtp_demuxer_t* rtp;
    //rtp = (struct rtp_demuxer_t*)param;
    if (RTCP_BYE == msg->type)
    {
        printf("finished: %p\n", param);
        //rtp->onpkt(rtp->param, NULL, 0, 0, 0);
    }
}

static struct rtp_packet_t* rtp_demuxer_alloc(struct rtp_demuxer_t* rtp, const void* data, int bytes)
{
    int r;
    uint8_t* ptr;
    struct rtp_packet_t* pkt;
    
    if(rtp->cap < bytes + (int)sizeof(struct rtp_packet_t) + (int)sizeof(int) /*bytes*/ )
    {
        r = bytes + sizeof(struct rtp_packet_t) + sizeof(int);
        r = r > 1500 ? r : 1500;
        ptr = (uint8_t*)realloc(rtp->ptr, r + sizeof(int) /*cap*/ );
        if(!ptr)
            return NULL;
        
        rtp->cap = r;
        rtp->ptr = ptr;
        *(int*)ptr = r; /*cap*/
    }

    *((int*)rtp->ptr + 1) = bytes; /*bytes*/
    pkt = (struct rtp_packet_t*)(rtp->ptr + sizeof(int) /*cap*/  + sizeof(int) /*bytes*/ );
    memcpy(pkt + 1, data, bytes);
    
    r = rtp_packet_deserialize(pkt, pkt + 1, bytes);
    if(0 != r)
        return NULL;
    
    rtp->cap = 0; // need more memory
    rtp->ptr = NULL;
    return pkt;
}

static void rtp_demuxer_freepkt(void* param, struct rtp_packet_t* pkt)
{
    int cap;
    uint8_t* ptr;
    struct rtp_demuxer_t* rtp;
    rtp = (struct rtp_demuxer_t*)param;
    ptr = (uint8_t*)pkt - sizeof(int) /*cap*/ - sizeof(int) /*bytes*/ ;
    cap = *(int*)ptr;
    
    if(cap <= rtp->cap)
    {
        free(ptr);
        return;
    }
    
    if(rtp->cap > 0 && rtp->ptr)
        free(rtp->ptr);
    rtp->cap = cap;
    rtp->ptr = ptr;
}

static int rtp_demuxer_init(struct rtp_demuxer_t* rtp, int jitter, int frequency, int payload, const char* encoding)
{
    uint32_t timestamp;
    struct rtp_event_t evthandler;
    struct rtp_payload_t handler;
//    const struct rtp_profile_t* profile;
//    profile = rtp_profile_find(payload);
//    frequency = profile ? profile->frequency : 90000;

    memset(&handler, 0, sizeof(handler));
    handler.alloc = NULL;
    handler.free = NULL;
    handler.packet = rtp_onpacket;
    rtp->skip_pt = -1; // 默认关闭：与上游行为逐字节一致
    rtp->gap_classify = NULL;
    rtp->gap_classify_ctx = NULL;
    rtp->gap_policy = RTP_GAP_POLICY_STRICT;
    rtp->last_seq_valid = 0;
    rtp->payload = rtp_payload_decode_create(payload, encoding, &handler, rtp);
    
    timestamp = (uint32_t)rtpclock();
    evthandler.on_rtcp = rtp_on_rtcp;
    rtp->rtp = rtp_create(&evthandler, rtp, rtp->ssrc, timestamp, frequency ? frequency : 90000, 2 * 1024 * 1024, 0);
    
    rtp->queue = rtp_queue_create(jitter, frequency, rtp_demuxer_freepkt, rtp);
    
    return rtp->payload && rtp->rtp && rtp->queue? 0 : -1;
}

struct rtp_demuxer_t* rtp_demuxer_create(int jitter, int frequency, int payload, const char* encoding, rtp_demuxer_onpacket onpkt, void* param)
{
    struct rtp_demuxer_t* rtp;
    rtp = (struct rtp_demuxer_t*)calloc(1, sizeof(*rtp));
    if(!rtp)
        return NULL;
    
    if(0 != rtp_demuxer_init(rtp, jitter, frequency, payload, encoding))
    {
        rtp_demuxer_destroy(&rtp);
        return NULL;
    }
    
    rtp->onpkt = onpkt;
    rtp->param = param;
    rtp->clock = rtpclock();
    rtp->ssrc = rtp_ssrc();
    rtp->max = RTP_PAYLOAD_MAX_SIZE;
    return rtp;
}

int rtp_demuxer_destroy(struct rtp_demuxer_t** pprtp)
{
    struct rtp_demuxer_t* rtp;
    if(pprtp && *pprtp)
    {
        rtp = *pprtp;
        if(rtp->rtp)
            rtp_destroy(rtp->rtp);
        
        if(rtp->payload)
            rtp_payload_decode_destroy(rtp->payload);
        
        if(rtp->queue)
            rtp_queue_destroy(rtp->queue);
        
        if(rtp->ptr)
            free(rtp->ptr);
        free(rtp);
    }
    
    return 0;
}

int rtp_demuxer_set_gap_policy(struct rtp_demuxer_t* rtp, int policy)
{
    if (!rtp)
        return -EINVAL;
    if (policy != RTP_GAP_POLICY_TOLERANT && policy != RTP_GAP_POLICY_HYBRID)
        policy = RTP_GAP_POLICY_STRICT;
    rtp->gap_policy = policy;
    return 0;
}

int rtp_demuxer_set_gap_classify(struct rtp_demuxer_t* rtp,
                                 int (*fn)(void* ctx, uint16_t seq), void* ctx)
{
    if (!rtp)
        return -EINVAL;
    rtp->gap_classify = fn;
    rtp->gap_classify_ctx = ctx;
    return 0;
}

int rtp_demuxer_set_skip_pt(struct rtp_demuxer_t* rtp, int pt)
{
    if (!rtp)
        return -EINVAL;
    rtp->skip_pt = (pt >= 0 && pt < 128) ? pt : -1;
    return 0;
}

int rtp_demuxer_input(struct rtp_demuxer_t* rtp, const void* data, int bytes)
{
    int r;
    uint8_t pt;
    struct rtp_packet_t* pkt;
    
    if (bytes < 12 || bytes > rtp->max)
        return -EINVAL;

    pt = ((uint8_t*)data)[1];
    // RFC7983 SRTP: https://tools.ietf.org/html/draft-ietf-avtcore-rfc5764-mux-fixes
    // http://www.iana.org/assignments/rtp-parameters/rtp-parameters.xhtml#rtp-parameters-4
    // RFC 5761 (RTCP-mux) states this range for secure RTCP/RTP detection.
    // RTCP packet types in the ranges 1-191 and 224-254 SHOULD only be used when other values have been exhausted.
    if(pt < RTCP_FIR || pt > RTCP_LIMIT)
    {
        pkt = rtp_demuxer_alloc(rtp, data, bytes);
        if (!pkt)
            return -ENOMEM;

        r = rtp_queue_write(rtp->queue, pkt);
        if(r <= 0) // 0-discard packet(duplicate/too late)
        {
            rtp_demuxer_freepkt(rtp, pkt);
            return r;
        }
        
        // re-order packet
        pkt = rtp_queue_read(rtp->queue);
        while(pkt)
        {
            bytes = *(int*)((uint8_t*)pkt - sizeof(int) /*bytes*/ );

            r = rtp_onreceived(rtp->rtp, pkt + 1, bytes);
            // 缺口处理：只有"全部是 FEC 占位"的缺口才推进解包器序号（无载荷），
            // 媒体/未知缺口保持上游语义（解包器丢弃已组装 NAL，避免坏 NAL 花屏）。
            if (rtp->last_seq_valid) {
                const uint16_t expect = (uint16_t)(rtp->last_seq + 1);
                const uint32_t missing = (uint16_t)(pkt->rtp.seq - expect);
                if (missing > 0 && missing <= 64 && rtp->gap_policy != RTP_GAP_POLICY_STRICT) {
                    int advance = 0;
                    if (rtp->gap_policy == RTP_GAP_POLICY_TOLERANT) {
                        advance = 1;
                    } else if (rtp->gap_classify != NULL) {
                        // HYBRID：1=确认媒体 → 不推进（解包器丢弃 NAL，防坏 NAL
                        // 花屏）；2=FEC 占位 / 0=未知 → 推进（未知多为丢失的
                        // FEC 包，容忍可避免整帧冻结）。
                        int has_media = 0;
                        for (uint32_t i = 0; i < missing; i++) {
                            if (rtp->gap_classify(rtp->gap_classify_ctx,
                                                  (uint16_t)(expect + i)) == 1) {
                                has_media = 1;
                                break;
                            }
                        }
                        advance = !has_media;
                    }
                    if (advance) {
                        for (uint32_t i = 0; i < missing; i++)
                            rtp_payload_decode_skip_seq(rtp->payload, (uint16_t)(expect + i));
                    }
                }
            }
            rtp->last_seq = pkt->rtp.seq;
            rtp->last_seq_valid = 1;
            // 共享 seq：FEC 包占位但无媒体载荷；推进解包器 seq 连续性，不送载荷
            if (rtp->skip_pt >= 0 && pkt->rtp.pt == rtp->skip_pt)
                r = rtp_payload_decode_skip_seq(rtp->payload, pkt->rtp.seq);
            else
                r = rtp_payload_decode_input(rtp->payload, pkt + 1, bytes);
            rtp_demuxer_freepkt(rtp, pkt);
            if(r < 0)
                return r;
    
            pkt = rtp_queue_read(rtp->queue);
        }
    }
    else
    {
        r = rtp_onreceived_rtcp(rtp->rtp, data, bytes);
        (void)r; // ignore rtcp handler
        
        return pt; // rtcp message type
    }

    return 0;
}

int rtp_demuxer_rtcp(struct rtp_demuxer_t* rtp, void* buf, int len)
{
    int r;
    int interval;
    uint64_t clock;
    
    r = 0;
    clock = rtpclock();
    interval = rtp_rtcp_interval(rtp->rtp);
    if (rtp->clock + (uint64_t)interval * 1000 < clock)
    {
        // RTCP report
        r = rtp_rtcp_report(rtp->rtp, buf, len);
        rtp->clock = clock;
    }
    
    return r;
}

void rtp_demuxer_stats(struct rtp_demuxer_t* rtp, int* lost, int* late, int* misorder, int* duplicate)
{
    struct rtp_queue_stats_t stats;
    rtp_queue_stats(rtp->queue, &stats);
    if (lost) *lost = stats.lost;
    if (late) *late = stats.late;
    if (misorder) *misorder = stats.reorder;
    if (duplicate) *duplicate = stats.duplicate;
}
