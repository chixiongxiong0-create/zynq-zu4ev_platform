`timescale 1ns/1ps
module sc130_camera_top (
    input wire pl_clk_p, pl_clk_n,
    input wire mipi_clk_p, mipi_clk_n,
    input wire [1:0] mipi_data_p, mipi_data_n,
    inout wire camera_scl, camera_sda
);

    // -------------------------------------------------------------------------
    // 1. PS 与开漏 I2C 桥接（gi/go/gt 分别为输入、输出和三态控制）
    // -------------------------------------------------------------------------
    wire [94:0] gi,
        go,
        gt;
`ifdef SC130_UVC
    wire [31:0] cap_awaddr,cap_araddr,cap_wdata,cap_rdata,yuy2_data;
    wire [3:0] cap_wstrb;
    wire [1:0] cap_bresp,cap_rresp;
    wire cap_awvalid,cap_awready,cap_wvalid,cap_wready,cap_bvalid,cap_bready;
    wire cap_arvalid,cap_arready,cap_rvalid,cap_rready;
    wire yuy2_valid,yuy2_ready,yuy2_sof,yuy2_eol,cap_irq,ps_resetn,uvc_resetn;
    wire dma_awvalid,dma_awready,dma_wvalid,dma_wready,dma_wlast,dma_bvalid,dma_bready;
    wire [15:0] dma_wstrb;wire [1:0] dma_bresp;
    wire dma_done,dma_error,epoch_start;wire [31:0] dma_bytes;
