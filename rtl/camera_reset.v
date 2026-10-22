`timescale 1ns/1ps
// PG232 requires video reset held for at least 40 reference clock cycles.
// Hold each local domain for 256 cycles after a synchronized clock lock.
module camera_reset (
    input wire clk, locked,
    output reg resetn = 0
);

    (* ASYNC_REG = "TRUE" *) reg [2:0] lock_sync = 0;
    reg [7:0] count = 0;

    always @(posedge clk or negedge locked) begin
        if (!locked) begin
            lock_sync <= 0;
            count <= 0;
            resetn <= 0;
        end else begin
            lock_sync <= {lock_sync[1:0], 1'b1};
            if (!lock_sync[2]) begin
                count <= 0;
                resetn <= 0;
            end else if (&count) resetn <= 1;
            else count <= count + 1'b1;
        end
    end
endmodule

// Software requests are independent of I2C/clock generation. Each local reset
// asserts asynchronously and releases only after its own clock has settled.
module camera_pipeline_reset (
    input wire video_clk, lite_clk, locked, ps_resetn,
    input wire lite_enable, video_enable, capture_enable,
    output wire lite_resetn, video_resetn, capture_resetn
);
    camera_reset rx_lite (
        .clk(lite_clk), .locked(locked & ps_resetn & lite_enable),
        .resetn(lite_resetn));
    camera_reset rx_video (
        .clk(video_clk),
        .locked(locked & ps_resetn & lite_resetn & video_enable),
        .resetn(video_resetn));
    camera_reset capture (
        .clk(video_clk), .locked(locked & ps_resetn & capture_enable),
        .resetn(capture_resetn));
endmodule
