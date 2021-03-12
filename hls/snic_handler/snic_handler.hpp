#pragma once

#include "../axi_utils.hpp"
#include "../packet.hpp"
#include "../toe/toe.hpp"
#include "snic_handler_config.hpp"

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
