# 压力传感器自动标定上位机

静态网页工具，通过 Web Serial 连接压力传感器采集板 `USART1 9600 8N1`。

## 启动

双击 `启动上位机.vbs`，或手动运行：

```powershell
cd D:\EH_main\soft\Pressure_STM32F030F4\Tools\pressure-calibration-host
npm run serve
```

然后用 Chrome 或 Edge 打开：

```text
http://localhost:4174
```

## 标定流程

1. 连接采集板串口。
2. 保持托盘在传感器上，托盘重量默认 `21.8g`。
3. 点击“开始自动标定”。
4. 按页面当前点提示依次放置砝码：`0g、5g、20g、50g、100g、200g、500g、1000g`。
5. 每个点稳定后自动采样；全部完成后点击“写入 MCU 标定表”。
6. MCU 收到标定表后写入 Flash，并立即使用新表换算重量。

## 固件通信

- 周期上报帧保持原协议：`AA 55 ... 55 AA`，固定 21 字节。
- 标定命令帧新增独立帧头：`A5 5A ... 5A A5`。
- 标定表 payload：`PointCount + (RawI32LE + RealX10U32LE) * N`。
- MCU 应答 payload：`Status + RuntimeEnabled + PointCount + Reserved`。

## Flash 分区

运行时标定表保存于 `0x08003800`，EIDE IROM 上限已改为 `0x08000000 + 0x3800`，避免擦写标定页时覆盖程序。
