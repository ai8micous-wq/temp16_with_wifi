# temp16_with_wifi

基于 ESP-IDF 6.x 的 ESP32-C3 多路温度巡温仪项目。

项目目标是实现一台 16 路热电偶巡温设备，包含：

- MAX31856 多路采样
- UART 串口屏显示与参数下发
- Wi-Fi 联网与 MQTT 上下行
- BLE 扫码配网
- 本地掉线缓存
- 量产一机一码出厂配网数据

## 当前状态

当前工程已经不是最初的“空骨架”了，主链路基本已经接起来：

- 16 路 MAX31856 采样链路已接通
- 通道原始温度寄存器与故障位调试日志已补充
- 串口屏上行温度刷新已实现
- 串口屏下行配置解析已实现
- MQTT 遥测、配置上报、状态上报已实现
- MQTT 下行 `config / cmd / time` 处理已实现
- BLE `network_provisioning` 配网已接入
- GPIO4 配网复位按键已接入
- GPIO8 单灯状态指示逻辑已接入
- MQTT 首次联网和周期性时间同步请求已接入
- 缓存记录 CRC 校验与坏记录跳过已接入
- `cache` 分区双页元数据轮换已接入
- 量产版出厂二维码数据已拆到独立 `fctry` 分区
- `factory_reset` 只清用户配置，不清出厂二维码数据

## 目录结构

- [main/app_main.c](main/app_main.c)：系统启动入口、任务编排
- [components/app_common/include/app_types.h](components/app_common/include/app_types.h)：核心数据结构
- [components/board](components/board)：板级引脚定义与 LED 控制
- [components/cs_mux](components/cs_mux)：16 路片选复用
- [components/max31856](components/max31856)：MAX31856 驱动
- [components/temp_sample](components/temp_sample)：采样任务、报警判定、调试日志
- [components/lcd_proto](components/lcd_proto)：串口屏协议收发
- [components/mqtt_service](components/mqtt_service)：MQTT 连接、上下行消息处理
- [components/wifi_service](components/wifi_service)：Wi-Fi 与 BLE 配网
- [components/factory_info](components/factory_info)：出厂二维码/配网数据读取
- [components/nvs_config](components/nvs_config)：用户配置持久化
- [components/storage_service](components/storage_service)：温度缓存分区
- [docs/factory_provisioning.md](docs/factory_provisioning.md)：量产一机一码设计说明

## 已实现功能

### 1. 温度采样

- SPI 总线与 16 路通道切换
- MAX31856 自动转换模式初始化
- 读取热端温度寄存器与状态寄存器
- 根据状态寄存器映射开路/高温/低温/通信故障
- 采样结果写入 `temp_frame_t`
- 支持按通道报警上下限叠加判定

### 2. 调试日志

`temp_sample` 已增加更有用的巡检日志，包含：

- 每路 `temp_x10`
- 每路 `fault`
- 最近一次 MAX31856 原始温度寄存器 3 字节
- 最近一次状态寄存器 `SR`
- 首次、变化时、周期性整包打印

### 3. 串口屏

已实现：

- 周期推送 16 路温度显示
- 推送标签、使能、上下限配置
- 解析屏幕回传控件修改
- 支持修改：
  - 通道标签
  - 通道启用/禁用
  - 高低报警阈值
- 配置变更后回写内存、触发保存与 MQTT 配置上报

当前限制：

- RTC 回传只记录日志，还没有反向参与系统时钟同步
- 协议 ID 和控件地址仍是当前屏幕版本绑定值，后续若 UI 改版需同步调整

### 4. MQTT

已实现上报：

- `up/telemetry`
- `up/config`
- `up/status`
- `up/event`
- `up/ack`

已实现下行：

- `down/config`
  - `channel_label`
  - `channel_alarm`
  - `device_name`
  - `full_config`
- `down/cmd`
  - `report_now`
  - `clear_cache`
  - `factory_reset`
  - `reboot`
- `down/time`
  - 设置系统时间
- `down/ack`
  - 业务层确认缓存遥测已被云端成功处理

当前限制：

- `enter_ble_provision` 仍返回 unsupported
- MQTT QoS/重试策略仍偏工程调试风格，未完全产品化

### 5. Wi-Fi 与 BLE 配网

已实现：

- 基于乐鑫 `network_provisioning` 的 BLE 配网
- 启动时自动判断“已配网/未配网”
- 已配网则直接 STA 联网
- 未配网则进入 BLE 配网模式
- 串口打印调试用 QR payload/URL
- GPIO4 长按 2 秒清除 Wi-Fi 配网信息并重启
- 重启后自动重新进入配网状态

当前设计：

- 开发板未烧录出厂数据时，回退到开发凭据，方便联调
- 量产时从 `fctry` 分区读取一机一码数据

### 6. 时间同步策略

已实现：

- `down/time` 下发后设置系统时钟
- MQTT 连接成功后主动请求一次时间同步
- 后续按 `time_sync_interval_s` 周期请求时间同步
- 最近一次成功同步的 UTC 时间会记录在状态上报里

当前行为：

- `up/event` 会发送 `time_sync_request`
- 收到有效 `down/time` 后发送 `time_sync_ok`
- 设置系统时间失败时发送 `time_sync_fail`
- `up/status` 包含 `last_time_sync_ts`

当前限制：

