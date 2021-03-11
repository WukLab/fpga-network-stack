#include "snic_handler.hpp"
#include "snic_handler_config.hpp"
#include <iostream>

// Buffers responses coming from the TCP stack
void status_handler(hls::stream<appTxRsp> &txStatus, hls::stream<internalAppTxRsp> &txStatusBuffer)
{
#pragma HLS PIPELINE II = 1
#pragma HLS INLINE off

	if (!txStatus.empty()) {
		appTxRsp resp = txStatus.read();
		txStatusBuffer.write(internalAppTxRsp(resp.sessionID, resp.error));
	}
}

template <int WIDTH>
void client(hls::stream<ipTuple> &openConnection, hls::stream<openStatus> &openConStatus,
	    hls::stream<ap_uint<16> > &closeConnection, hls::stream<appTxMeta> &txMetaData,
	    hls::stream<net_axis<WIDTH> > &txData, hls::stream<internalAppTxRsp> &txStatus,
	    ap_uint<14> useConn, ap_uint<8> pkgWordCount, ap_uint<32> regIpAddress0)
{
#pragma HLS PIPELINE II = 1
#pragma HLS INLINE off

	enum iperfFsmStateType {
		IDLE,
		INIT_CON,
		WAIT_CON,
		CONSTRUCT_HEADER,
		INIT_RUN,
		START_PKG,
		CHECK_REQ,
		WRITE_PKG,
		CHECK_TIME
	};
	static iperfFsmStateType iperfFsmState = IDLE;

	static ap_uint<14> numConnections = 0;
	static ap_uint<16> currentSessionID;
	static ap_uint<14> sessionIt = 0;
	static ap_uint<14> closeIt = 0;
	static ap_uint<8> wordCount = 0;
	static ap_uint<4> ipAddressIdx = 0;
	static iperfTcpHeader<WIDTH> header;
	static ap_uint<8> packetGapCounter = 0;

	switch (iperfFsmState) {
	case IDLE:
		// done do nothing
		sessionIt = 0;
		closeIt = 0;
		numConnections = 0;
		ipAddressIdx = 0;

		// XXX
		iperfFsmState = INIT_CON;
		break;
	case INIT_CON:
		if (sessionIt < useConn) {
			ipTuple openTuple;
			switch (ipAddressIdx) {
			case 0:
				openTuple.ip_address = regIpAddress0;
				break;
			}
			openTuple.ip_port = 5001;
			openConnection.write(openTuple);
			ipAddressIdx++;
			if (ipAddressIdx == 10) {
				ipAddressIdx = 0;
			}
		}
		sessionIt++;
		if (sessionIt == useConn) {
			sessionIt = 0;
			iperfFsmState = WAIT_CON;
		}
		break;
	case WAIT_CON:
		if (!openConStatus.empty()) {
			openStatus status = openConStatus.read();
			if (status.success) {
				// experimentID[sessionIt] = status.sessionID;
				std::cout << "Connection successfully opened." << std::endl;

				/*
         * NOTE YS
         * Ask if we can TX data
         */
				txMetaData.write(
					appTxMeta(status.sessionID, IPERF_TCP_HEADER_SIZE / 8));
				numConnections++;
			} else {
				std::cout << "Connection could not be opened." << std::endl;
			}

			sessionIt++;
			if (sessionIt == useConn) {
				sessionIt = 0;
				iperfFsmState = CONSTRUCT_HEADER;
			}
		}
		break;

	case CONSTRUCT_HEADER:
		header.clear();
		// header.setDualMode(dualModeEn);
		header.setListenPort(5001);
		// header.setSeconds(timeInSeconds);

		if (sessionIt == numConnections) {
			sessionIt = 0;
			iperfFsmState = CHECK_REQ;
		} else if (!txStatus.empty()) {
			internalAppTxRsp resp = txStatus.read();
			if (resp.error == 0) {
				currentSessionID = resp.sessionID;
				iperfFsmState = INIT_RUN;
			} else {
				if (resp.error == 1) {
					std::cout << "Connection was torn down. " << resp.sessionID
						  << std::endl;
					numConnections--;
				}
			}
		}
		break;
	case INIT_RUN: {
		net_axis<WIDTH> headerWord;
		headerWord.last = 0;

		if (header.consumeWord(headerWord.data) < (WIDTH / 8)) {
			headerWord.last = 1;

			/*
       * XXX
       * why do another txMetaData write here?
       * we already sent the req before
       */
			txMetaData.write(appTxMeta(currentSessionID, pkgWordCount * (WIDTH / 8)));
			sessionIt++;
			iperfFsmState = CONSTRUCT_HEADER;
		}
		headerWord.keep = ~0;
		if (headerWord.last) {
			if (WIDTH == 128) {
				headerWord.keep(15, 8) = 0;
			}
			if (WIDTH > 128) {
				headerWord.keep((WIDTH / 8) - 1, 24) = 0;
			}
		}
		txData.write(headerWord);
	} break;
	case CHECK_REQ:
		if (!txStatus.empty()) {
			internalAppTxRsp resp = txStatus.read();
			if (resp.error == 0) {
				currentSessionID = resp.sessionID;
				iperfFsmState = START_PKG;
			} else {
				if (resp.error == 1) {
					std::cout << "Connection was torn down. " << resp.sessionID
						  << std::endl;
					numConnections--;
				} else {
					txMetaData.write(appTxMeta(resp.sessionID,
								   pkgWordCount * (WIDTH / 8)));
					// sessionIt++;
				}
			}
		}
		break;
	case START_PKG: {
		net_axis<WIDTH> currWord;

		for (int i = 0; i < (WIDTH / 64); i++) {
#pragma HLS UNROLL
			currWord.data(i * 64 + 63, i * 64) = 0x3736353433323130;
			currWord.keep(i * 8 + 7, i * 8) = 0xff;
		}
		currWord.last = 0;

		// do the WRITE
		txData.write(currWord);

		wordCount = 1;
		iperfFsmState = WRITE_PKG;
	} break;
	case WRITE_PKG: {
		wordCount++;
		net_axis<WIDTH> currWord;
		for (int i = 0; i < (WIDTH / 64); i++) {
#pragma HLS UNROLL
			currWord.data(i * 64 + 63, i * 64) = 0x3736353433323130;
			currWord.keep(i * 8 + 7, i * 8) = 0xff;
		}
		currWord.last = (wordCount == pkgWordCount);
		txData.write(currWord);
		if (currWord.last) {
			wordCount = 0;
			iperfFsmState = CHECK_TIME;
		}
	} break;

	case CHECK_TIME:
		iperfFsmState = IDLE;
		break;
	}
}

