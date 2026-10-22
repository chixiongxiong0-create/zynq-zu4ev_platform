`timescale 1ns/1ps
// Preserve the capture interface; the active ISP is sc130_tonemap_yuy2.
// BYPASS=1 retains the uncorrected legacy linear path for A/B comparison.
module raw10_to_yuy2 #(
 parameter BYPASS=0
)(input wire clk,resetn,
 input wire [19:0] s_data,input wire s_valid,s_sof,s_eol,output wire s_ready,
 output wire [31:0] m_data,output wire m_valid,m_sof,m_eol,input wire m_ready);
 assign s_ready=~m_valid|m_ready;
 generate if(BYPASS) begin: legacy
  function [7:0] linear_luma;
   input [9:0] raw;
   reg [18:0] scaled;
   begin scaled=raw*19'd219+19'd511;linear_luma=8'd16+scaled/19'd1023;end
  endfunction
  reg [31:0] data_reg;
  always @(posedge clk) begin
   if(!resetn) data_reg<=0;
   else if(s_ready && s_valid)
    data_reg<={8'h80,linear_luma(s_data[19:10]),8'h80,linear_luma(s_data[9:0])};
  end
  reg valid_reg,sof_reg,eol_reg;
  always @(posedge clk) begin
   if(!resetn) begin valid_reg<=0;sof_reg<=0;eol_reg<=0;end
   else if(s_ready) begin
    valid_reg<=s_valid;sof_reg<=s_valid&&s_sof;eol_reg<=s_valid&&s_eol;
   end
  end
  assign m_data=data_reg;
  assign m_valid=valid_reg;
  assign m_sof=sof_reg;
  assign m_eol=eol_reg;
 end else begin: isp
  sc130_tonemap_yuy2 tone_map (
   .clk(clk),.rst_n(resetn),.ce(s_ready),
   .s_valid(s_valid),.s_sof(s_sof),.s_eol(s_eol),
   .s_raw0(s_data[9:0]),.s_raw1(s_data[19:10]),
   .m_valid(m_valid),.m_sof(m_sof),.m_eol(m_eol),.m_yuy2(m_data));
 end endgenerate
endmodule
