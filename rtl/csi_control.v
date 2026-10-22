`timescale 1ns/1ps
// Standalone AXI4-Lite initialization and passive diagnostics for PG232.
// Reset this master together with the slave. No automatic retry on a bus fault.
module csi_control #(
    parameter integer POLL_INTERVAL = 1000000,
    parameter integer TIMEOUT_CYCLES = 100000
) (
    input wire clk,
    input wire resetn,
    output wire [11:0] m_axi_awaddr,
    output reg m_axi_awvalid,
    input wire m_axi_awready,
    output wire [31:0] m_axi_wdata,
    output wire [3:0] m_axi_wstrb,
    output reg m_axi_wvalid,
    input wire m_axi_wready,
    input wire [1:0] m_axi_bresp,
    input wire m_axi_bvalid,
    output reg m_axi_bready,
    output reg [11:0] m_axi_araddr,
    output reg m_axi_arvalid,
    input wire m_axi_arready,
    input wire [31:0] m_axi_rdata,
    input wire [1:0] m_axi_rresp,
    input wire m_axi_rvalid,
    output reg m_axi_rready,
    output reg init_done,
    output reg bus_error,
    output reg [31:0] config_reg,
    output reg [31:0] status_reg,
    output reg [31:0] interrupt_status,
    output reg [31:0] interrupt_enable,
    output reg [31:0] lane0_info,
    output reg [31:0] lane1_info
);

    // START -> WRITE -> RESPONSE: one core-enable write.
    // DELAY -> ADDRESS -> READ: round-robin diagnostics; faults drain then HALT.
    localparam START = 0,
        WRITE = 1,
        RESPONSE = 2,
        DELAY = 3,
        ADDRESS = 4,
        READ = 5,
        HALT = 6;
    reg [2:0] state;
    reg [2:0] index;
    reg [31:0] timer;
    assign m_axi_awaddr = 12'h000;
    assign m_axi_wdata = 32'h00000001;
    assign m_axi_wstrb = 4'hf;

    function [11:0] read_address;
        input [2:0] slot;
        begin
            case (slot)
                0 : read_address = 12'h000; // Core configuration
                1 : read_address = 12'h010; // Core status / packet count
                2 : read_address = 12'h024; // Sticky interrupt status (read only here)
                3 : read_address = 12'h028; // Interrupt enable
                4 : read_address = 12'h040; // Lane 0 status
                default : read_address = 12'h044; // Lane 1 status
            endcase
        end
    endfunction

    always @(posedge clk) begin
        if (!resetn) begin
            state <= START;
            index <= 0;
            timer <= 0;
            init_done <= 0;
            bus_error <= 0;
            m_axi_awvalid <= 0;
            m_axi_wvalid <= 0;
            m_axi_bready <= 0;
            m_axi_arvalid <= 0;
            m_axi_araddr <= 0;
            m_axi_rready <= 0;
            config_reg <= 0;
            status_reg <= 0;
            interrupt_status <= 0;
            interrupt_enable <= 0;
            lane0_info <= 0;
            lane1_info <= 0;
        end else begin
            // A timeout latches the fault but never withdraws an outstanding VALID.
            // Finish/drain that transaction if the slave recovers, then halt.
            if (state == WRITE || state == RESPONSE || state == ADDRESS || state == READ) begin
                if (timer >= TIMEOUT_CYCLES-1) bus_error <= 1;
                else timer <= timer + 1;
            end
            case (state)
                START : begin
                    m_axi_awvalid <= 1;
                    m_axi_wvalid <= 1;
                    timer <= 0;
                    state <= WRITE;
                end
                WRITE : begin
                    if (m_axi_awvalid && m_axi_awready) m_axi_awvalid <= 0;
                    if (m_axi_wvalid && m_axi_wready) m_axi_wvalid <= 0;
                    if ((!m_axi_awvalid || m_axi_awready) && (!m_axi_wvalid || m_axi_wready)) begin
                        m_axi_bready <= 1;
                        timer <= 0;
                        state <= RESPONSE;
                    end
                end
                RESPONSE : if (m_axi_bvalid && m_axi_bready) begin
                    m_axi_bready <= 0;
                    timer <= 0;
                    if (m_axi_bresp != 0 || bus_error || timer >= TIMEOUT_CYCLES-1) begin
                        bus_error <= 1;
                        state <= HALT;
                    end else begin
                        init_done <= 1;
                        state <= DELAY;
                    end
                end
                DELAY : begin
                    if (bus_error) state <= HALT;
                    else if (timer >= POLL_INTERVAL-1) begin
                        timer <= 0;
                        m_axi_araddr <= read_address(index);
                        m_axi_arvalid <= 1;
                        state <= ADDRESS;
                    end else timer <= timer + 1;
                end
                ADDRESS : if (m_axi_arvalid && m_axi_arready) begin
                    m_axi_arvalid <= 0;
                    m_axi_rready <= 1;
                    timer <= 0;
                    state <= READ;
                end
                READ : if (m_axi_rvalid && m_axi_rready) begin
                    m_axi_rready <= 0;
                    timer <= 0;
                    if (m_axi_rresp != 0 || bus_error || timer >= TIMEOUT_CYCLES-1) begin
                        bus_error <= 1;
                        state <= HALT;
                    end else begin
                        case (index)
                            0 : config_reg <= m_axi_rdata;
                            1 : status_reg <= m_axi_rdata;
                            2 : interrupt_status <= m_axi_rdata;
                            3 : interrupt_enable <= m_axi_rdata;
                            4 : lane0_info <= m_axi_rdata;
                            5 : lane1_info <= m_axi_rdata;
                        endcase
                        index <= index == 5 ? 0 : index + 1;
                        state <= DELAY;
                    end
                end
                HALT : state <= HALT;
                default : begin
                    bus_error <= 1;
                    state <= HALT;
                end
            endcase
        end
    end
endmodule
