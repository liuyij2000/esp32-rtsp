#include "rtcp-internal.h"
// #include "rtp-packet.h"
#include "rtp-util.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static void rtp_seq_init(rtp_member *sender, uint16_t seq)
{
	sender->rtp_seq = seq;
	sender->rtp_seq_bad = (1<<16) + 1; /* so seq == bad_seq is false */
	sender->rtp_seq_base = seq;
	sender->rtp_seq_cycles = 0;
	sender->rtp_packets0 = 0;
	sender->rtp_expected0 = 0;
	sender->rtp_bytes = 0;
	sender->rtp_packets = 0;
	sender->rtp_probation = 0;
	sender->jitter = 0.0;
}

static int rtp_seq_update(rtp_member *sender, uint16_t seq)
{
	uint16_t delta;
	delta = seq - sender->rtp_seq;

	if(sender->rtp_probation > 0)
	{
		if(sender->rtp_seq + 1 == seq)
		{
			sender->rtp_seq = seq;
			if(0 == --sender->rtp_probation)
			{
				rtp_seq_init(sender, seq);
				return 1;
			}
		}
		else
		{
			sender->rtp_probation = RTP_PROBATION;
			sender->rtp_seq = seq;
		}
		return 0;
	}
	else if( delta < RTP_DROPOUT)
	{
		// in order, with permissible gap
		if(seq < sender->rtp_seq)
		{
			// sequence number wrapped
			sender->rtp_seq_cycles += (1 << 16);
		}

		sender->rtp_seq = seq;
	}
	else if( delta <= (1 << 16) - RTP_MISORDER )
	{
		/* the sequence number made a very large jump */
		if(sender->rtp_seq_bad + 1 == seq)
		{
			rtp_seq_init(sender, seq);
		}
		else
		{
			sender->rtp_seq_bad = seq;
			return 0;
		}
	}
	else
	{
		// duplicate or reordered packet
	}

	return 1;
}

rtp_member* rtp_member_fetch(rtp_session_t *session, uint32_t ssrc)
{
	rtp_member *p;
	p = rtp_member_list_find(session->members, ssrc);
	if(!p)
	{
		// exist in sender list?
		assert(!rtp_member_list_find(session->senders, ssrc));

		p = rtp_member_create(ssrc);
		if(p)
		{
			struct rtcp_msg_t msg;

			// update members list
			rtp_member_list_add(session->members, p);
			rtp_member_release(p);

			msg.type = RTCP_MSG_MEMBER;
			msg.u.member.ssrc = ssrc;
		}
	}
	return p;
}

rtp_member* rtp_sender_fetch(rtp_session_t *session, uint32_t ssrc)
{
	rtp_member *p;
	p = rtp_member_list_find(session->senders, ssrc);
	if(!p)
	{
		p = rtp_member_fetch(session, ssrc);
		if(p)
		{
			// update senders list
			rtp_member_list_add(session->senders, p);
		}
	}
	return p;
}

static int rtcp_parse(rtp_session_t *session, const unsigned char* data, size_t bytes)
{
	uint32_t rtcphd;
	rtcp_hdr_t header;

	assert(bytes >= sizeof(rtcphd));
	rtcphd = nbo_r32(data);

	header.v = RTCP_V(rtcphd);
	header.p = RTCP_P(rtcphd);
	header.count = RTCP_RC(rtcphd);
	header.pt = RTCP_PT(rtcphd);
	header.length = RTCP_LEN(rtcphd);
	
	// 1. RTP version field must equal 2 (p69)
	// 2. The payload type filed of the first RTCP packet in a compound packet must be SR or RR (p69)
	// 3. padding only valid at the last packet
	if (header.length * 4 + 4 > bytes || 2 != header.v || (1 == header.p && header.length < data[bytes - 1]))
	{
		assert(0);
		return -1;
	}

	if(1 == header.p)
	{
		header.length -= data[bytes - 1];
	}

	switch(header.pt)
	{
	case RTCP_SR:
		rtcp_sr_unpack(session, &header, data+4);
		break;

	case RTCP_RR:
		rtcp_rr_unpack(session, &header, data+4);
		break;

	case RTCP_SDES:
		rtcp_sdes_unpack(session, &header, data+4);
		break;

	case RTCP_BYE:
		rtcp_bye_unpack(session, &header, data+4);
		break;

	case RTCP_APP:
		rtcp_app_unpack(session, &header, data+4);
		break;

	default:
		assert(0);
	}

	return (RTCP_LEN(rtcphd) + 1) * 4;
}

int rtcp_input_rtcp(rtp_session_t *session, const void* data, int bytes)
{
	int r;
	const unsigned char* p;

	// RFC3550 6.1 RTCP Packet Format
	// 1. The first RTCP packet in the compound packet must always be a report packet to facilitate header validation
	// 2. An SDES packet containing a CNAME item must be included in each compound RTCP packet
	// 3. BYE should be the last packet sent with a given SSRC/CSRC.
	p = (const unsigned char*)data;
	while(bytes > 4)
	{
		// compound RTCP packet
		r = rtcp_parse(session, p, bytes);
		if(r <= 0)
			break;

		// RFC3550 6.3.3 Receiving an RTP or Non-BYE RTCP Packet (p26)
		session->avg_rtcp_size = (int)(session->avg_rtcp_size*1.0/16 + r * 15.0/16);

		p += r;
		bytes -= r;
	}
	return 0;
}

//主要是计算jitter
int rtcp_input_rtp(rtp_session_t *session, const void* data, int bytes)
{
	uint64_t clock;
	rtp_packet_t pkt={0};
	rtp_member *sender=NULL;

	/*
	if(0 != rtp_packet_deserialize(&pkt, data, bytes))
		return -1; // packet error

	assert(2 == pkt.rtp.v);
	sender = rtp_sender_fetch(session, pkt.rtp.ssrc);
	if(!sender)
		return -1; // memory error
	*/

	clock = rtpclock();

	// RFC3550 A.1 RTP Data Header Validity Checks
	if(0 == rtp_seq_update(sender, (uint16_t)pkt.rtp.seq))
		return 0; // disorder(need more data)

	// RFC3550 A.8 Estimating the Interarrival Jitter
	// the jitter estimate is updated:
	if(0 != sender->rtp_packets)
	{
		int D;
		D = (int)((unsigned int)((clock - sender->rtp_clock)*session->frequence/1000000) - (pkt.rtp.ts - sender->rtp_timestamp));
		if(D < 0) D = -D;
		sender->jitter += (D - sender->jitter)/16.0;
	}
	else
	{
		sender->jitter = 0.0;
	}

	sender->rtp_clock = clock;
	sender->rtp_timestamp = pkt.rtp.ts;
	sender->rtp_bytes += pkt.size;
	sender->rtp_packets += 1;
	return 1;
}
