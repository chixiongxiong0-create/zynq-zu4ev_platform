`timescale 1ns/1ps
// Phase selector around the verified single-frame gate. Only ARMED ingress is
// filtered; the current frame and its closing SOF remain fully observable.
module capture_phase_gate_v2 #(
 parameter PHASE=0,LINE_WORDS=640,FRAME_LINES=1024,FIFO_DEPTH=4096,TIMEOUT_CYCLES=20000000
)(input wire clk,resetn,arm,abort_capture,ack,clear_stats,
 input wire[31:0]arm_epoch,ack_epoch,source_seq,input wire[63:0]source_tick,
 input wire[31:0]s_data,input wire s_valid,s_sof,s_eol,packet_error,global_fault,dma_done,dma_error,
 output wire[31:0]m_data,output wire m_valid,m_sof,m_eol,input wire m_ready,
 output wire[31:0]status,output wire result_valid,output wire[31:0]result_epoch,result_flags,input_seq,
 output reg[31:0]result_seq,output wire[31:0]result_words,result_lines,dropped,overflows,output reg[31:0]complete,
 output reg[31:0]deadline_miss,arm_accept_epoch,arm_target_seq,
 output reg[63:0]arm_tick,result_sof_tick,result_done_tick,output wire epoch_start);
 wire[31:0]core_status;wire core_result_valid;
 wire[31:0]next_seq=source_seq+1;
 wire match_phase=(next_seq[0]==PHASE);
 wire source_sof=s_valid&&s_sof;
 wire accept_sof=core_status[1]&&source_sof&&match_phase&&!abort_capture;
 wire filtered_valid=s_valid&&(!core_status[1]||!s_sof||match_phase);
 reg result_seen,in_frame;
 assign epoch_start=arm&&core_status[0]&&!abort_capture;
 assign result_valid=core_result_valid&&result_seen;
 assign status={core_status[31:4],result_valid,core_status[2:0]};
 assign input_seq=source_seq;
 function[31:0]following_phase;
  input[31:0]after_seq;reg[31:0]candidate;
  begin candidate=after_seq+1;following_phase=candidate[0]==PHASE?candidate:candidate+1;end
 endfunction
 capture_frame_gate #(.LINE_WORDS(LINE_WORDS),.FRAME_LINES(FRAME_LINES),.FIFO_DEPTH(FIFO_DEPTH),.TIMEOUT_CYCLES(TIMEOUT_CYCLES)) core(
 .clk(clk),.resetn(resetn),.arm(arm),.abort_capture(abort_capture),.ack(ack),.clear_stats(clear_stats),.arm_epoch(arm_epoch),.ack_epoch(ack_epoch),
 .s_data(s_data),.s_valid(filtered_valid),.s_sof(s_sof),.s_eol(s_eol),
 .source_error(global_fault||(packet_error&&((in_frame&&!s_sof)||accept_sof))),.dma_done(dma_done),.dma_error(dma_error),
 .m_data(m_data),.m_valid(m_valid),.m_sof(m_sof),.m_eol(m_eol),.m_ready(m_ready),.status(core_status),.result_valid(core_result_valid),.result_epoch(result_epoch),.result_flags(result_flags),
 .input_seq(),.result_seq(),.result_words(result_words),.result_lines(result_lines),.dropped(dropped),.overflows(overflows),.complete());
 always@(posedge clk)begin
  if(!resetn)begin
   result_seen<=0;in_frame<=0;result_seq<=32'hffffffff;deadline_miss<=0;arm_accept_epoch<=0;arm_target_seq<=0;
   arm_tick<=0;result_sof_tick<=0;result_done_tick<=0;complete<=0;
  end else begin
   if(!core_result_valid)result_seen<=0;
   if(core_result_valid&&!result_seen)begin
    result_seen<=1;result_done_tick<=source_tick;in_frame<=0;
    if(result_flags==1)complete<=complete+1;
   end
   if(source_sof&&in_frame)in_frame<=0;
   if(source_sof&&match_phase&&(!core_status[1]||abort_capture))deadline_miss<=deadline_miss+1;
   if(epoch_start)begin
    arm_accept_epoch<=arm_epoch;arm_tick<=source_tick;result_seq<=32'hffffffff;result_sof_tick<=0;result_done_tick<=0;result_seen<=0;in_frame<=0;
    arm_target_seq<=following_phase(source_sof?next_seq:source_seq);
   end
   if(accept_sof)begin
    result_seq<=next_seq;arm_target_seq<=next_seq;result_sof_tick<=source_tick;in_frame<=1;
   end
   if(clear_stats&&core_status[0])begin deadline_miss<=0;complete<=0;end
  end
 end
endmodule
