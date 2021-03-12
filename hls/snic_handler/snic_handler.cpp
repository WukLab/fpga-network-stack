#include "snic_handler.hpp"
#include "snic_handler_config.hpp"
#include <iostream>

struct internalOpenConnMeta {
	ap_uint<32> remote_ip;
	ap_uint<16> remote_port;
};

struct internalListenConnMeta {
	ap_uint<16> local_port;
};

// Buffers responses coming from the TCP stack
void buffer_txStatus(hls::stream<appTxRsp> &txStatus, hls::stream<appTxRsp> &txStatusBuffer)
{
#pragma HLS PIPELINE II = 1
#pragma HLS INLINE off

	if (!txStatus.empty()) {
		appTxRsp resp = txStatus.read();
		txStatusBuffer.write(resp)
	}
}

template <int WIDTH>
void client(hls::stream<ipTuple> &openConnection, hls::stream<openStatus> &openConStatus,
	    hls::stream<ap_uint<16> > &closeConnection, hls::stream<appTxMeta> &txMetaData,
	    hls::stream<net_axis<WIDTH> > &txData,
	    hls::stream<appTxRsp> &txStatus,
	    hls::stream<struct internalOpenConnMeta> &internalOpenConnMeta,
	    hls::stream<net_axis<WIDTH> > &dataFromEndpoint2TCP)
{
#pragma HLS PIPELINE II = 1
#pragma HLS INLINE off

	enum openConnState { IDLE, WAIT_CON };

	static openConnState openFsmState = IDLE;

	switch (openFsmState) {
	case IDLE:
		if (!internalOpenConnMeta.empty()) {
			struct internalOpenConnMeta open;
			open = internalOpenConnMeta.read();

			ipTuple openTuple;
			openTuple.ip_address = open.remote_ip;
			openTuple.ip_port = open.remote_port;
			openConnection.write(openTuple);
			openFsmState = WAIT_CON;
		}
		break;
	case WAIT_CON:
		if (!openConStatus.empty()) {
			openStatus status = openConStatus.read();
			ap_uint<16> sessionID = status.sessionID;

			//printf("Open status: %d sessionID: %d\n", status.success, sessionID);

			/*
		       * TODO
		       * we should send REPLY back to host
		       */
			if (status.success) {
				;
			}
			openFsmState = IDLE;
		}
		break;
	}

	enum sendDataState {
		S_IDLE,
		WAIT_TX_STATUS,
		SEND_DATA
	};
       	static sendDataState sendDataState = S_IDLE;

	switch (sendDataState) {
	case (S_IDLE):
		if (!dataFromEndpoint2TCP.empty()) {
			/*
			 * TODO
			 * grab sessionID and len info from the header
			 */
			/*
			 * TODO
			 * the packets from host should have sessionID included
			 */
			static ap_uint<16> sessionID;
			static ap_uint<16> len;
			txMetaData.write(appTxMeta(sessionID, len));
			sendDataState = WAIT_TX_STATUS;
		}
		break;
	case (WAIT_TX_STATUS):
		if (!txStatus.empty()) {
			appTxRsp resp = txStatus.read();
			if (resp.error == 0) {
				sendDataState = SEND_DATA;
			} else {
				/*
				 * Retry
				 * (We are not dealing with closed connection)
				 */
				txMetaData.write(appTxMeta(sessionID, len));
				sendDataState = S_IDLE;
			}
		}
		break;
	case (SEND_DATA):
		if (!dataFromEndpoint2TCP.empty()) {
			net_axis<WIDTH> data = dataFromEndpoint2TCP.read();
			txData.write(data);

			if (data.last) {
				sendDataState = S_IDLE;
			}
		}
		break;
	}
}

template <int WIDTH>
void server(hls::stream<ap_uint<16> > &listenPort, hls::stream<bool> &listenPortStatus,
	    hls::stream<appNotification> &notifications, hls::stream<appReadRequest> &readRequest,
	    hls::stream<ap_uint<16> > &rxMetaData, hls::stream<net_axis<WIDTH> > &rxData,
	    hls::stream<struct internalListenConnMeta> &internalListenConnMeta)
{
#pragma HLS PIPELINE II = 1
#pragma HLS INLINE off

	enum listenFsmStateType { OPEN_PORT, WAIT_PORT_STATUS };
	static listenFsmStateType listenState = OPEN_PORT;

	switch (listenState) {
	case OPEN_PORT:
		/*
		 * Endpoint asks to open a port,
		 * send a Listen request to TCP.
		 */
		if (!internalListenConnMeta.empty()) {
			struct internalListenConnMeta listen = internalListenConnMeta.read();

			listenPort.write(listen.local_port);
			listenState = WAIT_PORT_STATUS;
		}
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

	enum consumeFsmStateType { WAIT_PKG, CONSUME };
	static consumeFsmStateType serverFsmState = WAIT_PKG;

	if (!notifications.empty()) {
		appNotification notification = notifications.read();

		if (notification.length != 0) {
			readRequest.write(appReadRequest(notification.sessionID, notification.length));
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
		   hls::stream<net_axis<WIDTH> > &dataToEndpoint,
		   hls::stream<struct internalOpenConnMeta> &internalOpenConnMeta,
		   hls::stream<struct internalListenConnMeta> &internalListenConnMeta,
		   hls::stream<net_axis<WIDTH> > &dataFromEndpoint2TCP)
{
	struct internalOpenConnMeta open;
	struct internalListenConnMeta listen;

	if (!dataFromEndpoint.empty()) {
		net_axis<WIDTH> w = dataFromEndpoint.read();

		// TODO change after we decide hdr format.
		int mode = w.data(32, 31);
		int remote_ip = w.data(31, 0);
		int port = w.data(15, 0);

		if (mode == 0) {
			open.remote_ip = remote_ip;
			open.remote_port = port;
			internalOpenConnMeta.write(open);
		} else if (mode == 1) {
			listen.local_port = port;
			internalListenConnMeta.write(listen);
		} else if (mode == 2) {
			dataFromEndpoint2TCP.write(w);
		} else if (mode ==3 ) {
			dataToEndpoint.write(w);
		}
	}
}

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

	// This is required to buffer up to 1024 reponses => supporting up to 1024 connections
	static hls::stream<appTxRsp> txStatusBuffer("txStatusBuffer");
#pragma HLS STREAM variable = txStatusBuffer depth = 32

	static hls::stream<struct internalOpenConnMeta> internalOpenConnMeta("internalOpenConnMeta");
	static hls::stream<struct internalListenConnMeta> internalListenConnMeta("internalListenConnMeta");
#pragma HLS STREAM variable = internalOpenConnMeta depth = 32
#pragma HLS STREAM variable = internalListenConnMeta depth = 32

	static hls::stream<net_axis<DATA_WIDTH> > dataFromEndpoint2TCP("dataFromEndpoint2TCP");
#pragma HLS STREAM variable = dataFromEndpoint2TCP depth = 8

	parse_packets<DATA_WIDTH>(dataFromEndpoint, dataToEndpoint, internalOpenConnMeta,
				  internalListenConnMeta, dataFromEndpoint2TCP);

	buffer_txStatus(txStatus, txStatusBuffer);

	client<DATA_WIDTH>(openConnection, openConStatus, closeConnection, txMetaData, txData,
			   txStatusBuffer, internalOpenConnMeta, dataFromEndpoint2TCP);

	server<DATA_WIDTH>(listenPort, listenPortStatus, notifications, readRequest, rxMetaData, rxData,
			   internalListenConnMeta);
}
