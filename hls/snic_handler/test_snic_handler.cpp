#include "snic_handler_config.hpp"
#include "snic_handler.hpp"
#include <iostream>

using namespace hls;

int main()
{
	hls::stream<ap_uint<16> > listenPort("listenPort");
	hls::stream<bool> listenPortStatus("listenPortStatus");
	hls::stream<appNotification> notifications("notifications");
	hls::stream<appReadRequest> readRequest("readRequest");
	hls::stream<ap_uint<16> > rxMetaData("rxMetaData");
	hls::stream<net_axis<DATA_WIDTH> > rxData("rxData");
	hls::stream<ipTuple> openConnection("openConnection");
	hls::stream<openStatus> openConStatus("openConStatus");
	hls::stream<ap_uint<16> > closeConnection("closeConnection");
	hls::stream<appTxMeta> txMetaData("txMetaData");
	hls::stream<net_axis<DATA_WIDTH> > txData("txData");
	hls::stream<appTxRsp> txStatus("txStatus");

	hls::stream<net_axis<DATA_WIDTH> > dataFromEndpoint("dataFromEndpoint");
	hls::stream<net_axis<DATA_WIDTH> > dataToEndpoint("dataToEndpoint");

	net_axis<DATA_WIDTH> input;

	/*
	 * OPEN request
	 */
	input.last = 1;
	input.keep = 0xFFFFFFFFFFFFFFFF;
	input.data(SNIC_OP_OFFSET+32-1, SNIC_OP_OFFSET) = SNIC_TCP_HANDLER_OP_OPEN_CONN; //op
	input.data(SNIC_OP_OFFSET+1*32+32-1, SNIC_OP_OFFSET+1*32) = 8888; //local_port
	input.data(SNIC_OP_OFFSET+2*32+32-1, SNIC_OP_OFFSET+2*32) = 0x7f000001; //remote_ip
	input.data(SNIC_OP_OFFSET+3*32+32-1, SNIC_OP_OFFSET+3*32) = 9999; //remote_port
	dataFromEndpoint.write(input);

#if 0
	/*
	 * WRITE request
	 */
	input.data(SNIC_OP_OFFSET+32-1, SNIC_OP_OFFSET) = SNIC_TCP_HANDLER_OP_WRITE; //op
	input.data(SNIC_OP_OFFSET+1*32+32-1, SNIC_OP_OFFSET+1*32) = 8888; //local_port
	input.data(SNIC_OP_OFFSET+2*32+32-1, SNIC_OP_OFFSET+2*32) = 0x100; //length
	dataFromEndpoint.write(input);
#endif

#if 0
	input.data(1, 0) = 2;
	dataFromEndpoint.write(input);
	dataFromEndpoint.write(input);

	input.last = 0;
	dataFromEndpoint.write(input);
	dataFromEndpoint.write(input);
	input.last = 1;
	dataFromEndpoint.write(input);

	input.data(1,0) = 1;
	input.last = 1;
	dataFromEndpoint.write(input);
#endif

	int count = 0;
	while (count < 1000) {
		snic_handler(listenPort, listenPortStatus, notifications, readRequest, rxMetaData,
			     rxData, openConnection, openConStatus, closeConnection, txMetaData,
			     txData, txStatus,
			     dataFromEndpoint, dataToEndpoint);

		if (count == 500) {
			appNotification n;

			n.sessionID = 8;
			n.length = 64;
			notifications.write(n);
		}

		if (!readRequest.empty()) {
			readRequest.read();
			printf("readRequest valid: module is asking for data. Sending data to it..\n");

			/*
			 * sessionId
			 */
			rxMetaData.write(0);

			/*
			 * prepare fake data.
			 */
			net_axis<DATA_WIDTH> d;
			d.last = 1;
			d.keep = 0xFFFFFFFFFFFFFFFF;
			d.data = 0;
			rxData.write(d);
		}

		/*
		 * This is data from snic to endhost.
		 */
		if (!dataToEndpoint.empty()) {
			net_axis<DATA_WIDTH> currWord = dataToEndpoint.read();

			printf("dataToEndpoint valid: op=%#x %x || ",
				currWord.data(SNIC_OP_OFFSET+32-1, SNIC_OP_OFFSET),
				currWord.data(31, 0)
			);
			print(std::cout, currWord);
			std::cout << std::endl;
		}

		if (!listenPort.empty()) {
			ap_uint<16> port = listenPort.read();
			std::cout << "Port " << port << " openend." << std::endl;
			listenPortStatus.write(true);
		}

		/*
		 * Handle open connection request
		 */
		if (!openConnection.empty()) {
			openConnection.read();
			std::cout << "Opening connection.. at cycle" << count << std::endl;

			ap_uint<16> sessionID;

			sessionID = 0x66;
			openConStatus.write(openStatus(sessionID, true));
		}

		if (!txMetaData.empty()) {
			appTxMeta meta = txMetaData.read();
			std::cout << "txMeataData New Pkg Coming: " << std::dec << meta.sessionID
				  << ", length[B]: " << meta.length << std::endl;

			txStatus.write(appTxRsp(meta.sessionID, meta.length, 0xFFFF, 0));
		}

		while (!txData.empty()) {
			net_axis<DATA_WIDTH> currWord = txData.read();
			printLE(std::cout, currWord);
			std::cout << std::endl;
		}

		if (!closeConnection.empty()) {
			ap_uint<16> sessionID = closeConnection.read();
			std::cout << "Closing connection: " << std::dec << sessionID << std::endl;
		}
		count++;
	}
	return 0;
}
