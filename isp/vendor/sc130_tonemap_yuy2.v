// RAW10 mono x2 per clock => YUY2 x2 pixels packed as one 32-bit word.
// Input assumptions:
//   Each valid cycle contains pixel0/pixel1 from an EVEN horizontal coordinate.
//   1280 pixels/line: 640 valid cycles/line, EOL asserted on the last pair.
//   No backpressure; an AXI-stream adapter should handle downstream stalls.
//   LUT file is 1024 lines of 2-hex-digit Y values.
//
// Byte layout of m_yuy2[31:0], little-endian byte stream:
//   byte0=Y0, byte1=U=128, byte2=Y1, byte3=V=128.
//
// Latency: 1 clock; pixel throughput: 2 pixels per clock.
module sc130_tonemap_yuy2 #(
    parameter LUT_FILE = "sc130_tone_lut.mem"
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        s_valid,
    input  wire        s_sof,
    input  wire        s_eol,
    input  wire [9:0]  s_raw0,
    input  wire [9:0]  s_raw1,
    output reg         m_valid,
    output reg         m_sof,
    output reg         m_eol,
    output wire [31:0] m_yuy2
);
    (* rom_style = "block" *) reg [7:0] tone_rom [0:1023];
    initial $readmemh(LUT_FILE, tone_rom);

    reg [7:0] y0_reg;
    reg [7:0] y1_reg;

    // Synchronous read: synthesis can infer a dual-port BRAM/ROM.
    always @(posedge clk) begin
        if (s_valid) begin
            y0_reg <= tone_rom[s_raw0];
            y1_reg <= tone_rom[s_raw1];
        end
        if (!rst_n) begin
            m_valid <= 1'b0;
            m_sof   <= 1'b0;
            m_eol   <= 1'b0;
        end else begin
            m_valid <= s_valid;
            m_sof   <= s_valid && s_sof;
            m_eol   <= s_valid && s_eol;
        end
    end

    assign m_yuy2 = {8'h80, y1_reg, 8'h80, y0_reg};
endmodule
