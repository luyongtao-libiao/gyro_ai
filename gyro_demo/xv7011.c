#include "xv7011.h"
#include <stdio.h>
#include <../CMSIS_RTOS/cmsis_os.h>

// 外部声明SPI句柄
extern SPI_HandleTypeDef hspi2;

// GPIO定义
#define XV7011_CS_PORT    GPIOB
#define XV7011_CS_PIN     GPIO_PIN_12

// 根据文档3-5章节的刻度因子
#define SCALE_FACTOR_16BIT  280.0f      // 16位输出：280 LSB/(°/s)（文档表格）
#define SCALE_FACTOR_24BIT  71680.0f    // 24位输出：71680 LSB/(°/s)（文档表格）

// 根据文档3-6章节的温度系数
#define TEMP_COEFF_8BIT     1.0f        // 8位模式：1 LSB/°C（典型值）
#define TEMP_COEFF_10BIT    4.0f        // 10位模式：4 LSB/°C
#define TEMP_COEFF_12BIT    16.0f       // 12位模式：16 LSB/°C（默认）

// 温度偏移值（文档表格：+25°C时的输出码）
#define TEMP_OFFSET_8BIT    25          // 8位模式：25 LSB @25°C
#define TEMP_OFFSET_10BIT   100         // 10位模式：100 LSB @25°C
#define TEMP_OFFSET_12BIT   400         // 12位模式：400 LSB @25°C

// CS控制
void XV7011_CS_Enable(void) {
    HAL_GPIO_WritePin(XV7011_CS_PORT, XV7011_CS_PIN, GPIO_PIN_RESET);
    __NOP(); __NOP(); __NOP(); __NOP();
}

void XV7011_CS_Disable(void) {
    __NOP(); __NOP(); __NOP(); __NOP();
    HAL_GPIO_WritePin(XV7011_CS_PORT, XV7011_CS_PIN, GPIO_PIN_SET);
}

void init_integrator(angle_integrator_t *integrator) {
    integrator->angle = 0.0f;
    integrator->last_time = 0.0f;
    integrator->bias_calibration = 0.0f;
}

// 寄存器读写函数
HAL_StatusTypeDef XV7011_WriteReg(uint8_t reg_addr, uint8_t data) {
    HAL_StatusTypeDef status;
    uint8_t tx_buffer[2];
    uint8_t rx_buffer[2];
    
    tx_buffer[0] = reg_addr;
    tx_buffer[1] = data;
    
    XV7011_CS_Enable();
    status = HAL_SPI_TransmitReceive(&hspi2, tx_buffer, rx_buffer, 2, 100);
    XV7011_CS_Disable();
    
    return status;
}

HAL_StatusTypeDef XV7011_ReadReg(uint8_t reg_addr, uint8_t *data) {
    HAL_StatusTypeDef status;
    uint8_t tx_buffer = 0;
    uint8_t rx_buffer = 0;
    
    tx_buffer = reg_addr | XV7011_READ_MASK;
    
    XV7011_CS_Enable();
    
    // 发送地址
    status = HAL_SPI_TransmitReceive(&hspi2, &tx_buffer, &rx_buffer, 1, 100);
    if (status != HAL_OK) {
        XV7011_CS_Disable();
        return status;
    }
    
    // 接收数据
    tx_buffer = 0x00;
    status = HAL_SPI_TransmitReceive(&hspi2, &tx_buffer, &rx_buffer, 1, 100);
    
    if (data != NULL) {
        *data = rx_buffer;
    }
    
    XV7011_CS_Disable();
    return status;
}

// 读取多字节寄存器数据
HAL_StatusTypeDef XV7011_ReadRegMulti(uint8_t reg_addr, uint8_t *data, uint8_t len) {
    HAL_StatusTypeDef status;
    uint8_t tx_buffer = 0;
    
    if (data == NULL || len == 0) {
        return HAL_ERROR;
    }
    
    tx_buffer = reg_addr| XV7011_READ_MASK;
    
    XV7011_CS_Enable();
    
    // 发送地址
    status = HAL_SPI_TransmitReceive(&hspi2, &tx_buffer, &tx_buffer, 1, 100);
    if (status != HAL_OK) {
        XV7011_CS_Disable();
        return status;
    }
    
    // 连续接收多个字节
    for (uint8_t i = 0; i < len; i++) {
        tx_buffer = 0x00;
        status = HAL_SPI_TransmitReceive(&hspi2, &tx_buffer, &data[i], 1, 100);
        if (status != HAL_OK) {
            break;
        }
    }
    
    XV7011_CS_Disable();
    return status;
}