`endif
    ps_system ps_i (
`ifdef SC130_UVC
        .video_clk_in(video_clk),.video_resetn_in(uvc_resetn),.ps_resetn_out(ps_resetn),.capture_irq(cap_irq),
        .dma_awvalid(dma_awvalid),.dma_awready(dma_awready),.dma_wvalid(dma_wvalid),.dma_wready(dma_wready),.dma_wlast(dma_wlast),.dma_wstrb(dma_wstrb),.dma_bvalid(dma_bvalid),.dma_bready(dma_bready),.dma_bresp(dma_bresp),
        .CAPTURE_AXIS_tdata(yuy2_data),.CAPTURE_AXIS_tkeep(4'hf),.CAPTURE_AXIS_tvalid(yuy2_valid),.CAPTURE_AXIS_tready(yuy2_ready),.CAPTURE_AXIS_tuser(yuy2_sof),.CAPTURE_AXIS_tlast(yuy2_eol),
        .CAPTURE_AXI_awaddr(cap_awaddr),.CAPTURE_AXI_awvalid(cap_awvalid),.CAPTURE_AXI_awready(cap_awready),
        .CAPTURE_AXI_wdata(cap_wdata),.CAPTURE_AXI_wstrb(cap_wstrb),.CAPTURE_AXI_wvalid(cap_wvalid),.CAPTURE_AXI_wready(cap_wready),
        .CAPTURE_AXI_bresp(cap_bresp),.CAPTURE_AXI_bvalid(cap_bvalid),.CAPTURE_AXI_bready(cap_bready),
        .CAPTURE_AXI_araddr(cap_araddr),.CAPTURE_AXI_arvalid(cap_arvalid),.CAPTURE_AXI_arready(cap_arready),
        .CAPTURE_AXI_rdata(cap_rdata),.CAPTURE_AXI_rresp(cap_rresp),.CAPTURE_AXI_rvalid(cap_rvalid),.CAPTURE_AXI_rready(cap_rready),
`endif
        .GPIO_0_0_tri_i (gi),
        .GPIO_0_0_tri_o (go),
        .GPIO_0_0_tri_t (gt)
    );
    assign camera_scl = (go[0] | gt[0]) ? 1'bz : 1'b0;
    assign camera_sda = (go[1] | gt[1]) ? 1'bz : 1'b0;

    // -------------------------------------------------------------------------
    // 2. 时钟与复位：视频/D-PHY 200 MHz，AXI-Lite 100 MHz
    // -------------------------------------------------------------------------
    wire video_clk,
        lite_clk,
        locked;
    camera_clocks clocks_i (
        .clk_in1_p (pl_clk_p),
        .clk_in1_n (pl_clk_n),
        .reset     (1'b0),
        .locked    (locked),
        .clk_out1  (video_clk),
        .clk_out2  (lite_clk)
    );
    wire video_resetn,
        lite_resetn;
    camera_reset video_reset_i (
        .clk    (video_clk),
        .locked (locked),
        .resetn (video_resetn)
    );
    camera_reset lite_reset_i (
        .clk    (lite_clk),
        .locked (locked),
        .resetn (lite_resetn)
    );

    // -------------------------------------------------------------------------
    // 3. AXI-Lite 初始化与 CSI 状态轮询
    // -------------------------------------------------------------------------
    wire [11:0] awaddr,
        araddr;
    wire awvalid,
        awready,
        wvalid,
        wready,
        bvalid,
        bready,
        arvalid,
        arready,
        rvalid,
        rready;
    wire [31:0] wdata,
        rdata;
    wire [3:0] wstrb;
    wire [1:0] bresp,
        rresp;
    wire init_done,
        bus_error;
    wire [31:0] config_reg,
        status_reg,
        interrupt_status,
        interrupt_enable,
        lane0_info,
        lane1_info;
    csi_control control_i (
        .clk              (lite_clk),
        .resetn           (lite_resetn),
        .m_axi_awaddr     (awaddr),
        .m_axi_awvalid    (awvalid),
        .m_axi_awready    (awready),
        .m_axi_wdata      (wdata),
        .m_axi_wstrb      (wstrb),
        .m_axi_wvalid     (wvalid),
        .m_axi_wready     (wready),
        .m_axi_bresp      (bresp),
        .m_axi_bvalid     (bvalid),
        .m_axi_bready     (bready),
        .m_axi_araddr     (araddr),
        .m_axi_arvalid    (arvalid),
        .m_axi_arready    (arready),
        .m_axi_rdata      (rdata),
        .m_axi_rresp      (rresp),
        .m_axi_rvalid     (rvalid),
        .m_axi_rready     (rready),
        .init_done        (init_done),
        .bus_error        (bus_error),
        .config_reg       (config_reg),
        .status_reg       (status_reg),
        .interrupt_status (interrupt_status),
        .interrupt_enable (interrupt_enable),
        .lane0_info       (lane0_info),
        .lane1_info       (lane1_info)
    );

    // -------------------------------------------------------------------------
    // 4. 双 lane MIPI CSI-2 接收器
    // -------------------------------------------------------------------------
    wire [23:0] csi_data;
`ifdef SC130_UVC
    wire [95:0] csi_user;
    assign sof=csi_user[0];
`endif
    wire valid,
        eol,
        sof,
        phy_locked,
        frame_pulse,
        irq,
        phy_reset,
        byteclk,
        phyclk;
`ifdef SC130_UVC
    csi2_rx_uvc receiver_i (
`else
    csi2_rx receiver_i (
`endif
        .lite_aclk             (lite_clk),
        .lite_aresetn          (lite_resetn),
        .dphy_clk_200M         (video_clk),
        .video_aclk            (video_clk),
        .video_aresetn         (video_resetn),
        .rxbyteclkhs           (byteclk),
        .clkoutphy_out         (phyclk),
        .pll_lock_out          (phy_locked),
        .system_rst_out        (phy_reset),
        .csirxss_csi_irq       (irq),
        .frame_rcvd_pulse_out  (frame_pulse),
        .csirxss_s_axi_awaddr  (awaddr[7:0]),
        .csirxss_s_axi_awvalid (awvalid),
        .csirxss_s_axi_awready (awready),
        .csirxss_s_axi_wdata   (wdata),
        .csirxss_s_axi_wstrb   (wstrb),
        .csirxss_s_axi_wvalid  (wvalid),
        .csirxss_s_axi_wready  (wready),
        .csirxss_s_axi_bresp   (bresp),
        .csirxss_s_axi_bvalid  (bvalid),
        .csirxss_s_axi_bready  (bready),
        .csirxss_s_axi_araddr  (araddr[7:0]),
        .csirxss_s_axi_arvalid (arvalid),
        .csirxss_s_axi_arready (arready),
        .csirxss_s_axi_rdata   (rdata),
        .csirxss_s_axi_rresp   (rresp),
        .csirxss_s_axi_rvalid  (rvalid),
        .csirxss_s_axi_rready  (rready),
        .video_out_tdata       (csi_data),
        .video_out_tvalid      (valid),
        .video_out_tready      (1'b1),
`ifdef SC130_UVC
        .video_out_tuser       (csi_user),
`else
        .video_out_tuser       (sof),
`endif
        .video_out_tlast       (eol),
        .video_out_tdest       (),
        .mipi_phy_if_clk_p     (mipi_clk_p),
        .mipi_phy_if_clk_n     (mipi_clk_n),
        .mipi_phy_if_data_p    (mipi_data_p),
        .mipi_phy_if_data_n    (mipi_data_n)
    );
    // -------------------------------------------------------------------------
    // 5. RAW10 重排：每拍两个像素，分别放入两个 16-bit 槽
    // -------------------------------------------------------------------------
    wire [31:0] pixels = {6'b0, csi_data[19:10], 6'b0, csi_data[9:0]};
`ifdef SC130_UVC
    camera_reset uvc_reset_i(.clk(video_clk),.locked(locked & ps_resetn),.resetn(uvc_resetn));
    (* ASYNC_REG="TRUE" *) reg [1:0] phy_fault_sync=2'b11;
    always @(posedge video_clk) phy_fault_sync<={phy_fault_sync[0],phy_reset|~phy_locked};
    vdma_write_guard write_guard_i(.clk(video_clk),.resetn(uvc_resetn),.start(epoch_start),
        .awvalid(dma_awvalid),.awready(dma_awready),.wvalid(dma_wvalid),.wready(dma_wready),.wlast(dma_wlast),.wstrb(dma_wstrb),.bvalid(dma_bvalid),.bready(dma_bready),.bresp(dma_bresp),.retired(dma_done),.error(dma_error),.bytes(dma_bytes));
    uvc_capture_path capture_i(.clk(video_clk),.resetn(uvc_resetn),.raw(csi_data[19:0]),.valid(valid),.sof(sof),.eol(eol),.source_error(phy_fault_sync[1]|bus_error|(valid & csi_user[1])),.dma_done(dma_done),.dma_error(dma_error),.epoch_start(epoch_start),
        .m_data(yuy2_data),.m_valid(yuy2_valid),.m_sof(yuy2_sof),.m_eol(yuy2_eol),.m_ready(yuy2_ready),.irq(cap_irq),
        .s_axi_awaddr(cap_awaddr[7:0]),.s_axi_awvalid(cap_awvalid),.s_axi_awready(cap_awready),
        .s_axi_wdata(cap_wdata),.s_axi_wstrb(cap_wstrb),.s_axi_wvalid(cap_wvalid),.s_axi_wready(cap_wready),
        .s_axi_bresp(cap_bresp),.s_axi_bvalid(cap_bvalid),.s_axi_bready(cap_bready),
        .s_axi_araddr(cap_araddr[7:0]),.s_axi_arvalid(cap_arvalid),.s_axi_arready(cap_arready),
        .s_axi_rdata(cap_rdata),.s_axi_rresp(cap_rresp),.s_axi_rvalid(cap_rvalid),.s_axi_rready(cap_rready));
    uvc_ila uvc_ila_i(.clk(video_clk),.probe0(yuy2_data),
        .probe1({24'b0,dma_done,dma_error,cap_irq,phy_fault_sync[1],yuy2_ready,yuy2_valid,yuy2_eol,yuy2_sof}));
`endif

    // -------------------------------------------------------------------------
    // 6. EMIO 控制同步：bit2 请求快照，bit3 复位统计，bit12:8 选择快照字
    // -------------------------------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [12:0] gc1 = 0, gc2 = 0;

    always @(posedge video_clk) begin
        gc1 <= go[12:0];
        gc2 <= gc1;
    end

    // -------------------------------------------------------------------------
    // 7. 图像统计与派生的行/帧事件脉冲
    // -------------------------------------------------------------------------
    wire [31:0] frame_count,
        line_count,
        total_word_count,
        last_frame_lines,
        last_line_pixels,
        frame_period_cycles,
        last_frame_words,
        data_xor,
        data_change_count;
    wire [9:0] data_min,
        data_max;
    wire active_video,
        hs,
        vs;
    capture_monitor monitor_i (
        .clk                 (video_clk),
        .resetn              (video_resetn & ~gc2[3]),
        .axis_tvalid         (valid),
        .axis_tready         (1'b1),
        .axis_tuser          (sof),
        .axis_tlast          (eol),
        .axis_tdata          (pixels),
        .frame_count         (frame_count),
        .line_count          (line_count),
        .total_word_count    (total_word_count),
        .last_frame_lines    (last_frame_lines),
        .last_line_pixels    (last_line_pixels),
        .frame_period_cycles (frame_period_cycles),
        .last_frame_words    (last_frame_words),
        .data_min            (data_min),
        .data_max            (data_max),
        .data_xor            (data_xor),
        .data_change_count   (data_change_count),
        .active_video        (active_video),
        .hs                  (hs),
        .vs                  (vs)
    );
    // Snapshot counters at one video clock edge. Select an entry after the request settles.
    // Both clocks come from the same MMCM: retain normal timing analysis for this bus.

    // -------------------------------------------------------------------------
    // 8. 同源时钟下的诊断流水与原子计数快照
    // -------------------------------------------------------------------------
    reg [193:0] ds1 = 0,
        ds2 = 0;

    always @(posedge video_clk) begin
        ds1 <= {
            bus_error, init_done,
            config_reg, status_reg,
            interrupt_status, interrupt_enable,
            lane0_info, lane1_info
        };
        ds2 <= ds1;
    end
    // 快照索引：0 magic；1~3 累计帧/行/拍；4~7 上帧行数/行宽/周期/拍数；
    // 8~11 像素最小/最大/XOR/变化次数；12~16 CSI 配置/状态/ISR/lane0/lane1；
    // 17 标志位。索引与 tools/capture_status.c 保持一致。
    reg snap_seen = 0;
    reg [31:0] snapshot[0:17];
    integer i;

    always @(posedge video_clk) begin
        if (!video_resetn) begin
            snap_seen <= 0;
            for (i = 0; i < 18; i = i + 1)
                snapshot[i] <= 0;
        end else if (gc2[2] != snap_seen) begin
            snap_seen <= gc2[2];
            snapshot[0] <= 32'h53433130;
            snapshot[1] <= frame_count;
            snapshot[2] <= line_count;
            snapshot[3] <= total_word_count;
            snapshot[4] <= last_frame_lines;
            snapshot[5] <= last_line_pixels;
            snapshot[6] <= frame_period_cycles;
            snapshot[7] <= last_frame_words;
            snapshot[8] <= {22'b0, data_min};
            snapshot[9] <= {22'b0, data_max};
            snapshot[10] <= data_xor;
            snapshot[11] <= data_change_count;
            snapshot[12] <= ds2[191:160];
            snapshot[13] <= ds2[159:128];
            snapshot[14] <= ds2[127:96];
            snapshot[15] <= ds2[63:32];
            snapshot[16] <= ds2[31:0];
            // [7:0] = PHY reset, IRQ, frame pulse, PHY lock, bus error, init done, MMCM lock, video resetn.
            snapshot[17] <= {24'b0, phy_reset, irq, frame_pulse, phy_locked, ds2[193], ds2[192], locked, video_resetn};
        end
    end
    wire [31:0] selected = gc2[12:8] < 18 ? snapshot[gc2[12:8]] : 32'hdeadbeef;
    assign gi = {31'b0, selected, 30'b0, camera_sda, camera_scl};

    // -------------------------------------------------------------------------
    // 9. ILA 探针：像素、事件、帧/行计数和帧周期
    // -------------------------------------------------------------------------
    camera_ila ila_i (
        .clk    (video_clk),
        .probe0 (pixels),
        .probe1 ({24'b0, phy_locked, locked, frame_pulse, vs, hs, active_video, eol, sof}),
        .probe2 (frame_count),
        .probe3 (line_count),
        .probe4 (last_frame_words),
        .probe5 (last_frame_lines),
        .probe6 (last_line_pixels),
        .probe7 (frame_period_cycles)
    );
endmodule
