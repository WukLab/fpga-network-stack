`timescale 1ns / 1ps
`default_nettype none

//`define IP_VERSION4
//`define POINTER_CHASING

`include "davos_types.svh"

module network_stack #(
    parameter NET_BANDWIDTH = 100,
    parameter WIDTH = 512,
    parameter TCP_EN = 1,
    parameter RX_DDR_BYPASS_EN = 0,
    parameter NUM_TCP_CHANNELS = 1
)(
    input wire          net_clk,
    input wire          net_aresetn,

    axi_stream.slave        s_axis_net,
    axi_stream.master       m_axis_net,

    //TCP/IP Interface
    // memory cmd streams
    axis_mem_cmd.master    m_axis_read_cmd[NUM_TCP_CHANNELS],
    axis_mem_cmd.master    m_axis_write_cmd[NUM_TCP_CHANNELS],
    axis_mem_status.slave     s_axis_read_sts[NUM_TCP_CHANNELS],
    axis_mem_status.slave     s_axis_write_sts[NUM_TCP_CHANNELS],
    axi_stream.slave    s_axis_read_data[NUM_TCP_CHANNELS],
    axi_stream.master   m_axis_write_data[NUM_TCP_CHANNELS],

    //Application interface streams
    axis_meta.slave     s_axis_listen_port,
    axis_meta.master    m_axis_listen_port_status,
   
    axis_meta.slave     s_axis_open_connection,
    axis_meta.master    m_axis_open_status,
    axis_meta.slave     s_axis_close_connection,

    axis_meta.master    m_axis_notifications,
    axis_meta.slave     s_axis_read_package,
    
    axis_meta.master    m_axis_rx_metadata,
    axi_stream.master   m_axis_rx_data,
    
    axis_meta.slave     s_axis_tx_metadata,
    axi_stream.slave    s_axis_tx_data,
    axis_meta.master    m_axis_tx_status,
    
    input wire[31:0]    local_ip_address
);

logic       session_count_valid;
logic[15:0] session_count_data;
 
tcp_stack #(
     .TCP_EN(TCP_EN),
     .WIDTH(WIDTH),
     .RX_DDR_BYPASS_EN(RX_DDR_BYPASS_EN)
 ) tcp_stack_inst(
     .net_clk(net_clk),
     .net_aresetn(net_aresetn),
     
     // streams to network
     .s_axis_rx_data(s_axis_net),
     .m_axis_tx_data(m_axis_net),

     // memory cmd streams
     .m_axis_mem_read_cmd(m_axis_read_cmd),
     .m_axis_mem_write_cmd(m_axis_write_cmd),
     .s_axis_mem_read_sts(s_axis_read_sts),
     .s_axis_mem_write_sts(s_axis_write_sts),
     .s_axis_mem_read_data(s_axis_read_data),
     .m_axis_mem_write_data(m_axis_write_data),
     
     //Application
     .s_axis_listen_port(s_axis_listen_port),
     .m_axis_listen_port_status(m_axis_listen_port_status),
     
     .s_axis_open_connection(s_axis_open_connection),
     .m_axis_open_status(m_axis_open_status),
     .s_axis_close_connection(s_axis_close_connection),
     
     .m_axis_notifications(m_axis_notifications),
     .s_axis_read_package(s_axis_read_package),
     
     .m_axis_rx_metadata(m_axis_rx_metadata),
     .m_axis_rx_data(m_axis_rx_data),
     
     .s_axis_tx_metadata(s_axis_tx_metadata),
     .s_axis_tx_data(s_axis_tx_data),
     .m_axis_tx_status(m_axis_tx_status),
     
     // NOTE Yizhou
     // We will use a separate module to manipulate
     // ip and mac anyway. so no need to use a valid IP.
     .local_ip_address(local_ip_address),
     .session_count_valid(session_count_valid),
     .session_count_data(session_count_data)
);

endmodule

`default_nettype wire
