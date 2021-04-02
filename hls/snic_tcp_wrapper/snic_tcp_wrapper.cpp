#include "snic_tcp_wrapper.hpp"
#include "snic_tcp_wrapper_config.hpp"
#include <iostream>

const int ETH_HEADER_SIZE = 112;

template <int D>
struct net_axis_dirty
{
	ap_uint<D>		data;
	ap_uint<D/8>		keep;
	ap_uint<1>		last;
	ap_uint<1>		dest;

	net_axis_dirty() {}
	net_axis_dirty(ap_uint<D> data,
		 ap_uint<D/8> keep,
		 ap_uint<1> last,
		 ap_uint<1> dest)
		:data(data), keep(keep), last(last), dest(dest) {}
};

// The 2nd template parameter is a hack to use this function multiple times
template <int W, int whatever>
void lshiftWordByOctet_dirty(	uint16_t offset,
						hls::stream<net_axis_dirty<W> >& input,
						hls::stream<net_axis_dirty<W> >& output)
{
#pragma HLS inline off
#pragma HLS pipeline II=1
	static bool ls_firstWord = true;
	static bool ls_writeRemainder = false;
	static net_axis_dirty<W> prevWord;

	net_axis_dirty<W> currWord;
	net_axis_dirty<W> sendWord;

	//TODO use states
	if (ls_writeRemainder)
	{
		sendWord.data((8*offset)-1, 0) = prevWord.data((W-1), W-(8*offset));
		sendWord.data((W-1), (8*offset)) = 0;
		sendWord.keep(offset-1, 0) = prevWord.keep((W/8-1), (W/8)-offset);
		sendWord.keep((W/8-1), offset) = 0;
		sendWord.last = 1;

		output.write(sendWord);
		ls_writeRemainder = false;
	}
	else if (!input.empty())
	{
		input.read(currWord);

		if (offset == 0)
		{
			output.write(currWord);
		}
		else
		{

			if (ls_firstWord)
			{
				sendWord.data((8*offset)-1, 0) = 0;
				sendWord.data((W-1), (8*offset)) = currWord.data((W-1)-(8*offset), 0);
				sendWord.keep(offset-1, 0) = 0xFFFFFFFF;
				sendWord.keep((W/8-1), offset) = currWord.keep((W/8-1)-offset, 0);
				sendWord.last = (currWord.keep((W/8-1), (W/8)-offset) == 0);
			}
			else
			{
				sendWord.data((8*offset)-1, 0) = prevWord.data((W-1), W-(8*offset));
				sendWord.data((W-1), (8*offset)) = currWord.data((W-1)-(8*offset), 0);

				sendWord.keep(offset-1, 0) = prevWord.keep((W/8-1), (W/8)-offset);
				sendWord.keep((W/8-1), offset) = currWord.keep((W/8-1)-offset, 0);

				sendWord.last = (currWord.keep((W/8-1), (W/8)-offset) == 0);

			}
			output.write(sendWord);

			prevWord = currWord;
			ls_firstWord = false;
			if (currWord.last)
			{
				ls_firstWord = true;
				ls_writeRemainder = !sendWord.last;
			}
		} //else offset
	}

}

void snic_tcp_wrapper(
		hls::stream<net_axis_dirty<DATA_WIDTH> > &toTCP_in,
		hls::stream<net_axis_dirty<DATA_WIDTH> > &toTCP_out,
		hls::stream<net_axis_dirty<DATA_WIDTH> > &fromTCP_in,
		hls::stream<net_axis_dirty<DATA_WIDTH> > &fromTCP_out)
{
#pragma HLS DATAFLOW disable_start_propagation
#pragma HLS INTERFACE ap_ctrl_none port = return

#pragma HLS INTERFACE axis register port=toTCP_in     name=s_axis_totcp_in
#pragma HLS INTERFACE axis register port=toTCP_out    name=m_axis_totcp_out
#pragma HLS INTERFACE axis register port=fromTCP_in   name=s_axis_fromtcp_in
#pragma HLS INTERFACE axis register port=fromTCP_out  name=m_axis_fromtcp_out

	//static hls::stream<net_axis_dirty<WIDTH> >		toTCP_in_fifo;

	/*
	 * Shift right to extract the ethernet headers.
	 * in: ETH, IP, TCP
	 * out: IP, TCP
	 */
	rshiftWordByOctet<net_axis_dirty<DATA_WIDTH>, DATA_WIDTH, 1> (
		((ETH_HEADER_SIZE % DATA_WIDTH) / 8),
		toTCP_in, toTCP_out
	);

	/*
	 * Shift left to add the ethernet headers.
	 * in: IP, TCP
	 * out: Eth, IP, TCP
	 */
	lshiftWordByOctet_dirty<DATA_WIDTH, 1> (
		((ETH_HEADER_SIZE % DATA_WIDTH) / 8),
		fromTCP_in,
		fromTCP_out
	);
}