// 初始化函数
HAL_StatusTypeDef XV7011_Init(angle_integrator_t *integrator) {
    HAL_StatusTypeDef status;
    uint8_t temp_reg;
    
    // 等待上电稳定（文档3-4章节）
    osDelay(10);
    
    // 退出睡眠模式
    // status = XV7011_WriteReg(XV7011_REG_SLEEPOUT, 0x00);
    // if (status != HAL_OK) return status;
    osDelay(50);  // 增加等待时间，确保芯片完全退出睡眠
    
    // // 配置角速度输出（根据文档6-11章节）
    // // Bit2=0: 16位输出，Bit1-0=01: 角速度数据输出
    // status = XV7011_WriteReg(XV7011_REG_OUTCTL1, 0x01);  // 0x01 = 0b00000001
    // if (status != HAL_OK) return status;
    
    // // 配置温度传感器为12位输出（默认，文档6-15章节）
    // // 读取当前温度格式
    // status = XV7011_ReadReg(XV7011_REG_TEMP_FORMAT, &temp_reg);
    // if (status != HAL_OK) return status;
    
    // // 确保温度传感器格式正确（bit6-5）
    // temp_reg &= ~0x60;  // 清除bit6-5
    // temp_reg |= (0x02 << 5);  // 设置为12位输出(10)
    // status = XV7011_WriteReg(XV7011_REG_TEMP_FORMAT, temp_reg);

    init_integrator(integrator);
	return HAL_OK;
}

// 读取温度值（DEMO函数）
HAL_StatusTypeDef XV7011_ReadTemperature(float *temperature) {
    HAL_StatusTypeDef status;
    uint8_t tx_buffer[3] = {0};
    uint8_t rx_buffer[3] = {0};
    uint16_t raw_temp = 0;
    float temp_celsius;
    uint8_t temp_format;
    
    // 步骤1：读取温度数据格式（文档5-6章节）
    status = XV7011_ReadReg(XV7011_REG_TEMP_FORMAT, &temp_format);
    if (status != HAL_OK) return status;
    
    // 提取温度格式位（bit6-5）
    temp_format = (temp_format >> 5) & 0x03;
    
    // 步骤2：发送温度读取命令（文档5-6和6-8章节）
    // 寄存器0x08: 温度传感器数据读取
    tx_buffer[0] = XV7011_REG_TEMP_READ | XV7011_READ_MASK;
    
    XV7011_CS_Enable();
    
    // 发送读命令
    status = HAL_SPI_TransmitReceive(&hspi2, tx_buffer, rx_buffer, 1, 100);
    if (status != HAL_OK) {
        XV7011_CS_Disable();
        return status;
    }
    
    // 根据文档表5.6，不同格式的字节数不同
    uint8_t bytes_to_read = 1;  // 默认8位
    if (temp_format == 0x01) bytes_to_read = 2;  // 10位：2字节
    if (temp_format == 0x02) bytes_to_read = 2;  // 12位：2字节
    
    // 接收温度数据字节（发送dummy字节来接收实际数据）
    for (int i = 0; i < bytes_to_read; i++) {
        tx_buffer[0] = 0x00;
        status = HAL_SPI_TransmitReceive(&hspi2, tx_buffer, &rx_buffer[i], 1, 100);
        if (status != HAL_OK) break;
    }
    
    XV7011_CS_Disable();
    
    if (status != HAL_OK) return status;
    
    // 步骤3：解析原始数据（2的补码格式，文档5-6章节）
    if (temp_format == 0x00) {  // 8位模式
        raw_temp = (int8_t)rx_buffer[0];  // 8位有符号
    } else if (temp_format == 0x01) {  // 10位模式
        raw_temp = ((rx_buffer[0] & 0xFF) << 2) | (rx_buffer[1] >> 6);
        if (raw_temp & 0x200) raw_temp |= 0xFC00;  // 符号扩展
    } else {  // 12位模式（默认）
        raw_temp = ((rx_buffer[0] & 0xFF) << 4) | (rx_buffer[1] >> 4);
        if (raw_temp & 0x800) raw_temp |= 0xF000;  // 符号扩展
    }
    
    // 步骤4：转换为摄氏度（文档3-6章节）
    // 温度计算公式：T = (原始值 - 偏移量) / 温度系数 + 25°C
    switch (temp_format) {
        case 0x00:  // 8位
            temp_celsius = ((float)raw_temp - TEMP_OFFSET_8BIT) / TEMP_COEFF_8BIT + 25.0f;
            break;
        case 0x01:  // 10位
            temp_celsius = ((float)raw_temp - TEMP_OFFSET_10BIT) / TEMP_COEFF_10BIT + 25.0f;
            break;
        default:    // 12位
            temp_celsius = ((float)raw_temp - TEMP_OFFSET_12BIT) / TEMP_COEFF_12BIT + 25.0f;
            break;
    }
    
    // 考虑温度精度（文档表格：±5°C）
    // 如果需要更高精度，可以添加校准
    if (temperature != NULL) {
        *temperature = temp_celsius;
    }
    
    return HAL_OK;
}