- 还没有 NTP 兜底
- 串口屏 RTC 回传还没有反向参与系统时钟同步
- 时区仅透传日志，不做独立持久化处理

### 7. 状态灯与复位按键

硬件定义：

- LED3 连接 GPIO8，高电平点亮
- 配网复位按键连接 GPIO4，另一端接 GND
- GPIO4 配置为输入上拉，按下为低电平

当前行为：

- 未配网但尚未开始 provisioning 时，LED3 慢闪，每 1 秒翻转一次
- 正在配网时，LED3 快闪，每 0.3 秒翻转一次
- Wi-Fi 和 MQTT 都连接成功后，LED3 常亮
- 已配网但当前未同时连上 Wi-Fi 和 MQTT 时，LED3 熄灭
- GPIO4 低电平持续超过 2 秒后，系统清除 Wi-Fi 配网信息并重启

### 8. 缓存与容错

已实现：

- 采样按上报周期进入本地 `cache`
- MQTT 重连后自动按顺序补发缓存
- 缓存补发先等待 MQTT Broker publish ack，再等待云端 `down/ack`
- 只有收到匹配 `frame_seq / reply_to` 的业务层成功 ack 后才推进缓存发送游标
- 缓存记录自带 CRC 校验
- 读取到坏记录时自动跳过并继续补发
- `cache` 元数据使用双页轮换，避免单页覆盖式更新
- 启动时自动校验缓存元数据，异常时自动重建
- 业务层 ack 超时后自动重发同一条缓存，避免补发链路卡死

当前行为：

- 温度缓存是“上报节奏数据”，不是每个 500 ms 采样点都落盘
- `up/telemetry` 和 `up/status` 会携带 `cache_pending`
- 缓存补发期间如果 MQTT 断开，未确认成功的记录不会标记已发送
- `up/telemetry` 中缓存记录自带 `msg_id=tm_cache_<frame_seq>` 和 `frame_seq`
- 云端应在处理完成后回 `down/ack`
  - 推荐字段：`reply_to` 或 `frame_seq`
  - `result=0` 表示成功，可删除缓存
  - 非 0 表示业务拒绝，设备会保留并重发

当前限制：

- 元数据虽然已经双页轮换，但还没有做到更细粒度的页内版本链
- 目前只对单条记录做 CRC 校验，不做整页校验

### 9. 一机一码量产数据

已实现：

- 单独 `fctry` 分区保存出厂配网数据
- `factory_info_record_t` 结构定义
- 出厂数据与用户配置分层
- `factory_reset` 不擦除出厂二维码数据
- 可由出厂数据覆盖默认 `device_id` / `device_name`

详细设计见：

- [docs/factory_provisioning.md](docs/factory_provisioning.md)

## 当前还需要补全的功能

下面这些是我看完当前项目后，优先级比较高的待补项。

### P1. 量产写入工具

当前“读出厂数据”的固件侧已经有了，但“怎么生成并烧录每台设备的二维码资料”还没落成工具链。

还需要补：

- 产线 CSV/JSON 数据格式
- `username / pop -> salt / verifier` 生成工具
- `fctry` 分区 NVS 镜像生成脚本
- 实际烧录流程文档

### P2. 时间同步进一步增强

当前已经有 MQTT 驱动的时间同步闭环，后续还可以继续补：

- NTP 兜底
- 串口屏 RTC 联动
- 时间同步失败退避重试策略
- 时区/夏令时产品策略

### P2. MQTT 配置项收口

系统里目前还保留了：

- `report_interval_s`
- `time_sync_interval_s`
- MQTT broker/用户名/密码默认值

但这些配置的产品策略还没有完全定型。需要决定：

- 是不是允许云端改 broker
- 是不是允许串口屏改上传周期
- 是不是把更多配置固定在出厂或固件里

### P2. 存储层可靠性进一步增强

`storage_service` 目前已经有双页元数据轮换、记录 CRC 校验和坏记录跳过，后续还能再加强：

- 元数据页内多版本链
- 更细粒度的断电一致性恢复
- 擦写寿命评估
- 更丰富的缓存统计与诊断信息

### P3. 工程清理

还有一些非阻塞但建议后续收口的点：

- 若后续正式量产，建议把 `wifi_service` 中的开发 fallback 凭据完全关掉，避免误出货

## 构建

```bash
idf.py set-target esp32c3
idf.py reconfigure
idf.py build
idf.py -p PORT flash monitor
```

## 依赖说明

项目当前使用到的外部组件包括：

- `espressif/mqtt`
- `espressif/cjson`
- `espressif/network_provisioning`

说明：

- `network_provisioning` 通过 [components/wifi_service/idf_component.yml](components/wifi_service/idf_component.yml) 引入
- 如果本机无法访问 Espressif 组件仓库，首次 `reconfigure/build` 会失败

## 分区说明

- `nvs`：用户运行配置
- `fctry`：出厂一机一码配网数据
- `cache`：离线温度缓存

其中：

- `factory_reset` 只擦除 `nvs`
- `fctry` 用于保存量产二维码与 Security 2 凭据

## 建议下一步

如果按产品交付角度排优先级，我建议下一步按这个顺序推进：

1. 先把“产线生成/烧录一机一码数据工具”做完整
2. 再补云端业务 ack 的去重策略和联调测试用例
3. 然后继续做时间同步和存储可靠性的深度产品化
