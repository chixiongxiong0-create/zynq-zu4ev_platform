# 曝光模式实板确认（2026-10-21）

用户报告画面偏暗，要求确认自动曝光是否开启。未修改传感器曝光、增益或流状态，也未重启板卡。

GPIO模拟I2C只读传感器寄存器成功，见exposure_sensor_readback_retry.log：ID3107/3108=01/30，0100=01，3e01=08，3e02=00，3e03=0b，3e08=02，3e09=20。与boot/fsbl/camera/sc130_table.h初始化表一致。

SmartSens SC130GS设计应用指南V2.0表4-2定义3e03 bit0为AEC manual、bit1为AGC manual，置1均为手动。当前0x0b的这两位均为1，因此自动曝光和自动增益均未启用。厂商编写指南公开副本：https://www.scribd.com/document/1035683943/SC130GS-设计应用指南-V2-0 。

软件无运行时AE/AGC闭环；UVC控制只处理视频格式及帧率协商，未接入曝光控制。附件ISP是固定LUT，不能自动调整sensor曝光。画面偏暗与当前固定曝光/增益配置相符，但并不证明它是唯一影响亮度的因素。本次未在缺少完整曝光时序推导的情况下换算毫秒数。

临时只读工具在build/exposure-probe/read_sensor.c，I2C读事务复用已验证FSBL实现，通过/dev/mem访问现有GPIO，未请求sensor写寄存器事务。初次大量串口上传后等待回执超时，重新取得控制台后用timeout 8执行读取成功，未使用重启恢复。