// 读取角速度值（DEMO函数）
HAL_StatusTypeDef XV7011_ReadAngularRate(float *angular_rate) {
    HAL_StatusTypeDef status;
    uint8_t tx_buffer[4] = {0};
    uint8_t rx_buffer[4] = {0};
    int32_t raw_angular = 0;
    float angular_rate_dps;
    uint8_t data_format;
    uint8_t outctl_reg;
    
    // 步骤1：读取输出控制寄存器（文档6-11章节）
    status = XV7011_ReadReg(XV7011_REG_OUTCTL1, &outctl_reg);
    if (status != HAL_OK) return status;
    
    // 检查是否启用了角速度输出（bit1-0 = 01）
    if ((outctl_reg & 0x03) != 0x01) {
        // 如果未启用，则启用它
        outctl_reg = (outctl_reg & 0xFC) | 0x01;  // 保持bit2不变，设置bit1-0=01
        status = XV7011_WriteReg(XV7011_REG_OUTCTL1, outctl_reg);
        if (status != HAL_OK) return status;
    }
    
    // 获取数据格式（bit2）
    data_format = (outctl_reg >> 2) & 0x01;
    
    // 步骤2：发送角速度读取命令（文档5-5和6-10章节）
    // 寄存器0x0A: 角速度数据读取
    tx_buffer[0] = XV7011_REG_ANGULAR_RATE  | XV7011_READ_MASK;
    
    XV7011_CS_Enable();
    
    // 发送读命令
    status = HAL_SPI_TransmitReceive(&hspi2, tx_buffer, rx_buffer, 1, 100);
    if (status != HAL_OK) {
        XV7011_CS_Disable();
        return status;
    }
    
    // 根据数据格式决定读取的字节数（文档5.5表格）
    uint8_t bytes_to_read = 2;  // 16位：2字节
    if (data_format == 1) bytes_to_read = 3;  // 24位：3字节
    
	

    // 一次性读取所有角速度数据字节
    for (int i = 0; i < bytes_to_read; i++) {
        tx_buffer[0] = 0x00;
	    status = HAL_SPI_TransmitReceive(&hspi2, &tx_buffer[0], &rx_buffer[i], 1, 100);
        if (status != HAL_OK) break;
    }
 
    XV7011_CS_Disable();
    
    if (status != HAL_OK) return status;
    
    // 步骤3：解析原始数据（2的补码格式，文档2-3章节）
    if (data_format == 0) {  // 16位输出
        raw_angular = ((int16_t)rx_buffer[0] << 8) | rx_buffer[1];
	    raw_angular = (int16_t)raw_angular;
    } else {  // 24位输出
        raw_angular = ((int32_t)rx_buffer[0] << 16) | 
                      ((int32_t)rx_buffer[1] << 8) | 
                      rx_buffer[2];
        
	   if (raw_angular & 0x800000) raw_angular |= 0xFF000000;
    }
    
    // 步骤4：转换为角速度(°/s)（文档3-5章节）
    // 角速度 = 原始值 / 刻度因子
    if (data_format == 0) {
        angular_rate_dps = (float)raw_angular / SCALE_FACTOR_16BIT;
    } else {
        angular_rate_dps = (float)raw_angular / SCALE_FACTOR_24BIT;
    }
  
    // 考虑测量范围（文档表格：±100°/s）
    //if (angular_rate_dps > 100.0f) angular_rate_dps = 100.0f;
    //if (angular_rate_dps < -100.0f) angular_rate_dps = -100.0f;
    
    if (angular_rate != NULL) {
        *angular_rate = angular_rate_dps;
    }
    
    return HAL_OK;
}

// 读取所有数据（完整的DEMO）
HAL_StatusTypeDef XV7011_ReadAllData(XV7011_Data_t *data) {
    HAL_StatusTypeDef status;
    
    if (data == NULL) {
        return HAL_ERROR;
    }
    
    // 读取温度
    status = XV7011_ReadTemperature(&data->temperature);
    if (status != HAL_OK) {
        data->temperature = 0.0f;
    }
    
    // 读取角速度
    status = XV7011_ReadAngularRate(&data->angular_rate);
    if (status != HAL_OK) {
        data->angular_rate = 0.0f;
    }
    
    // 读取其他信息（可选）
    uint8_t temp;
    
    // 读取状态寄存器
    XV7011_ReadReg(XV7011_REG_STATUS, &temp);
    data->data_format = (temp >> 2) & 0x01;  // 从OUTCTL1读取更准确
    
    XV7011_ReadReg(XV7011_REG_TEMP_FORMAT, &temp);
    data->temp_format = (temp >> 5) & 0x03;
    
    return HAL_OK;
}
