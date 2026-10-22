`timescale 1ns/1ps
// Passive monitor at the VDMA memory master boundary. The fixed aligned format
// emits only full 128-bit beats. Count AW/WLAST/B independently: W can precede AW.
module vdma_write_guard #(parameter EXPECTED_BYTES=2621440)(
 input wire clk,resetn,start,awvalid,awready,wvalid,wready,wlast,
 input wire[15:0]wstrb,input wire bvalid,bready,input wire[1:0]bresp,
 output reg retired,error,output reg[31:0]bytes);
 reg [31:0]aw_count,last_count,b_count;reg armed;
 always@(posedge clk)begin
  if(!resetn)begin retired<=0;error<=0;bytes<=0;aw_count<=0;last_count<=0;b_count<=0;armed<=0;end
  else if(start)begin retired<=0;error<=0;bytes<=0;aw_count<=0;last_count<=0;b_count<=0;armed<=1;end
  else if(armed)begin
   if(awvalid&&awready)aw_count<=aw_count+1;
   if(wvalid&&wready)begin
    bytes<=bytes+16;
    if(wlast)last_count<=last_count+1;
    if(wstrb!=16'hffff||bytes>=EXPECTED_BYTES)error<=1;
   end
   if(bvalid&&bready)begin b_count<=b_count+1;if(bresp!=0)error<=1;end
   if(bytes==EXPECTED_BYTES&&aw_count!=0&&aw_count==last_count&&aw_count==b_count&&!error)retired<=1;
  end
 end
endmodule
