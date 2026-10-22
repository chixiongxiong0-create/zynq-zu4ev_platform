`timescale 1ns/1ps
module uvc_capture_path(input wire clk,resetn,
 input wire [19:0] raw,input wire valid,sof,eol,source_error,dma_done,dma_error,
 output wire[31:0] m_data,output wire m_valid,m_sof,m_eol,input wire m_ready,
 input wire [7:0] s_axi_awaddr,input wire s_axi_awvalid,output wire s_axi_awready,
 input wire [31:0] s_axi_wdata,input wire [3:0] s_axi_wstrb,input wire s_axi_wvalid,output wire s_axi_wready,
 output wire [1:0] s_axi_bresp,output wire s_axi_bvalid,input wire s_axi_bready,
 input wire [7:0] s_axi_araddr,input wire s_axi_arvalid,output wire s_axi_arready,
 output wire [31:0] s_axi_rdata,output wire [1:0] s_axi_rresp,output wire s_axi_rvalid,input wire s_axi_rready,
 output wire irq,epoch_start);
 wire[31:0] converted;wire cv,cs,ce;
 raw10_to_yuy2 converter(.clk(clk),.resetn(resetn),.s_data(raw),.s_valid(valid),.s_sof(sof),.s_eol(eol),.s_ready(),.m_data(converted),.m_valid(cv),.m_sof(cs),.m_eol(ce),.m_ready(1'b1));
 wire arm,abort_capture,ack,clear_stats,result_valid;
 wire[31:0] arm_epoch,ack_epoch,status,result_epoch,result_flags,input_seq,result_seq,result_words,result_lines,dropped,overflows,complete;
 // Source error must align with the converter's one-cycle latency.
 reg error_d;always@(posedge clk) if(!resetn)error_d<=0;else error_d<=source_error;
 capture_frame_gate gate(.clk(clk),.resetn(resetn),.arm(arm),.abort_capture(abort_capture),.ack(ack),.clear_stats(clear_stats),.arm_epoch(arm_epoch),.ack_epoch(ack_epoch),
 .s_data(converted),.s_valid(cv),.s_sof(cs),.s_eol(ce),.source_error(error_d),.dma_done(dma_done),.dma_error(dma_error),.m_data(m_data),.m_valid(m_valid),.m_sof(m_sof),.m_eol(m_eol),.m_ready(m_ready),
 .status(status),.result_valid(result_valid),.result_epoch(result_epoch),.result_flags(result_flags),.input_seq(input_seq),.result_seq(result_seq),.result_words(result_words),.result_lines(result_lines),.dropped(dropped),.overflows(overflows),.complete(complete));
 capture_axi_regs regs(.clk(clk),.resetn(resetn),
 .s_axi_awaddr(s_axi_awaddr),.s_axi_awvalid(s_axi_awvalid),.s_axi_awready(s_axi_awready),.s_axi_wdata(s_axi_wdata),.s_axi_wstrb(s_axi_wstrb),.s_axi_wvalid(s_axi_wvalid),.s_axi_wready(s_axi_wready),.s_axi_bresp(s_axi_bresp),.s_axi_bvalid(s_axi_bvalid),.s_axi_bready(s_axi_bready),
 .s_axi_araddr(s_axi_araddr),.s_axi_arvalid(s_axi_arvalid),.s_axi_arready(s_axi_arready),.s_axi_rdata(s_axi_rdata),.s_axi_rresp(s_axi_rresp),.s_axi_rvalid(s_axi_rvalid),.s_axi_rready(s_axi_rready),
 .arm(arm),.abort_capture(abort_capture),.ack(ack),.clear_stats(clear_stats),.arm_epoch(arm_epoch),.ack_epoch(ack_epoch),.status(status),.result_valid(result_valid),.result_epoch(result_epoch),.result_flags(result_flags),.input_seq(input_seq),.result_seq(result_seq),.result_words(result_words),.result_lines(result_lines),.dropped(dropped),.overflows(overflows),.complete(complete),.irq(irq));
 assign epoch_start=arm&&status[0]&&!abort_capture;
endmodule