template <int WIDTH>
void server(hls::stream<ap_uint<16> > &listenPort, hls::stream<bool> &listenPortStatus,
	    hls::stream<appNotification> &notifications, hls::stream<appReadRequest> &readRequest,
	    hls::stream<ap_uint<16> > &rxMetaData, hls::stream<net_axis<WIDTH> > &rxData)
{
#pragma HLS PIPELINE II = 1
#pragma HLS INLINE off

	enum listenFsmStateType { OPEN_PORT, WAIT_PORT_STATUS };
	static listenFsmStateType listenState = OPEN_PORT;
	enum consumeFsmStateType { WAIT_PKG, CONSUME };
	static consumeFsmStateType serverFsmState = WAIT_PKG;
#pragma HLS RESET variable = listenState

	switch (listenState) {
	case OPEN_PORT:
		// Open Port 5001
		listenPort.write(5001);
		listenState = WAIT_PORT_STATUS;
		break;
	case WAIT_PORT_STATUS:
		if (!listenPortStatus.empty()) {
			bool open = listenPortStatus.read();
			if (!open) {
				listenState = OPEN_PORT;
			}
		}
		break;
	}

	if (!notifications.empty()) {
		appNotification notification = notifications.read();

		if (notification.length != 0) {
			readRequest.write(
				appReadRequest(notification.sessionID, notification.length));
		}
	}

	switch (serverFsmState) {
	case WAIT_PKG:
		if (!rxMetaData.empty() && !rxData.empty()) {
			rxMetaData.read();
			net_axis<WIDTH> receiveWord = rxData.read();
			if (!receiveWord.last) {
				serverFsmState = CONSUME;
			}
		}
		break;
	case CONSUME:
		if (!rxData.empty()) {
			net_axis<WIDTH> receiveWord = rxData.read();
			if (receiveWord.last) {
				serverFsmState = WAIT_PKG;
			}
		}
		break;
	}
}

/*
 * We only support 64B width.
 * one flit is enough.
 */
