`timescale 1ns/1ps
module tb_isp;
 reg clk=0; always #5 clk=~clk;
 reg resetn=0,sv=0,ss=0,se=0; reg [19:0] sd=0;
 reg mr=1;
 reg [31:0] held_data,held_bypass;
 reg held_sof,held_eol;
 wire ready; wire [31:0] md,bd; wire mv,ms,me,bv,bs,be;
 reg packet_error=0,global_fault=0,packet_d=0,fault_d=0;
 always @(posedge clk) if(!resetn) begin packet_d<=0;fault_d<=0;end else begin packet_d<=packet_error;fault_d<=global_fault;end
 reg [7:0] lut[0:1023]; integer i,n=0; integer prev;
 isp dut(clk,resetn,sd,sv,ss,se,ready,md,mv,ms,me,mr);
 isp #(.BYPASS(1)) bypass(clk,resetn,sd,sv,ss,se,,bd,bv,bs,be,mr);
 wire old_ready,old_bready;
 wire [31:0] old_md,old_bd;
 wire old_mv,old_ms,old_me,old_bv,old_bs,old_be;
 raw10_to_yuy2 original(clk,resetn,sd,sv,ss,se,old_ready,old_md,old_mv,old_ms,old_me,mr);
 raw10_to_yuy2 #(.BYPASS(1)) original_bypass(clk,resetn,sd,sv,ss,se,old_bready,old_bd,old_bv,old_bs,old_be,mr);
 always @(posedge clk) begin
  #1;
  if({ready,md,mv,ms,me,bd,bv,bs,be} !==
     {old_ready,old_md,old_mv,old_ms,old_me,old_bd,old_bv,old_bs,old_be})
   $fatal(1,"ISP top changed legacy data, latency, markers or backpressure");
 end
 function [7:0] linear; input integer a; begin linear=16+(a*219+511)/1023; end endfunction
 function [7:0] corrected_tone; input integer a; integer normalized;
 begin
 normalized=a<=52 ? 0 : ((a-52)*1023+485)/971;
 corrected_tone=lut[normalized];
 end endfunction
 task tick; input integer a,b,v,s,e,r; reg [31:0] exp,lexp;
 begin
 @(negedge clk); sd={10'(b),10'(a)}; sv=v;ss=s;se=e;resetn=r;
 packet_error=(n%5==0);global_fault=(n%7==0);
 exp={8'h80,corrected_tone(b),8'h80,corrected_tone(a)}; lexp={8'h80,linear(b),8'h80,linear(a)};
 @(posedge clk); #1;
 if(packet_d!==(r&&(n%5==0))||fault_d!==(r&&(n%7==0))) $fatal(1,"fault latency cycle %0d",n);
 if(ready!==1) $fatal(1,"ready");
 if(mv!==(v&&r)||bv!==(v&&r)) $fatal(1,"valid cycle %0d",n);
 if(ms!==(v&&r&&s)||me!==(v&&r&&e)||bs!==(v&&r&&s)||be!==(v&&r&&e)) $fatal(1,"markers cycle %0d",n);
 if(r&&v&&(md!==exp||bd!==lexp)) $fatal(1,"data cycle %0d raw=%0d,%0d got=%h expected=%h bypass=%h exp=%h",n,a,b,md,exp,bd,lexp);
 n=n+1;
 end endtask
 initial begin
 $readmemh("../../isp/vendor/sc130_tone_lut.mem",lut);
 prev=16; for(i=0;i<1024;i=i+1) begin
 if(lut[i]<prev||lut[i]<16||lut[i]>235) $fatal(1,"invalid LUT %0d",i); prev=lut[i]; end
 if(lut[0]!=16||lut[1023]!=235) $fatal(1,"anchors");
 tick(0,1023,1,1,1,0);
 for(i=0;i<1024;i=i+1) begin
 tick(i,1023-i,1,i%640==0,i%640==639,1);
 tick(1023-i,i,1,i==0,i==1023,1);
 if(i%13==0) tick(0,0,0,1,1,1);
 if(i%101==0) tick(i,i,1,1,1,0);
 end
 for(i=0;i<1280;i=i+1) tick(i%1024,(i*37)%1024,1,i%640==0,i%640==639,1);
 tick(0,0,0,1,1,1); tick(0,0,0,0,0,0); tick(1023,0,1,1,0,1);
 tick(52,53,1,1,1,1);
 held_data=md;held_bypass=bd;held_sof=ms;held_eol=me;
 @(negedge clk);mr=0;sd={10'd1023,10'd128};ss=0;se=0;
 repeat(8) begin
  @(posedge clk);#1;
  if(ready!==0 || md!==held_data || bd!==held_bypass || !mv || !bv ||
     ms!==held_sof || me!==held_eol || bs!==held_sof || be!==held_eol)
   $fatal(1,"stall changed held transaction");
 end
 @(negedge clk);mr=1;
 tick(52,0,1,1,1,1);
 if(md!==32'h80108010) $fatal(1,"black clamp anchor");
 @(negedge clk);mr=0;resetn=0;
 @(posedge clk);#1;
 if(mv!==0 || bv!==0 || ms!==0 || me!==0 || ready!==1)
  $fatal(1,"reset while stalled");
 $display("TEST_PASS black52 normalized_white1023 exhaustive1024 both_lanes bypass gaps resets markers sustained1280 stalls integration_fault_delay cycles=%0d",n); $finish;
 end
 initial begin #1000000; $fatal(1,"watchdog");end
endmodule


