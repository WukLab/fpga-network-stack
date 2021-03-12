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
	input.last = 1;
	input.keep = 0xFFFFFFFFFFFFFFFF;
	input.data(1, 0) = 0;
	input.data(32,1) = 0;
	input.data(47,32) = 0;

	dataFromEndpoint.write(input);

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

	int count = 0;
	while (count < 1000) {
		snic_handler(listenPort, listenPortStatus, notifications, readRequest, rxMetaData,
			     rxData, openConnection, openConStatus, closeConnection, txMetaData,
			     txData, txStatus,
			     dataFromEndpoint, dataToEndpoint);

		if (count == 500) {
			appNotification n;

			n.sessionID = 0;
			n.length = 64;
			notifications.write(n);
		}

		if (!readRequest.empty()) {
			readRequest.read();
			printf("readRequest valid: module is asking for data\n");
			rxMetaData.write(0);

			net_axis<DATA_WIDTH> d;
			d.last = 1;
			d.keep = 0xFFFFFFFFFFFFFFFF;
			rxData.write(d);
		}

		if (!dataToEndpoint.empty()) {
			dataToEndpoint.read();
			printf("dataToEndpoint valid\n");
		}

		if (!listenPort.empty()) {
			ap_uint<16> port = listenPort.read();
			std::cout << "Port " << port << " openend." << std::endl;
			listenPortStatus.write(true);
		}

		if (!openConnection.empty()) {
			openConnection.read();
			std::cout << "Opening connection.. at cycle" << count << std::endl;
			openConStatus.write(openStatus(123 + count, true));
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
