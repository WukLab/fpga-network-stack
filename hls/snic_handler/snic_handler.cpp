#include "snic_handler.hpp"
#include "snic_handler_config.hpp"
#include <iostream>

struct map_entry {
	//ap_uint<16> sessionID;
	ap_uint<32> local_ip;
	ap_uint<16> local_port;
	//ap_uint<32> remote_ip;
	//ap_uint<32> remote_port;
};

#define NR_MAX_ENTRIES (1024)
struct map_entry map_table[NR_MAX_ENTRIES];

struct internalOpenConnMeta {
	ap_uint<32> remote_ip;
	ap_uint<16> remote_port;
	ap_uint<16> local_port;
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
		txStatusBuffer.write(resp);
	}
}

template <int WIDTH>
void client(hls::stream<ipTuple> &openConnection,
	    hls::stream<openStatus> &openConStatus,
	    hls::stream<appTxMeta> &txMetaData,
	    hls::stream<net_axis<WIDTH> > &txData,
	    hls::stream<appTxRsp> &txStatus,
	    hls::stream<struct internalOpenConnMeta> &internalOpenConnMeta,
	    hls::stream<net_axis<WIDTH> > &dataFromEndpoint2TCP,
	    hls::stream<net_axis<WIDTH> > &dataToEndpoint)
{
#pragma HLS PIPELINE II = 1
#pragma HLS INLINE off

	enum openConnState { IDLE, WAIT_CON };

	static openConnState openFsmState = IDLE;

	static struct internalOpenConnMeta open;

	switch (openFsmState) {
	case IDLE:
		if (!internalOpenConnMeta.empty()) {
			open = internalOpenConnMeta.read();

			/*
			 * Both ip_address and ip_port are little-endian
			 * (the x86 host order)
			 */
			ipTuple openTuple;
			openTuple.ip_address = open.remote_ip;
			openTuple.ip_port = open.remote_port;
			openTuple.local_port = open.local_port;

			openConnection.write(openTuple);

			openFsmState = WAIT_CON;
		}
		break;
	case WAIT_CON:
		if (!openConStatus.empty()) {
			/*
			 * With our modified version, the returned sessionID
			 * is the local_port.
			 */
			openStatus status = openConStatus.read();
			ap_uint<16> sessionID = status.sessionID;

			/*
			 * Let's send the session id back to the original host.
			 * struct snic_handler_open_conn_reply
			 *
			 * Leave the Eth/IP/UDP headers to the Scala code.
			 */
			net_axis<WIDTH> w;

			w.keep = 0xFFFFFFFFFFFFFFFF;
			w.last = 1;

			w.data(511, 0) = 0;
			//w.data(14*8-1, 12*8) = 0x01A0;
			w.data(SNIC_OP_OFFSET+32-1, SNIC_OP_OFFSET) = SNIC_TCP_HANDLER_OP_OPEN_CONN_REPLY; //op
			w.data(SNIC_OP_OFFSET+32+16-1, SNIC_OP_OFFSET+32) = sessionID; //sessionID
			w.data(SNIC_OP_OFFSET+32+16+16-1, SNIC_OP_OFFSET+32+16) = status.success;

			dataToEndpoint.write(w);

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
	static ap_uint<16> sessionID;
	static ap_uint<16> len;
	static net_axis<WIDTH> wdata;

	switch (sendDataState) {
	case (S_IDLE):
		if (!dataFromEndpoint2TCP.empty()) {
			wdata      = dataFromEndpoint2TCP.read();
			sessionID  = wdata.data(SNIC_OP_OFFSET+1*32+32-1, SNIC_OP_OFFSET+1*32);
			len        = wdata.data(SNIC_OP_OFFSET+2*32+32-1, SNIC_OP_OFFSET+2*32);

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
				 * (NOTE We are not dealing with closed connection)
				 */
				txMetaData.write(appTxMeta(sessionID, len));
				sendDataState = S_IDLE;
			}
		}
		break;
	case (SEND_DATA):
		/*
		 * Ok, we are good to send.
		 * Read the data and send to TCP module.
		 */
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

/*
 * Receive data from TCP module, then send to endhost.
 */
template <int WIDTH>
void server(hls::stream<ap_uint<16> > &listenPort, hls::stream<bool> &listenPortStatus,
	    hls::stream<appNotification> &notifications, hls::stream<appReadRequest> &readRequest,
	    hls::stream<ap_uint<16> > &rxMetaData, hls::stream<net_axis<WIDTH> > &rxData,
	    hls::stream<struct internalListenConnMeta> &internalListenConnMeta,
	    hls::stream<net_axis<WIDTH> > &dataToEndpoint)
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

	/*
	 * TCP notify us that there is incoming data
	 * We immediately send a ReadRequest to read the data from TCP module
	 */
	if (!notifications.empty()) {
		appNotification notification = notifications.read();

		//printf("%s:%d received new data notification.\n", __func__, __LINE__);
		if (notification.length != 0) {
			readRequest.write(appReadRequest(notification.sessionID, notification.length));
		}
	}

	/*
	 * TODO
	 * Those packets will go to endpoints.
	 * We need to attach headers.
	 *
	 * But not sure whether the packets from TCP have headers.
	 */
	switch (serverFsmState) {
	case WAIT_PKG:
		if (!rxMetaData.empty() && !rxData.empty()) {
			rxMetaData.read();

			/*
			 * On the first cycle, we send the READ header
			 */
			net_axis<WIDTH> w;

			w.data(511, 0) = 0;
			w.data(7,0)=0x66;
			w.data(14*8-1, 12*8) = 0xa000;
			w.data(SNIC_OP_OFFSET+32-1, SNIC_OP_OFFSET) = SNIC_TCP_HANDLER_OP_READ;
			w.keep = 0xFFFFFFFFFFFFFFFF;
			w.last = 0;
			dataToEndpoint.write(w);
			serverFsmState = CONSUME;
		}
		break;
	case CONSUME:
		if (!rxData.empty()) {
			net_axis<WIDTH> data = rxData.read();
			dataToEndpoint.write(data);
			if (data.last) {
				serverFsmState = WAIT_PKG;
			}
		}
		break;
	}
}

/*
 * in1 is the reply for open connection
 * in2 is the data from TCP to endpoint
 */
template <int WIDTH>
void mergeOutput(hls::stream<net_axis<WIDTH> > &in1,
		 hls::stream<net_axis<WIDTH> > &in2,
		 hls::stream<net_axis<WIDTH> > &out)
{
	if (!in1.empty()) {
		out.write(in1.read());
	} else if (!in2.empty()) {
		out.write(in2.read());
	}
}

/*
 * This function parse functions sent over from endpoints.
 * There are two type of packets:
 * 1) control: open with a remote ip:port, and listen on a port.
 * 2) data: packets go to TCP modules.
 */
template <int WIDTH>
void parse_packets(hls::stream<net_axis<WIDTH> > &dataFromEndpoint,
		   hls::stream<struct internalOpenConnMeta> &internalOpenConnMeta,
		   hls::stream<struct internalListenConnMeta> &internalListenConnMeta,
		   hls::stream<net_axis<WIDTH> > &dataFromEndpoint2TCP,
		   hls::stream<ap_uint<16> > &closeConnection)
{
	static struct internalOpenConnMeta open;
	static struct internalListenConnMeta listen;

	enum parseState { IDLE, WRITE_DATA };
	static parseState state = IDLE;

	switch (state) {
	case IDLE:
		/*
		 * FAT NOTE
		 *
		 * All these snic fields sent over are little-endian,
		 * the same with x86. This works fine as long as all
		 * parities use little-endian.
		 */
		if (!dataFromEndpoint.empty()) {
			net_axis<WIDTH> w = dataFromEndpoint.read();

			static int op = w.data(SNIC_OP_OFFSET+32-1, SNIC_OP_OFFSET);
			static int local_port = 0;
			static int remote_ip = 0;
			static int remote_port = 0;

			if (op == SNIC_TCP_HANDLER_OP_OPEN_CONN) {
				local_port  = w.data(SNIC_OP_OFFSET+1*32+32-1, SNIC_OP_OFFSET+1*32);
				remote_ip   = w.data(SNIC_OP_OFFSET+2*32+32-1, SNIC_OP_OFFSET+2*32);
				remote_port = w.data(SNIC_OP_OFFSET+3*32+32-1, SNIC_OP_OFFSET+3*32);

				open.local_port = local_port;
				open.remote_ip = remote_ip;
				open.remote_port = remote_port;
				internalOpenConnMeta.write(open);
			} else if (op == SNIC_TCP_HANDLER_OP_LISTEN) {
				//listen.local_port = port;
				internalListenConnMeta.write(listen);
			} else if (op == SNIC_TCP_HANDLER_OP_WRITE) {
				unsigned int length;

				/*
					struct snic_handler_write_req {
						struct common_headers common_headers;
						uint32_t op;
						uint32_t local_port;
						uint32_t length;
						char data[];
					} __packed;
				*/
				// local_port = w.data(SNIC_OP_OFFSET+1*32+32-1, SNIC_OP_OFFSET+1*32);
				// length     = w.data(SNIC_OP_OFFSET+2*32+32-1, SNIC_OP_OFFSET+2*32);
				dataFromEndpoint2TCP.write(w);

				state = WRITE_DATA;
			} else if (op == SNIC_TCP_HANDLER_OP_CLOSE) {
				closeConnection.write(0);
			}
		}
		break;
	case WRITE_DATA:
		if (!dataFromEndpoint.empty()) {
			net_axis<WIDTH> data = dataFromEndpoint.read();
			dataFromEndpoint2TCP.write(data);
			if (data.last) {
				state = IDLE;
			}
		}
		break;
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
#pragma HLS STREAM variable = txStatusBuffer depth = 16

	static hls::stream<struct internalOpenConnMeta> internalOpenConnMeta("internalOpenConnMeta");
	static hls::stream<struct internalListenConnMeta> internalListenConnMeta("internalListenConnMeta");
#pragma HLS STREAM variable = internalOpenConnMeta depth = 8
#pragma HLS STREAM variable = internalListenConnMeta depth = 8

	static hls::stream<net_axis<DATA_WIDTH> > dataFromEndpoint2TCP("dataFromEndpoint2TCP");
#pragma HLS STREAM variable = dataFromEndpoint2TCP depth = 8

	static hls::stream<net_axis<DATA_WIDTH> > reply_dataToEndpoint("dataFromEndpoint2TCP");
#pragma HLS STREAM variable = reply_dataToEndpoint depth = 8

	static hls::stream<net_axis<DATA_WIDTH> > data_dataToEndpoint("dataFromEndpoint2TCP");
#pragma HLS STREAM variable = data_dataToEndpoint depth = 8

	buffer_txStatus(txStatus, txStatusBuffer);

	parse_packets<DATA_WIDTH>(dataFromEndpoint, internalOpenConnMeta,
				  internalListenConnMeta, dataFromEndpoint2TCP,
				  closeConnection);

	client<DATA_WIDTH>(openConnection, openConStatus, txMetaData, txData,
			   txStatusBuffer, internalOpenConnMeta, dataFromEndpoint2TCP,
			   reply_dataToEndpoint);

	server<DATA_WIDTH>(listenPort, listenPortStatus, notifications, readRequest, rxMetaData, rxData,
			   internalListenConnMeta,
			   data_dataToEndpoint);

	mergeOutput<DATA_WIDTH>(reply_dataToEndpoint, data_dataToEndpoint, dataToEndpoint);
}
