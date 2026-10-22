`timescale 1ns/1ps
// AXI4-Stream RAW10 monitor: two 10-bit pixels in 16-bit slots per beat.
// All state and pulses use clk; resetn is synchronous active-low.
module capture_monitor (
    input wire clk,
    input wire resetn,
    input wire axis_tvalid,
    input wire axis_tready,
    input wire axis_tuser,
    input wire axis_tlast,
    input wire [31:0] axis_tdata,
    output reg [31:0] frame_count, line_count, total_word_count,
    output reg [31:0] last_frame_lines, last_line_pixels,
    output reg [31:0] frame_period_cycles, last_frame_words,
    output reg [9:0] data_min, data_max,
    output reg [31:0] data_xor, data_change_count,
    output reg active_video, hs, vs
);

    reg [31:0] cycle_count,
        last_sof_cycle,
        frame_words,
        frame_lines,
        line_pixels;
    reg [31:0] previous_data;
    reg seen_sof,
        seen_data;
    wire accepted = axis_tvalid & axis_tready;
    wire [9:0] pixel0 = axis_tdata[9:0];
    wire [9:0] pixel1 = axis_tdata[25:16];
    wire [9:0] beat_min = pixel0 < pixel1 ? pixel0 : pixel1;
    wire [9:0] beat_max = pixel0 > pixel1 ? pixel0 : pixel1;

    // 每个时序块仅驱动一个寄存器；所有状态使用非阻塞赋值。
    // 当前拍 SOF 锁存的仍是上一帧计数，不包含新帧的首拍。

    // -------------------------------------------------------------------------
    // 1. 有效拍及行/帧事件脉冲（每拍更新，不保持上一拍的脉冲）
    // -------------------------------------------------------------------------
    always @(posedge clk) begin
        if (!resetn)
            active_video <= 0;
        else
            active_video <= accepted;
    end

    always @(posedge clk) begin
        if (!resetn)
            hs <= 0;
        else
            hs <= accepted & axis_tlast;
    end

    always @(posedge clk) begin
        if (!resetn)
            vs <= 0;
        else
            vs <= accepted & axis_tuser;
    end

    // -------------------------------------------------------------------------
    // 2. 累计计数与帧间隔基准
    // -------------------------------------------------------------------------
    always @(posedge clk) begin
        if (!resetn)
            cycle_count <= 0;
        else
            cycle_count <= cycle_count + 1'b1;
    end

    always @(posedge clk) begin
        if (!resetn)
            total_word_count <= 0;
        else if (accepted)
            total_word_count <= total_word_count + 1'b1;
    end

    always @(posedge clk) begin
        if (!resetn)
            line_count <= 0;
        else if (accepted && axis_tlast)
            line_count <= line_count + 1'b1;
    end

    always @(posedge clk) begin
        if (!resetn)
            frame_count <= 0;
        else if (accepted && axis_tuser)
            frame_count <= frame_count + 1'b1;
    end

    always @(posedge clk) begin
        if (!resetn)
            seen_sof <= 0;
        else if (accepted && axis_tuser)
            seen_sof <= 1'b1;
    end

    always @(posedge clk) begin
        if (!resetn)
            last_sof_cycle <= 0;
        else if (accepted && axis_tuser)
            last_sof_cycle <= cycle_count;
    end

    always @(posedge clk) begin
        if (!resetn)
            frame_period_cycles <= 0;
        else if (accepted && axis_tuser && seen_sof)
            frame_period_cycles <= cycle_count - last_sof_cycle;
    end

    // -------------------------------------------------------------------------
    // 3. 当前帧计数及上一完整帧快照
    // -------------------------------------------------------------------------
    always @(posedge clk) begin
        if (!resetn)
            frame_words <= 0;
        else if (accepted) begin
            if (axis_tuser)
                frame_words <= 1;
            else if (seen_sof)
                frame_words <= frame_words + 1'b1;
        end
    end

    always @(posedge clk) begin
        if (!resetn)
            frame_lines <= 0;
        else if (accepted) begin
            if (axis_tuser)
                frame_lines <= axis_tlast ? 32'd1 : 32'd0;
            else if (seen_sof && axis_tlast)
                frame_lines <= frame_lines + 1'b1;
        end
    end

    always @(posedge clk) begin
        if (!resetn)
            last_frame_words <= 0;
        else if (accepted && axis_tuser && seen_sof)
            last_frame_words <= frame_words;
    end

    always @(posedge clk) begin
        if (!resetn)
            last_frame_lines <= 0;
        else if (accepted && axis_tuser && seen_sof)
            last_frame_lines <= frame_lines;
    end

    // -------------------------------------------------------------------------
    // 4. 行宽统计：SOF 丢弃未完成行；SOF 与 EOL 同拍时该行仅两个像素
    // -------------------------------------------------------------------------
    always @(posedge clk) begin
        if (!resetn)
            line_pixels <= 0;
        else if (accepted) begin
            if (axis_tuser)
                line_pixels <= axis_tlast ? 32'd0 : 32'd2;
            else if (axis_tlast)
                line_pixels <= 0;
            else
                line_pixels <= line_pixels + 32'd2;
        end
    end

    always @(posedge clk) begin
        if (!resetn)
            last_line_pixels <= 0;
        else if (accepted) begin
            if (axis_tuser) begin
                if (axis_tlast)
                    last_line_pixels <= 2;
            end else if (axis_tlast)
                last_line_pixels <= line_pixels + 32'd2;
        end
    end

    // -------------------------------------------------------------------------
    // 5. 像素统计：只接受 valid && ready 的两个 RAW10 像素
    // -------------------------------------------------------------------------
    always @(posedge clk) begin
        if (!resetn)
            data_min <= 10'h3ff;
        else if (accepted && beat_min < data_min)
            data_min <= beat_min;
    end

    always @(posedge clk) begin
        if (!resetn)
            data_max <= 0;
        else if (accepted && beat_max > data_max)
            data_max <= beat_max;
    end

    always @(posedge clk) begin
        if (!resetn)
            data_xor <= 0;
        else if (accepted)
            data_xor <= data_xor ^ axis_tdata;
    end

    always @(posedge clk) begin
        if (!resetn)
            previous_data <= 0;
        else if (accepted)
            previous_data <= axis_tdata;
    end

    always @(posedge clk) begin
        if (!resetn)
            seen_data <= 0;
        else if (accepted)
            seen_data <= 1'b1;
    end

    always @(posedge clk) begin
        if (!resetn)
            data_change_count <= 0;
        else if (accepted && seen_data && axis_tdata != previous_data)
            data_change_count <= data_change_count + 1'b1;
    end
endmodule
