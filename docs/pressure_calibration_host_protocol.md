# 压力传感器上位机标定协议

## 1. 串口参数

- 物理接口：`USART1`
- 串口参数：`9600, 8N1`
- MCU 周期上报：继续使用原 `AA 55 ... 55 AA` 21 字节帧
- 上位机命令：新增 `A5 5A ... 5A A5` 命令帧

## 2. MCU 周期上报帧

原上报协议不变，上位机按 `docs/cs1237_uart_protocol.md` 解码：

```text
AA 55 | 02 | 01 | 0C | Seq | RawCs1237(4) | WeightX10(4) | ThresholdG(2) | DeviceCode | CRC16(2) | 55 AA
```

## 3. 上位机命令帧

```text
A5 5A | Ver | Cmd | PayloadLen | Seq | Payload | CRC16(2) | 5A A5
```

字段说明：

| 字段 | 长度 | 说明 |
| --- | ---: | --- |
| Header | 2 | 固定 `A5 5A` |
| Ver | 1 | 固定 `0x01` |
| Cmd | 1 | 命令字 |
| PayloadLen | 1 | Payload 字节数 |
| Seq | 1 | 上位机命令序号 |
| Payload | N | 命令数据 |
| CRC16 | 2 | `CRC16/MODBUS`，little-endian |
| Tail | 2 | 固定 `5A A5` |

CRC 覆盖范围：

```text
Ver + Cmd + PayloadLen + Seq + Payload
```

## 4. 命令字

| Cmd | 名称 | Payload |
| --- | --- | --- |
| `0x01` | 握手 | 空 |
| `0x10` | 写入标定表 | `PointCount + (RawI32LE + RealX10U32LE) * N` |
| `0x11` | 清除标定表 | 空 |

写入标定表约束：

- `PointCount` 范围：`2..8`
- `RealX10` 单位：`0.1g`
- 本工装默认 8 点：托盘 `21.8g` + 砝码 `0/5/20/50/100/200/500/1000g`
- 对应目标值：`218, 268, 418, 718, 1218, 2218, 5218, 10218`
- 相邻 `Raw` 不允许相等；目标重量必须递增

## 5. MCU 应答帧

应答帧也使用 `A5 5A ... 5A A5`，`Cmd = 原命令 | 0x80`。

Payload 固定 4 字节：

```text
Status | RuntimeEnabled | PointCount | Reserved
```

状态码：

| Status | 说明 |
| --- | --- |
| `0x00` | 成功 |
| `0x01` | 帧格式错误 |
| `0x02` | Payload 长度错误 |
| `0x03` | 标定表非法 |
| `0x04` | Flash 写入失败 |
| `0x05` | 未知命令 |

## 6. 固件保存位置

- 运行时标定表保存地址：`0x08003800`
- 当前 EIDE IROM 限制为：`0x08000000..0x080037FF`
- 预留页用于避免 Flash 擦写标定表时覆盖程序

启动流程：

1. `WeightCalibration_LoadRuntimeFromFlash()` 读取标定表。
2. 校验通过后运行时标定表优先生效。
3. 校验失败或未写入时回退到 `weight_calibration.h` 里的编译期默认表。
