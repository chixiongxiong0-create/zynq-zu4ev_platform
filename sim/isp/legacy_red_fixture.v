`timescale 1ns/1ps
// One-stage elastic AXIS converter. Source must obey s_ready; the camera uses
// m_ready=1 and the downstream frame gate absorbs/drops unthrottleable input.
module raw10_to_yuy2 #(parameter BYPASS=0)(input wire clk,resetn,
 input wire [19:0] s_data,input wire s_valid,s_sof,s_eol,output wire s_ready,
 output reg [31:0] m_data,output reg m_valid,m_sof,m_eol,input wire m_ready);
 function [7:0] luma;
  input [9:0] raw;
  reg [18:0] scaled;
  begin scaled=raw*19'd219+19'd511; luma=8'd16+scaled/19'd1023; end
 endfunction
 assign s_ready=~m_valid|m_ready;
 always @(posedge clk) begin
  if(!resetn) begin m_valid<=0;m_sof<=0;m_eol<=0;m_data<=0;end
  else if(s_ready) begin
   m_valid<=s_valid;
   if(s_valid) begin m_data<={8'h80,luma(s_data[19:10]),8'h80,luma(s_data[9:0])};m_sof<=s_sof;m_eol<=s_eol;end
  end
 end
endmodule

