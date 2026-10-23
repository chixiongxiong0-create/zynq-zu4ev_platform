`timescale 1ns/1ps
// ISP pipeline entry point: two RAW10 pixels -> two YUY2 pixels per beat.
// Keep algorithm stages inside this hierarchy; capture logic only uses isp.
// Current stage includes fused black correction, tone lookup and YUY2 packing.
module isp #(
    parameter BYPASS = 0
) (
    input  wire        clk,
    input  wire        resetn,
    input  wire [19:0] s_data,
    input  wire        s_valid,
    input  wire        s_sof,
    input  wire        s_eol,
    output wire        s_ready,
    output wire [31:0] m_data,
    output wire        m_valid,
    output wire        m_sof,
    output wire        m_eol,
    input  wire        m_ready
);
    // Add future RAW-domain stages before this converter, and YUV-domain
    // stages after it. Propagate READY and delay SOF/EOL with their pixels.
    // Adding registers changes latency: update capture fault alignment too.
    raw10_to_yuy2 #(.BYPASS(BYPASS)) raw10_2_yuv_i (
        .clk(clk),
        .resetn(resetn),
        .s_data(s_data),
        .s_valid(s_valid),
        .s_sof(s_sof),
        .s_eol(s_eol),
        .s_ready(s_ready),
        .m_data(m_data),
        .m_valid(m_valid),
        .m_sof(m_sof),
        .m_eol(m_eol),
        .m_ready(m_ready)
    );
endmodule
