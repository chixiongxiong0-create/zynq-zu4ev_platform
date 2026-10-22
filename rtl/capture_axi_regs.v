`timescale 1ns/1ps
module capture_axi_regs(input wire clk,resetn,
 input wire [7:0] s_axi_awaddr,input wire s_axi_awvalid,output wire s_axi_awready,
 input wire [31:0] s_axi_wdata,input wire [3:0] s_axi_wstrb,input wire s_axi_wvalid,output wire s_axi_wready,
 output wire [1:0] s_axi_bresp,output reg s_axi_bvalid,input wire s_axi_bready,
 input wire [7:0] s_axi_araddr,input wire s_axi_arvalid,output wire s_axi_arready,
 output reg [31:0] s_axi_rdata,output wire [1:0] s_axi_rresp,output reg s_axi_rvalid,input wire s_axi_rready,
 output reg arm,abort_capture,ack,clear_stats,output reg [31:0] arm_epoch,ack_epoch,
 input wire [31:0] status,result_epoch,result_flags,input_seq,result_seq,result_words,result_lines,dropped,overflows,complete,
 input wire result_valid,output wire irq);
 reg have_aw,have_w,irq_enable;reg[7:0]awaddr;reg[31:0]wdata;reg[3:0]wstrb;
 assign s_axi_awready=!have_aw&&!s_axi_bvalid;
 assign s_axi_wready=!have_w&&!s_axi_bvalid;
 assign s_axi_arready=!s_axi_rvalid;
 assign s_axi_bresp=0;assign s_axi_rresp=0;assign irq=result_valid&&irq_enable;
 integer i;
 always @(posedge clk) begin
  if(!resetn) begin
   have_aw<=0;have_w<=0;s_axi_bvalid<=0;s_axi_rvalid<=0;s_axi_rdata<=0;
   arm<=0;abort_capture<=0;ack<=0;clear_stats<=0;arm_epoch<=0;ack_epoch<=0;irq_enable<=0;awaddr<=0;wdata<=0;wstrb<=0;
  end else begin
   arm<=0;abort_capture<=0;ack<=0;clear_stats<=0;
   if(s_axi_awvalid&&s_axi_awready) begin have_aw<=1;awaddr<=s_axi_awaddr;end
   if(s_axi_wvalid&&s_axi_wready) begin have_w<=1;wdata<=s_axi_wdata;wstrb<=s_axi_wstrb;end
   if(s_axi_bvalid&&s_axi_bready)s_axi_bvalid<=0;
   if(have_aw&&have_w&&!s_axi_bvalid) begin
    have_aw<=0;have_w<=0;s_axi_bvalid<=1;
    case(awaddr)
     8'h08:if(wstrb[0])begin arm<=wdata[0];abort_capture<=wdata[1];clear_stats<=wdata[2];end
     8'h10:for(i=0;i<4;i=i+1)if(wstrb[i])arm_epoch[8*i+:8]<=wdata[8*i+:8];
     8'h1c:if(&wstrb)begin ack_epoch<=wdata;ack<=1;end
     8'h3c:if(wstrb[0])irq_enable<=wdata[0];
     default:;
    endcase
   end
   if(s_axi_rvalid&&s_axi_rready)s_axi_rvalid<=0;
   if(s_axi_arvalid&&s_axi_arready) begin
    s_axi_rvalid<=1;
    case(s_axi_araddr)
     8'h00:s_axi_rdata<=32'h55564331;8'h04:s_axi_rdata<=32'h00010000;
     8'h0c:s_axi_rdata<=status;8'h10:s_axi_rdata<=arm_epoch;8'h14:s_axi_rdata<=result_epoch;
     8'h18:s_axi_rdata<=result_flags;8'h20:s_axi_rdata<=input_seq;8'h24:s_axi_rdata<=result_seq;
     8'h28:s_axi_rdata<=result_words;8'h2c:s_axi_rdata<=result_lines;8'h30:s_axi_rdata<=dropped;
     8'h34:s_axi_rdata<=overflows;8'h38:s_axi_rdata<=complete;8'h3c:s_axi_rdata<={31'b0,irq_enable};
     default:s_axi_rdata<=0;
    endcase
   end
  end
 end
endmodule
