#ifndef _rtp_member_h_
#define _rtp_member_h_

#include "rtcp-header.h"

#define RTP_PROBATION	2
#define RTP_DROPOUT		500
#define RTP_MISORDER	100

typedef struct 
{
	int32_t ref;					//member存在member_list中的参考地址

	uint32_t ssrc;					// ssrc == rtcp_sr.ssrc == rtcp_rb.ssrc
	rtcp_sr_t rtcp_sr;
	rtcp_rb_t rtcp_rb;
	rtcp_sdes_item_t sdes[9];		// SDES item

	uint64_t rtcp_clock;			// last RTCP SR/RR packet clock(local time)

	uint16_t rtp_seq;				// last send/received RTP packet RTP sequence(in packet header)
	uint32_t rtp_timestamp;			// last send/received RTP packet RTP timestamp(in packet header)
	uint64_t rtp_clock;				// last send/received RTP packet clock(local time)
	uint32_t rtp_packets;			// send/received RTP packet count(include duplicate, late)
	uint64_t rtp_bytes;				// send/received RTP octet count

	double jitter;
	uint32_t rtp_packets0;			// last SR received RTP packets
	uint32_t rtp_expected0;			// last SR expect RTP sequence number

	uint16_t rtp_probation;
	uint16_t rtp_seq_base;			// init sequence number
	uint32_t rtp_seq_bad;			// bad sequence number
	uint32_t rtp_seq_cycles;		// high extension sequence number
} rtp_member;

rtp_member* rtp_member_create(uint32_t ssrc);
void rtp_member_addref(rtp_member *member);
void rtp_member_release(rtp_member *member);

int rtp_member_setvalue(rtp_member *member, int item, const uint8_t* data, int bytes);

#endif /* !_rtp_member_h_ */