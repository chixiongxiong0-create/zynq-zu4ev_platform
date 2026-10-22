`timescale 1ns/1ps
// One shared source index/tick, independent of both channels' READY/commands.
module capture_source_clock_v2(input wire clk,resetn,valid,sof,
 output reg[31:0]source_seq,output reg[63:0]source_tick);
 always@(posedge clk)begin
  if(!resetn)begin source_seq<=32'hffffffff;source_tick<=0;end
  else begin source_tick<=source_tick+1;if(valid&&sof)source_seq<=source_seq+1;end
 end
endmodule
