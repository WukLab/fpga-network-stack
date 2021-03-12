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
	ap_uint<1> runExperiment;
	ap_uint<13> useConn;
	ap_uint<8> pkgWordCount;

	ap_uint<32> ipAddress0 = 0x01010101;

	pkgWordCount = 8;

	hls::stream<net_axis<DATA_WIDTH> > dataFromEndpoint("dataFromEndpoint");
	hls::stream<net_axis<DATA_WIDTH> > dataToEndpoint("dataToEndpoint");

	int count = 0;
	while (count < 10000) {
		useConn = 1;
		runExperiment = 0;

		if (count == 20) {
			runExperiment = 1;
		}

		snic_handler(listenPort, listenPortStatus, notifications, readRequest, rxMetaData,
			     rxData, openConnection, openConStatus, closeConnection, txMetaData,
			     txData, txStatus, runExperiment, useConn, pkgWordCount,
			     dataFromEndpoint, dataToEndpoint);

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
			std::cout << "txMeataData New Pkg: " << std::dec << meta.sessionID
				  << ", length[B]: " << meta.length << std::endl;

			int toss = rand() % 2;
			toss = (toss == 0 || meta.length == IPERF_TCP_HEADER_SIZE / 8) ? 0 : -1;
			std::cout << "toss: " << toss << std::endl;
			txStatus.write(appTxRsp(meta.sessionID, meta.length, 0xFFFF, toss));
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
