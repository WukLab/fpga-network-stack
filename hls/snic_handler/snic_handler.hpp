#pragma once

#include "../axi_utils.hpp"
#include "../packet.hpp"
#include "../toe/toe.hpp"
#include "snic_handler_config.hpp"

#define __packed		__attribute__((__packed__))

struct eth_hdr {
	uint8_t dst_mac[6];
	uint8_t src_mac[6];
	uint16_t eth_type;
} __packed;

struct ipv4_hdr {
	uint8_t ihl : 4;
	uint8_t version : 4;

	uint8_t ecn : 2;
	uint8_t dscp : 6;

	uint16_t tot_len;
	uint16_t id;
	uint16_t frag_off;
	uint8_t ttl;
	uint8_t protocol;
	uint16_t check;
	uint32_t src_ip;
	uint32_t dst_ip;
} __attribute__((packed));

struct udp_hdr {
	uint16_t src_port;
	uint16_t dst_port;
	uint16_t len;
	uint16_t check;
} __attribute__((packed));

struct common_headers {
	struct eth_hdr 		eth;
	struct ipv4_hdr		ipv4;
	struct udp_hdr		udp;
} __packed;

enum {
	SNIC_TCP_HANDLER_OP_OPEN_CONN = 1,
	SNIC_TCP_HANDLER_OP_LISTEN,
	SNIC_TCP_HANDLER_OP_WRITE,
	SNIC_TCP_HANDLER_OP_READ
};

struct snic_handler_open_conn_req {
	struct common_headers common_headers;
	uint32_t op;

	/* In network order */
	uint32_t local_port;
	uint32_t remote_ip;
	uint32_t remote_port;
} __packed;

struct snic_handler_listen_req {
	struct common_headers common_headers;
	uint32_t op;
	uint32_t local_port;
} __packed;

struct snic_handler_write_req {
	struct common_headers common_headers;

	uint32_t op;
	uint32_t local_port;
	uint32_t length;
	char data[];
} __packed;

struct snic_handler_read_rsp {
	struct common_headers common_headers;

	uint32_t op;
	uint32_t length;
	char data[];
} __packed;

void snic_handler(hls::stream<ap_uint<16> > &listenPort,
		  hls::stream<bool> &listenPortStatus,
		  hls::stream<appNotification> &notifications,
		  hls::stream<appReadRequest> &readRequest,
		  hls::stream<ap_uint<16> > &rxMetaData,
		  hls::stream<net_axis<DATA_WIDTH> > &rxData,
		  hls::stream<ipTuple> &openConnection,
		  hls::stream<openStatus> &openConStatus,
		  hls::stream<ap_uint<16> > &closeConnection,
		  hls::stream<appTxMeta> &txMetaData,
		  hls::stream<net_axis<DATA_WIDTH> > &txData,
		  hls::stream<appTxRsp> &txStatus,
		  hls::stream<net_axis<DATA_WIDTH> > &dataFromEndpoint,
		  hls::stream<net_axis<DATA_WIDTH> > &dataToEndpoint);