template <int WIDTH>
void parse_packets(hls::stream<net_axis<WIDTH> > &dataFromEndpoint,
		   hls::stream<net_axis<WIDTH> > &dataToEndpoint)
{
	if (!dataFromEndpoint.empty()) {
		net_axis<WIDTH> w = dataFromEndpoint.read();
		dataToEndpoint.write(w);
	}
}

void snic_handler(hls::stream<ap_uint<16> > &listenPort, hls::stream<bool> &listenPortStatus,
		  hls::stream<appNotification> &notifications,
		  hls::stream<appReadRequest> &readRequest, hls::stream<ap_uint<16> > &rxMetaData,
		  hls::stream<net_axis<DATA_WIDTH> > &rxData, hls::stream<ipTuple> &openConnection,
		  hls::stream<openStatus> &openConStatus,
		  hls::stream<ap_uint<16> > &closeConnection, hls::stream<appTxMeta> &txMetaData,
		  hls::stream<net_axis<DATA_WIDTH> > &txData, hls::stream<appTxRsp> &txStatus,
		  ap_uint<14> useConn, ap_uint<8> pkgWordCount, ap_uint<32> regIpAddress0,
		  hls::stream<net_axis<DATA_WIDTH> > &dataFromEndpoint,
		  hls::stream<net_axis<DATA_WIDTH> > &dataToEndpoint)
{
#pragma HLS DATAFLOW disable_start_propagation
#pragma HLS INTERFACE ap_ctrl_none port = return

#pragma HLS INTERFACE axis register port = dataFromEndpoint name = s_axis_dataFromEndpoint
#pragma HLS INTERFACE axis register port = dataToEndpoint name = m_axis_dataToEndpoint

#pragma HLS INTERFACE axis register port = listenPort name = m_axis_listen_port
#pragma HLS INTERFACE axis register port = listenPortStatus name = s_axis_listen_port_status

#pragma HLS INTERFACE axis register port = notifications name = s_axis_notifications
#pragma HLS INTERFACE axis register port = readRequest name = m_axis_read_package
#pragma HLS DATA_PACK variable = notifications
#pragma HLS DATA_PACK variable = readRequest

#pragma HLS INTERFACE axis register port = rxMetaData name = s_axis_rx_metadata
#pragma HLS INTERFACE axis register port = rxData name = s_axis_rx_data

#pragma HLS INTERFACE axis register port = openConnection name = m_axis_open_connection
#pragma HLS INTERFACE axis register port = openConStatus name = s_axis_open_status
#pragma HLS DATA_PACK variable = openConnection
#pragma HLS DATA_PACK variable = openConStatus

#pragma HLS INTERFACE axis register port = closeConnection name = m_axis_close_connection

#pragma HLS INTERFACE axis register port = txMetaData name = m_axis_tx_metadata
#pragma HLS INTERFACE axis register port = txData name = m_axis_tx_data
#pragma HLS INTERFACE axis register port = txStatus name = s_axis_tx_status
#pragma HLS DATA_PACK variable = txMetaData
#pragma HLS DATA_PACK variable = txStatus

#pragma HLS INTERFACE ap_none register port = useConn
#pragma HLS INTERFACE ap_none register port = pkgWordCount
#pragma HLS INTERFACE ap_none register port = regIpAddress0

	// This is required to buffer up to 1024 reponses => supporting up to 1024
	// connections
	static hls::stream<internalAppTxRsp> txStatusBuffer("txStatusBuffer");
#pragma HLS STREAM variable = txStatusBuffer depth = 1024

	/*
   * TODO:
   *
   * Parse data from endpoint, then determine whether we should
   *    1.1. acting as a server, listenPort
   *    1.2. acting as a client, open with a remote ip:port
   *
   * packets from/to endpoint should have special mark.
   */

	static hls::stream<internalAppTxRsp> txStatusBuffer("txStatusBuffer");
#pragma HLS STREAM variable = txStatusBuffer depth = 1024
	parse_packets<DATA_WIDTH>(dataFromEndpoint, dataToEndpoint);

	/*
   * Client
   */
	status_handler(txStatus, txStatusBuffer);

	client<DATA_WIDTH>(openConnection, openConStatus, closeConnection, txMetaData, txData,
			   txStatusBuffer, useConn, pkgWordCount, regIpAddress0);

	/*
   * Server
   */
	server<DATA_WIDTH>(listenPort, listenPortStatus, notifications, readRequest, rxMetaData,
			   rxData);
}
