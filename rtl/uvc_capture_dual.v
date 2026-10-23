`timescale 1ns/1ps
// Independent FIFOs/tokens/guards; source is always broadcast, never gated by READY.
module uvc_capture_dual #(
    parameter LINE_WORDS  = 640,
    parameter FRAME_LINES = 1024,
    parameter FIFO_DEPTH  = 4096
) (
    // Shared clock, reset and source stream
    input  wire        clk,
    input  wire        resetn,
    input  wire [19:0] raw,
    input  wire        valid,
    input  wire        sof,
    input  wire        eol,
    input  wire        packet_error,
    input  wire        global_fault,

    // Channel 0 stream and memory write monitor
    output wire [31:0] m0_data,
    output wire        m0_valid,
    output wire        m0_sof,
    output wire        m0_eol,
    input  wire        m0_ready,
    input  wire        d0_awvalid,
    input  wire        d0_awready,
    input  wire        d0_wvalid,
    input  wire        d0_wready,
    input  wire        d0_wlast,
    input  wire        d0_bvalid,
    input  wire        d0_bready,
    input  wire [15:0] d0_wstrb,
    input  wire [1:0]  d0_bresp,
    output wire        irq0,

    // Channel 0 AXI4-Lite control
    input  wire [7:0]  c0_s_axi_awaddr,
    input  wire        c0_s_axi_awvalid,
    output wire        c0_s_axi_awready,
    input  wire [31:0] c0_s_axi_wdata,
    input  wire [3:0]  c0_s_axi_wstrb,
    input  wire        c0_s_axi_wvalid,
    output wire        c0_s_axi_wready,
    output wire [1:0]  c0_s_axi_bresp,
    output wire        c0_s_axi_bvalid,
    input  wire        c0_s_axi_bready,
    input  wire [7:0]  c0_s_axi_araddr,
    input  wire        c0_s_axi_arvalid,
    output wire        c0_s_axi_arready,
    output wire [31:0] c0_s_axi_rdata,
    output wire [1:0]  c0_s_axi_rresp,
    output wire        c0_s_axi_rvalid,
    input  wire        c0_s_axi_rready,

    // Channel 1 stream and memory write monitor
    output wire [31:0] m1_data,
    output wire        m1_valid,
    output wire        m1_sof,
    output wire        m1_eol,
    input  wire        m1_ready,
    input  wire        d1_awvalid,
    input  wire        d1_awready,
    input  wire        d1_wvalid,
    input  wire        d1_wready,
    input  wire        d1_wlast,
    input  wire        d1_bvalid,
    input  wire        d1_bready,
    input  wire [15:0] d1_wstrb,
    input  wire [1:0]  d1_bresp,
    output wire        irq1,

    // Channel 1 AXI4-Lite control
    input  wire [7:0]  c1_s_axi_awaddr,
    input  wire        c1_s_axi_awvalid,
    output wire        c1_s_axi_awready,
    input  wire [31:0] c1_s_axi_wdata,
    input  wire [3:0]  c1_s_axi_wstrb,
    input  wire        c1_s_axi_wvalid,
    output wire        c1_s_axi_wready,
    output wire [1:0]  c1_s_axi_bresp,
    output wire        c1_s_axi_bvalid,
    input  wire        c1_s_axi_bready,
    input  wire [7:0]  c1_s_axi_araddr,
    input  wire        c1_s_axi_arvalid,
    output wire        c1_s_axi_arready,
    output wire [31:0] c1_s_axi_rdata,
    output wire [1:0]  c1_s_axi_rresp,
    output wire        c1_s_axi_rvalid,
    input  wire        c1_s_axi_rready
);

    // ---------------------------------------------------------------------
    // Shared stream and source sequence used by both capture channels
    // ---------------------------------------------------------------------
    wire [31:0] converted;
    wire        cv;
    wire        cs;
    wire        ce;
    wire [31:0] source_seq;
    wire [63:0] source_tick;

    // Shared pixel format conversion
    isp converter (
        .clk     (clk),
        .resetn  (resetn),
        .s_data  (raw),
        .s_valid (valid),
        .s_sof   (sof),
        .s_eol   (eol),
        .s_ready (),
        .m_data  (converted),
        .m_valid (cv),
        .m_sof   (cs),
        .m_eol   (ce),
        .m_ready (1'b1)
    );

    // Match source error flags to the converter's one-cycle pipeline.
    reg packet_d;
    reg fault_d;

    always @(posedge clk) begin
        if (!resetn) begin
            packet_d <= 0;
            fault_d  <= 0;
        end else begin
            packet_d <= packet_error;
            fault_d  <= global_fault;
        end
    end

    // One source sequence and timestamp for both channels
    capture_source_clock_v2 source_clock (
        .clk         (clk),
        .resetn      (resetn),
        .valid       (cv),
        .sof         (cs),
        .source_seq  (source_seq),
        .source_tick (source_tick)
    );

    // ---------------------------------------------------------------------
    // Channel 0: even source frames
    // Signals shared by this channel's guard, frame gate and AXI registers.
    // ---------------------------------------------------------------------
    // Commands and epoch handshakes
    wire        c0_arm;
    wire        c0_abort_capture;
    wire        c0_ack;
    wire        c0_clear_stats;
    wire [31:0] c0_arm_epoch;
    wire [31:0] c0_ack_epoch;

    // Capture result and counters
    wire [31:0] c0_status;
    wire        c0_result_valid;
    wire [31:0] c0_result_epoch;
    wire [31:0] c0_result_flags;
    wire [31:0] c0_input_seq;
    wire [31:0] c0_result_seq;
    wire [31:0] c0_result_words;
    wire [31:0] c0_result_lines;
    wire [31:0] c0_dropped;
    wire [31:0] c0_overflows;
    wire [31:0] c0_complete;

    // Source timing and arm diagnostics
    wire [31:0] c0_deadline_miss;
    wire [31:0] c0_arm_accept_epoch;
    wire [31:0] c0_arm_target_seq;
    wire [63:0] c0_arm_tick;
    wire [63:0] c0_result_sof_tick;
    wire [63:0] c0_result_done_tick;

    // Memory write completion
    wire        c0_epoch_start;
    wire        c0_dma_done;
    wire        c0_dma_error;
    wire        c0_quiescent;
    wire [31:0] c0_guard_bytes;

    // Observe VDMA writes at the DDR-facing AXI boundary
    vdma_write_guard_v2 #(
        .EXPECTED_BYTES (LINE_WORDS*FRAME_LINES*4)
    ) guard0 (
        .clk       (clk),
        .resetn    (resetn),
        .start     (c0_epoch_start),
        .awvalid   (d0_awvalid),
        .awready   (d0_awready),
        .wvalid    (d0_wvalid),
        .wready    (d0_wready),
        .wlast     (d0_wlast),
        .wstrb     (d0_wstrb),
        .bvalid    (d0_bvalid),
        .bready    (d0_bready),
        .bresp     (d0_bresp),
        .retired   (c0_dma_done),
        .error     (c0_dma_error),
        .quiescent (c0_quiescent),
        .bytes     (c0_guard_bytes)
    );

    // Select this phase and validate a complete frame
    capture_phase_gate_v2 #(
        .PHASE       (0),
        .LINE_WORDS  (LINE_WORDS),
        .FRAME_LINES (FRAME_LINES),
        .FIFO_DEPTH  (FIFO_DEPTH)
    ) gate0 (
        .clk              (clk),
        .resetn           (resetn),
        .arm              (c0_arm),
        .abort_capture    (c0_abort_capture),
        .ack              (c0_ack),
        .clear_stats      (c0_clear_stats),
        .arm_epoch        (c0_arm_epoch),
        .ack_epoch        (c0_ack_epoch),
        .source_seq       (source_seq),
        .source_tick      (source_tick),
        .s_data           (converted),
        .s_valid          (cv),
        .s_sof            (cs),
        .s_eol            (ce),
        .packet_error     (packet_d),
        .global_fault     (fault_d),
        .dma_done         (c0_dma_done),
        .dma_error        (c0_dma_error),
        .m_data           (m0_data),
        .m_valid          (m0_valid),
        .m_sof            (m0_sof),
        .m_eol            (m0_eol),
        .m_ready          (m0_ready),
        .status           (c0_status),
        .result_valid     (c0_result_valid),
        .result_epoch     (c0_result_epoch),
        .result_flags     (c0_result_flags),
        .input_seq        (c0_input_seq),
        .result_seq       (c0_result_seq),
        .result_words     (c0_result_words),
        .result_lines     (c0_result_lines),
        .dropped          (c0_dropped),
        .overflows        (c0_overflows),
        .complete         (c0_complete),
        .deadline_miss    (c0_deadline_miss),
        .arm_accept_epoch (c0_arm_accept_epoch),
        .arm_target_seq   (c0_arm_target_seq),
        .arm_tick         (c0_arm_tick),
        .result_sof_tick  (c0_result_sof_tick),
        .result_done_tick (c0_result_done_tick),
        .epoch_start      (c0_epoch_start)
    );

    // Expose commands, results and diagnostics to PS
    capture_axi_regs_v2 #(
        .CHANNEL_ID (0),
        .PHASE      (0)
    ) regs0 (
        .clk              (clk),
        .resetn           (resetn),
        .s_axi_awaddr     (c0_s_axi_awaddr),
        .s_axi_awvalid    (c0_s_axi_awvalid),
        .s_axi_awready    (c0_s_axi_awready),
        .s_axi_wdata      (c0_s_axi_wdata),
        .s_axi_wstrb      (c0_s_axi_wstrb),
        .s_axi_wvalid     (c0_s_axi_wvalid),
        .s_axi_wready     (c0_s_axi_wready),
        .s_axi_bresp      (c0_s_axi_bresp),
        .s_axi_bvalid     (c0_s_axi_bvalid),
        .s_axi_bready     (c0_s_axi_bready),
        .s_axi_araddr     (c0_s_axi_araddr),
        .s_axi_arvalid    (c0_s_axi_arvalid),
        .s_axi_arready    (c0_s_axi_arready),
        .s_axi_rdata      (c0_s_axi_rdata),
        .s_axi_rresp      (c0_s_axi_rresp),
        .s_axi_rvalid     (c0_s_axi_rvalid),
        .s_axi_rready     (c0_s_axi_rready),
        .arm              (c0_arm),
        .abort_capture    (c0_abort_capture),
        .ack              (c0_ack),
        .clear_stats      (c0_clear_stats),
        .arm_epoch        (c0_arm_epoch),
        .ack_epoch        (c0_ack_epoch),
        .status           (c0_status),
        .result_epoch     (c0_result_epoch),
        .result_flags     (c0_result_flags),
        .input_seq        (c0_input_seq),
        .result_seq       (c0_result_seq),
        .result_words     (c0_result_words),
        .result_lines     (c0_result_lines),
        .dropped          (c0_dropped),
        .overflows        (c0_overflows),
        .complete         (c0_complete),
        .deadline_miss    (c0_deadline_miss),
        .arm_accept_epoch (c0_arm_accept_epoch),
        .arm_target_seq   (c0_arm_target_seq),
        .guard_bytes      (c0_guard_bytes),
        .arm_tick         (c0_arm_tick),
        .result_sof_tick  (c0_result_sof_tick),
        .result_done_tick (c0_result_done_tick),
        .result_valid     (c0_result_valid),
        .source_tick      (source_tick),
        .guard_status     ({29'b0,c0_quiescent,c0_dma_error,c0_dma_done}),
        .irq              (irq0)
    );

    // ---------------------------------------------------------------------
    // Channel 1: odd source frames
    // Signals shared by this channel's guard, frame gate and AXI registers.
    // ---------------------------------------------------------------------
    // Commands and epoch handshakes
    wire        c1_arm;
    wire        c1_abort_capture;
    wire        c1_ack;
    wire        c1_clear_stats;
    wire [31:0] c1_arm_epoch;
    wire [31:0] c1_ack_epoch;

    // Capture result and counters
    wire [31:0] c1_status;
    wire        c1_result_valid;
    wire [31:0] c1_result_epoch;
    wire [31:0] c1_result_flags;
    wire [31:0] c1_input_seq;
    wire [31:0] c1_result_seq;
    wire [31:0] c1_result_words;
    wire [31:0] c1_result_lines;
    wire [31:0] c1_dropped;
    wire [31:0] c1_overflows;
    wire [31:0] c1_complete;

    // Source timing and arm diagnostics
    wire [31:0] c1_deadline_miss;
    wire [31:0] c1_arm_accept_epoch;
    wire [31:0] c1_arm_target_seq;
    wire [63:0] c1_arm_tick;
    wire [63:0] c1_result_sof_tick;
    wire [63:0] c1_result_done_tick;

    // Memory write completion
    wire        c1_epoch_start;
    wire        c1_dma_done;
    wire        c1_dma_error;
    wire        c1_quiescent;
    wire [31:0] c1_guard_bytes;

    // Observe VDMA writes at the DDR-facing AXI boundary
    vdma_write_guard_v2 #(
        .EXPECTED_BYTES (LINE_WORDS*FRAME_LINES*4)
    ) guard1 (
        .clk       (clk),
        .resetn    (resetn),
        .start     (c1_epoch_start),
        .awvalid   (d1_awvalid),
        .awready   (d1_awready),
        .wvalid    (d1_wvalid),
        .wready    (d1_wready),
        .wlast     (d1_wlast),
        .wstrb     (d1_wstrb),
        .bvalid    (d1_bvalid),
        .bready    (d1_bready),
        .bresp     (d1_bresp),
        .retired   (c1_dma_done),
        .error     (c1_dma_error),
        .quiescent (c1_quiescent),
        .bytes     (c1_guard_bytes)
    );

    // Select this phase and validate a complete frame
    capture_phase_gate_v2 #(
        .PHASE       (1),
        .LINE_WORDS  (LINE_WORDS),
        .FRAME_LINES (FRAME_LINES),
        .FIFO_DEPTH  (FIFO_DEPTH)
    ) gate1 (
        .clk              (clk),
        .resetn           (resetn),
        .arm              (c1_arm),
        .abort_capture    (c1_abort_capture),
        .ack              (c1_ack),
        .clear_stats      (c1_clear_stats),
        .arm_epoch        (c1_arm_epoch),
        .ack_epoch        (c1_ack_epoch),
        .source_seq       (source_seq),
        .source_tick      (source_tick),
        .s_data           (converted),
        .s_valid          (cv),
        .s_sof            (cs),
        .s_eol            (ce),
        .packet_error     (packet_d),
        .global_fault     (fault_d),
        .dma_done         (c1_dma_done),
        .dma_error        (c1_dma_error),
        .m_data           (m1_data),
        .m_valid          (m1_valid),
        .m_sof            (m1_sof),
        .m_eol            (m1_eol),
        .m_ready          (m1_ready),
        .status           (c1_status),
        .result_valid     (c1_result_valid),
        .result_epoch     (c1_result_epoch),
        .result_flags     (c1_result_flags),
        .input_seq        (c1_input_seq),
        .result_seq       (c1_result_seq),
        .result_words     (c1_result_words),
        .result_lines     (c1_result_lines),
        .dropped          (c1_dropped),
        .overflows        (c1_overflows),
        .complete         (c1_complete),
        .deadline_miss    (c1_deadline_miss),
        .arm_accept_epoch (c1_arm_accept_epoch),
        .arm_target_seq   (c1_arm_target_seq),
        .arm_tick         (c1_arm_tick),
        .result_sof_tick  (c1_result_sof_tick),
        .result_done_tick (c1_result_done_tick),
        .epoch_start      (c1_epoch_start)
    );

    // Expose commands, results and diagnostics to PS
    capture_axi_regs_v2 #(
        .CHANNEL_ID (1),
        .PHASE      (1)
    ) regs1 (
        .clk              (clk),
        .resetn           (resetn),
        .s_axi_awaddr     (c1_s_axi_awaddr),
        .s_axi_awvalid    (c1_s_axi_awvalid),
        .s_axi_awready    (c1_s_axi_awready),
        .s_axi_wdata      (c1_s_axi_wdata),
        .s_axi_wstrb      (c1_s_axi_wstrb),
        .s_axi_wvalid     (c1_s_axi_wvalid),
        .s_axi_wready     (c1_s_axi_wready),
        .s_axi_bresp      (c1_s_axi_bresp),
        .s_axi_bvalid     (c1_s_axi_bvalid),
        .s_axi_bready     (c1_s_axi_bready),
        .s_axi_araddr     (c1_s_axi_araddr),
        .s_axi_arvalid    (c1_s_axi_arvalid),
        .s_axi_arready    (c1_s_axi_arready),
        .s_axi_rdata      (c1_s_axi_rdata),
        .s_axi_rresp      (c1_s_axi_rresp),
        .s_axi_rvalid     (c1_s_axi_rvalid),
        .s_axi_rready     (c1_s_axi_rready),
        .arm              (c1_arm),
        .abort_capture    (c1_abort_capture),
        .ack              (c1_ack),
        .clear_stats      (c1_clear_stats),
        .arm_epoch        (c1_arm_epoch),
        .ack_epoch        (c1_ack_epoch),
        .status           (c1_status),
        .result_epoch     (c1_result_epoch),
        .result_flags     (c1_result_flags),
        .input_seq        (c1_input_seq),
        .result_seq       (c1_result_seq),
        .result_words     (c1_result_words),
        .result_lines     (c1_result_lines),
        .dropped          (c1_dropped),
        .overflows        (c1_overflows),
        .complete         (c1_complete),
        .deadline_miss    (c1_deadline_miss),
        .arm_accept_epoch (c1_arm_accept_epoch),
        .arm_target_seq   (c1_arm_target_seq),
        .guard_bytes      (c1_guard_bytes),
        .arm_tick         (c1_arm_tick),
        .result_sof_tick  (c1_result_sof_tick),
        .result_done_tick (c1_result_done_tick),
        .result_valid     (c1_result_valid),
        .source_tick      (source_tick),
        .guard_status     ({29'b0,c1_quiescent,c1_dma_error,c1_dma_done}),
        .irq              (irq1)
    );

endmodule
