# temp16_idf_project

基于用户提供的《物联网多路温度巡温仪-系统架构-工程实现参数》生成的 ESP-IDF 工程骨架。

## 已包含
- 工程目录结构与组件拆分
- 核心数据结构
- Wi-Fi STA 骨架
- MQTT 连接与遥测/配置上报骨架
- UART 串口屏发送骨架
- MAX31856 总线/通道抽象
- 独立 cache 分区与环形缓存元数据框架
- 采样任务 / UI 任务 / MQTT 发布任务 / 状态任务

## 目前是“可扩展骨架”，还需要你继续补全的部分
1. MAX31856 真实寄存器读写细节（当前为占位假数据）
2. 串口屏下行指令解析与参数回写
3. BLE 配网
4. MQTT 下行 JSON 解析与命令执行
5. cache 元数据耐久化增强（建议双页轮换）
6. 生产级 CRC、重试与错误恢复

## 构建
```bash
idf.py set-target esp32c3
idf.py build
idf.py -p PORT flash monitor
```

## 关键组件
- `main/app_main.c`：任务编排和启动入口
- `components/app_common/include/app_types.h`：核心数据结构
- `components/max31856/`：MAX31856 抽象
- `components/lcd_proto/`：串口屏发送协议
- `components/mqtt_service/`：MQTT 上报骨架
- `components/storage_service/`：环形缓存骨架
