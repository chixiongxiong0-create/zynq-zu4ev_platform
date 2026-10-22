`timescale 1ns/1ps
// Exercise the production guard, not a reimplementation. Address coverage is
// deliberately external: the production guard has no address input.
module tb_write_completeness;
 localparam N=2621440/16;
 reg clk=0; always #2.5 clk=~clk;
 reg resetn=0,start=0,av=0,wv=0,wl=0,bv=0;
 reg [15:0] strb=16'hffff;
 reg [1:0] resp=0;
 wire retired,error,quiet; wire [31:0] bytes;
 bit visited[0:N-1]; integer unique_beats;
 vdma_write_guard_v2 dut(.clk(clk),.resetn(resetn),.start(start),
  .awvalid(av),.awready(1'b1),.wvalid(wv),.wready(1'b1),
  .wlast(wl),.wstrb(strb),.bvalid(bv),.bready(1'b1),.bresp(resp),
  .retired(retired),.error(error),.quiescent(quiet),.bytes(bytes));
 task idle; begin @(negedge clk); av=0;wv=0;wl=0;bv=0; @(posedge clk); #1; end endtask
 task begin_epoch; begin
  idle; start=1; @(posedge clk); #1; @(negedge clk); start=0;
  strb=16'hffff;resp=0; unique_beats=0;
  for(integer i=0;i<N;i=i+1)visited[i]=0;
 end endtask
 // 16 beats per burst. The scoreboard models either correct addresses or a
 // faulty address generator that overwrites row zero for every source row.
 task frame; input integer beats,alias_rows,partial,bad_response,hold_last_b;
 integer a;
 begin
  for(integer i=0;i<beats;i=i+1)begin
   @(negedge clk); av=(i%16==0);wv=1;wl=(i%16==15);
   bv=(i%16==15)&&!(hold_last_b&&i==beats-1);
   strb=(partial&&i==100)?16'h00ff:16'hffff;
   resp=(bad_response&&i==15)?2'b10:0;
   a=alias_rows?(i%(2560/16)):i;
   if(!visited[a])begin visited[a]=1;unique_beats=unique_beats+1;end
   @(posedge clk); #1;
  end
  idle;idle;
 end endtask
 initial begin
  repeat(3)@(posedge clk); @(negedge clk);resetn=1;
  begin_epoch; frame(N,0,0,0,0);
  if(!retired||error||!quiet||bytes!=2621440||unique_beats!=N)$fatal(1,"full frame");
  $display("CASE_PASS complete contiguous full-size frame");
  begin_epoch; frame(N-1,0,0,0,0);
  if(retired)$fatal(1,"short frame accepted");
  $display("CASE_PASS one missing beat rejected");
  begin_epoch; frame(N,0,1,0,0);
  if(retired||!error)$fatal(1,"partial strobe accepted");
  $display("CASE_PASS partial write strobe rejected");
  begin_epoch; frame(N,0,0,1,0);
  if(retired||!error)$fatal(1,"BRESP error accepted");
  $display("CASE_PASS failed write response rejected");
  begin_epoch; frame(N,0,0,0,1);
  if(retired||quiet)$fatal(1,"early retirement before final response");
  @(negedge clk);bv=1; @(posedge clk);#1;idle;idle;
  if(!retired||error||!quiet)$fatal(1,"final response not retired");
  $display("CASE_PASS waits for final write response");
  begin_epoch; frame(N,1,0,0,0);
  if(!retired||error||!quiet||unique_beats!=160)$fatal(1,"address blindspot changed");
  $display("BLINDSPOT_CONFIRMED bytes=%0d unique_bytes=%0d retired=%0d",bytes,unique_beats*16,retired);
  $display("TEST_PASS six cases; address blindspot is NOT a reproduced hardware fault");
  $finish;
 end
 initial begin #20000000; $fatal(1,"watchdog"); end
endmodule
