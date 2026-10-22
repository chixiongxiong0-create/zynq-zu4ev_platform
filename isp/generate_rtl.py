"""Generate fused black correction + vendor tone ROM, reproducibly from any CWD."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent

def create_lut(vendor, black):
    if not 0 <= black < 1023:
        raise ValueError('black must be in 0..1022')
    span = 1023 - black
    return [vendor[0 if raw <= black else ((raw-black)*1023+span//2)//span]
            for raw in range(1024)]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--black', type=int, default=52)
    args = parser.parse_args()
    vendor = [int(x, 16) for x in (ROOT/'vendor/sc130_tone_lut.mem').read_text().split()]
    assert len(vendor) == 1024 and vendor[0] == 16 and vendor[-1] == 235
    assert all(a <= b for a,b in zip(vendor,vendor[1:]))
    lut = create_lut(vendor,args.black)
    assert lut[args.black] == 16 and lut[-1] == 235
    head = f'''`timescale 1ns/1ps
// Adapted from vendor/sc130_tonemap_yuy2.v; vendor original remains unchanged.
// Fused RAW10 black correction ({args.black}) + normalized vendor tone curve.
// index = round(max(raw-{args.black},0)*1023/{1023-args.black}); Y=vendor[index].
// Constants are folded into the ROM: no divider, extra stage, or external mem file.
module sc130_tonemap_yuy2 (
 input wire clk,rst_n,ce,
 input wire s_valid,s_sof,s_eol,
 input wire [9:0] s_raw0,s_raw1,
 output reg m_valid,m_sof,m_eol,
 output wire [31:0] m_yuy2);
 (* rom_style="block" *) reg [7:0] tone_rom [0:1023];
 initial begin
'''
    tail = ''' end
 // Both luma registers belong to the same two-pixel lookup transaction.
 reg [7:0] y0_reg,y1_reg;
 always @(posedge clk) begin
  if(!rst_n) begin y0_reg<=8'd16;y1_reg<=8'd16;end
  else if(ce && s_valid) begin
   y0_reg<=tone_rom[s_raw0];
   y1_reg<=tone_rom[s_raw1];
  end
 end
 // Control advances on the same accepted beat as both luma values.
 always @(posedge clk) begin
  if(!rst_n) begin m_valid<=0;m_sof<=0;m_eol<=0;end
  else if(ce) begin
   m_valid<=s_valid;m_sof<=s_valid&&s_sof;m_eol<=s_valid&&s_eol;
  end
 end
 assign m_yuy2={8'h80,y1_reg,8'h80,y0_reg};
endmodule
'''
    (ROOT/'sc130_tonemap_yuy2.v').write_text(head+''.join(
        f"  tone_rom[{i}] = 8'h{v:02X};\n" for i,v in enumerate(lut))+tail)
    (ROOT/'black_corrected_tone_lut.mem').write_text(''.join(f'{x:02X}\n' for x in lut))
    (ROOT/'tone_config.json').write_text(json.dumps({
        'black_level_raw10':args.black,'white_level_raw10':1023,
        'span':1023-args.black,'range':'limited','Y_black':16,'Y_white':235,
        'normalization':'round-half-up',
        'vendor_lut_sha256':hashlib.sha256((ROOT/'vendor/sc130_tone_lut.mem').read_bytes()).hexdigest(),
        'corrected_lut_sha256':hashlib.sha256(bytes(lut)).hexdigest()},indent=2)+'\n')
    print(f'ISP_LUT_PASS black={args.black} span={1023-args.black} entries={len(lut)}')

if __name__ == '__main__':
    main()
