#pragma once

#include "../axi_utils.hpp"
#include "../packet.hpp"
#include "../toe/toe.hpp"
#include "snic_handler_config.hpp"

#ifndef __SYNTHESIS__
static const ap_uint<32> END_TIME = 1000; // 1000000;
static const ap_uint<40> END_TIME_120 = 1000;

#else
static const ap_uint<32> END_TIME = 1546546546; // 1501501501;
static const ap_uint<40> END_TIME_120 = 18750000000;
#endif

const uint32_t IPERF_TCP_HEADER_SIZE = 192;

/**
 * iperf TCP Header
 */
template <int W> class iperfTcpHeader : public packetHeader<W, IPERF_TCP_HEADER_SIZE> {
	using packetHeader<W, IPERF_TCP_HEADER_SIZE>::header;

    public:
	iperfTcpHeader()
	{
		header(63, 32) = 0x01000000;
		header(79, 64) = 0x0000;
		header(127, 96) = 0x00000000;
		header(159, 128) = 0;
	}

	void setDualMode(bool en)
	{
		if (en) {
			header(31, 0) = 0x01000080;
		} else {
			header(31, 0) = 0x00000000;
		}
	}
	// This is used for dual tes
	void setListenPort(ap_uint<16> port)
	{
		header(95, 80) = reverse(port);
	}
	void setSeconds(ap_uint<32> seconds)
	{
		header(191, 160) = reverse(seconds);
	}
};

struct internalAppTxRsp {
	ap_uint<16> sessionID;
	ap_uint<2> error;
	internalAppTxRsp()
	{
	}
	internalAppTxRsp(ap_uint<16> id, ap_uint<2> err) : sessionID(id), error(err)
	{
	}
};

void snic_handler(hls::stream<ap_uint<16> > &listenPort, hls::stream<bool> &listenPortStatus,
		  hls::stream<appNotification> &notifications,
		  hls::stream<appReadRequest> &readRequest, hls::stream<ap_uint<16> > &rxMetaData,
		  hls::stream<net_axis<DATA_WIDTH> > &rxData, hls::stream<ipTuple> &openConnection,
		  hls::stream<openStatus> &openConStatus,
		  hls::stream<ap_uint<16> > &closeConnection, hls::stream<appTxMeta> &txMetaData,
		  hls::stream<net_axis<DATA_WIDTH> > &txData, hls::stream<appTxRsp> &txStatus,
		  ap_uint<1> runExperiment, ap_uint<14> useConn, ap_uint<8> pkgWordCount,
		  hls::stream<net_axis<DATA_WIDTH> > &dataFromEndpoint,
		  hls::stream<net_axis<DATA_WIDTH> > &dataToEndpoint);
