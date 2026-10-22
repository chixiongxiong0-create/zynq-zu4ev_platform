`timescale 1ns/1ps
// Unthrottleable source, single-frame authorization. ACK flush is a stream reset
// and is legal ONLY after software has quiesced the VDMA channel.
module capture_frame_gate #(
 parameter LINE_WORDS=640, FRAME_LINES=1024, FIFO_DEPTH=4096,
 parameter TIMEOUT_CYCLES=20000000
)(input wire clk,resetn,arm,abort_capture,ack,clear_stats,
 input wire [31:0] arm_epoch,ack_epoch,
 input wire [31:0] s_data,input wire s_valid,s_sof,s_eol,source_error,dma_done,dma_error,
 output wire [31:0] m_data,output wire m_valid,m_sof,m_eol,input wire m_ready,
 output wire [31:0] status,output reg result_valid,
 output reg [31:0] result_epoch,result_flags,input_seq,result_seq,result_words,result_lines,dropped,overflows,complete);
 localparam PTR=$clog2(FIFO_DEPTH), IDLE=0,ARMED=1,CAPTURE=2,DRAIN=3,RESULT=4;
 localparam FRAME_WORDS=LINE_WORDS*FRAME_LINES;
 reg [2:0] state;
 (* ram_style="distributed" *) reg [33:0] fifo[0:FIFO_DEPTH-1];
 reg [PTR-1:0] wrptr,rdptr;reg [PTR:0] count;
 reg [31:0] x,y,out_words,timer;
 reg frame_closed,close_pending;
 reg [31:0] close_data;
 wire pop=m_valid&&m_ready&&(count!=0);
 wire close_pop=m_valid&&m_ready&&(count==0)&&close_pending;
 wire selected=s_valid&&((state==ARMED&&s_sof)||state==CAPTURE);
 wire unexpected=(state==CAPTURE&&s_sof);
 wire short_line=selected&&s_eol&&(x!=LINE_WORDS-1);
 wire long_line=selected&&!s_eol&&(x==LINE_WORDS-1);
 wire full=(count==FIFO_DEPTH)&&!pop;
 wire push=selected&&!unexpected&&!short_line&&!long_line&&!full&&!source_error&&!abort_capture;
 assign m_valid=(count!=0)||(close_pending&&dma_done);
 assign {m_sof,m_eol,m_data}=(count!=0)?fifo[rdptr]:{1'b1,1'b0,close_data};
 assign status={26'b0,(result_flags!=0&&result_flags!=1),(count==0&&!close_pending),result_valid,(state==CAPTURE||state==DRAIN),state==ARMED,state==IDLE};
 task fail_frame;
 input [31:0] flags;
 begin
  result_valid<=1;result_flags<=flags;result_words<=out_words+pop;result_lines<=y;
  state<=RESULT;dropped<=dropped+1;
 end
 endtask
 always @(posedge clk) begin
  if(!resetn) begin
   state<=IDLE;wrptr<=0;rdptr<=0;count<=0;x<=0;y<=0;out_words<=0;timer<=0;frame_closed<=0;close_pending<=0;close_data<=0;
   result_valid<=0;result_epoch<=0;result_flags<=0;input_seq<=0;result_seq<=0;
   result_words<=0;result_lines<=0;dropped<=0;overflows<=0;complete<=0;
  end else begin
   if(s_valid&&s_sof) begin
    input_seq<=input_seq+1;
    if(state!=ARMED) dropped<=dropped+1;
   end
   if(pop) begin rdptr<=rdptr+1'b1;if(state!=RESULT)out_words<=out_words+1;end
   if(push) begin
    fifo[wrptr]<={s_sof,s_eol,s_data};wrptr<=wrptr+1'b1;
    if(state==ARMED) begin state<=CAPTURE;result_seq<=input_seq+1;end
    if(s_eol) begin x<=0;y<=y+1; if(y==FRAME_LINES-1)state<=DRAIN;end
    else x<=x+1;
   end
   case({push,pop}) 2'b10:count<=count+1'b1;2'b01:count<=count-1'b1;default:;endcase
   if(state==ARMED||state==CAPTURE||state==DRAIN) begin
    if(push||pop||close_pop)timer<=0;else timer<=timer+1;
    // AXIS video has no EOF: wait for the following SOF to prove there were
    // exactly FRAME_LINES lines, including no trailing oversized-height data.
    // PG020 S2MM TUSER mode requires the next valid SOF to retire the
    // previous transfer. A single lookahead beat closes count=1 VDMA; it
    // must not become DDR data (verified against the actual VDMA IP).
    if(state==DRAIN&&s_valid&&s_sof&&!frame_closed)begin frame_closed<=1;close_pending<=1;close_data<=s_data;end
    if(state==DRAIN&&s_valid&&!s_sof&&!frame_closed)fail_frame(16);
    if(state==DRAIN&&close_pop&&out_words==FRAME_WORDS) begin
     close_pending<=0;
     state<=RESULT;result_valid<=1;result_flags<=1;result_words<=FRAME_WORDS;result_lines<=FRAME_LINES;complete<=complete+1;
    end
    if(timer==TIMEOUT_CYCLES-1&&!push&&!pop&&!close_pop)fail_frame(128);
    if(selected) begin
     if(full)begin fail_frame(2);overflows<=overflows+1;end
     else if(unexpected)fail_frame(32);
     else if(short_line)fail_frame(4);
     else if(long_line)fail_frame(8);
    end
    if(source_error)fail_frame(256);
    if(dma_error)fail_frame(512);
    if(abort_capture)fail_frame(64);
   end
   if(arm&&state==IDLE&&!abort_capture) begin
    state<=ARMED;result_epoch<=arm_epoch;result_flags<=0;
    result_words<=0;result_lines<=0;out_words<=0;x<=0;y<=0;timer<=0;frame_closed<=0;close_pending<=0;
   end
   if(ack&&result_valid&&ack_epoch==result_epoch) begin
    state<=IDLE;result_valid<=0;result_flags<=0;count<=0;rdptr<=0;wrptr<=0;close_pending<=0;
   end
   if(clear_stats&&state==IDLE) begin input_seq<=0;dropped<=0;overflows<=0;complete<=0;end
  end
 end
endmodule
