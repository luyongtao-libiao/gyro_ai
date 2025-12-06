#ifndef __XV7011_SPI_H
#define __XV7011_SPI_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

// XV7011寄存器地址
#define XV7011_REG_STATUS            0x04    // 状态寄存器
#define XV7011_REG_SLEEPOUT          0x06    // 退出睡眠模式
#define XV7011_REG_TEMP_READ         0x08    // 温度读取
#define XV7011_REG_ANGULAR_RATE      0x0A    // 角速度读取
#define XV7011_REG_OUTCTL1           0x0B    // 角速度输出控制
#define XV7011_REG_TEMP_FORMAT       0x1C    // 温度数据格式

// 温度数据格式控制（TsDataFormat寄存器，bit6-5）
#define XV7011_TEMP_FORMAT_8BIT      0x00    // 00: 8位输出
#define XV7011_TEMP_FORMAT_10BIT     0x01    // 01: 10位输出
#define XV7011_TEMP_FORMAT_12BIT     0x02    // 10: 12位输出（默认）

// 角速度控制位定义（OUTCTL1寄存器，bit1-0）
#define XV7011_OUTCTL_ANGULAR_RATE   0x01    // 01: 角速度数据输出（文档6-11表格）

// 数据格式控制（OUTCTL1寄存器，bit2）
#define XV7011_FORMAT_16BIT          0       // 0: 16位输出
#define XV7011_FORMAT_24BIT          1       // 1: 24位输出

// SPI读写控制位
#define XV7011_WRITE_MASK            0x00
#define XV7011_READ_MASK             0x80

// 数据结构体
typedef struct {
    float angular_rate;      // 角速度 (°/s)
    float temperature;       // 温度 (°C)
    uint32_t raw_angular;    // 原始角速度数据
    uint16_t raw_temp;       // 原始温度数据
    uint8_t data_format;     // 数据格式：0=16位，1=24位
    uint8_t temp_format;     // 温度格式：0=8位，1=10位，2=12位
} XV7011_Data_t;

typedef struct {
    float angle;           // 累积角度（°）
    float last_time;       // 上次采样时间
    float bias_calibration;// 零偏校准值
} angle_integrator_t;

// 函数声明
void XV7011_CS_Enable(void);
void XV7011_CS_Disable(void);
HAL_StatusTypeDef XV7011_WriteReg(uint8_t reg_addr, uint8_t data);
HAL_StatusTypeDef XV7011_ReadReg(uint8_t reg_addr, uint8_t *data);
HAL_StatusTypeDef XV7011_ReadRegMulti(uint8_t reg_addr, uint8_t *data, uint8_t len); 
HAL_StatusTypeDef XV7011_Init(angle_integrator_t *integrator);
HAL_StatusTypeDef XV7011_ReadTemperature(float *temperature);
HAL_StatusTypeDef XV7011_ReadAngularRate(float *angular_rate);
HAL_StatusTypeDef XV7011_ReadAllData(XV7011_Data_t *data);

#endif
