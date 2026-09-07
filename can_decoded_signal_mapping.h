#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct CanDecodedSignalDef {
    std::uint32_t canId;
    int dataIndex;
    const char *variableName;
    const char *displayNameZh;
    const char *unit;
    const char *source;
};

inline constexpr std::array<CanDecodedSignalDef, 132> kCanDecodedSignalDefs = {{
    {0x18FF481E, 0, "EngineSpeed_rpm", "发动机转速", "rpm", "油装"},
    {0x18FF481E, 1, "FuelLevel_pct", "燃油液位百分比", "%", "油装"},
    {0x18FF481E, 2, "VehicleSpeed_kmh", "车速", "km/h", "油装"},
    {0x18FF481E, 3, "ConverterOilTemp_degC", "变矩器油温", "degC", "油装"},
    {0x18FF481E, 4, "EngineWaterTemp_degC", "发动机水温", "degC", "油装"},
    {0x18FF481E, 5, "GearboxOilTemp_degC", "变速箱油温", "degC", "混动"},

    {0x18FF491E, 0, "FNRState", "前进空倒挡状态", "", "油装"},
    {0x18FF491E, 1, "GearDisplay", "档位显示", "", "油装"},
    {0x18FF491E, 2, "RemoteLinkState", "远程连接状态", "", "油装"},
    {0x18FF491E, 3, "SymbolPairState", "符号对状态", "", "油装"},
    {0x18FF491E, 4, "ControlledVehicleNo", "受控车辆号", "", "油装"},
    {0x18FF491E, 5, "RemoteIconState", "远程图标状态", "", "油装"},
    {0x18FF491E, 6, "WorkModeDisplay", "工作模式显示", "", "油装"},
    {0x18FF491E, 7, "WorkModeFlashState", "工作模式闪烁状态", "", "油装"},
    {0x18FF491E, 8, "ParkingBrakeIcon", "驻车制动图标", "", "油装"},
    {0x18FF491E, 9, "EStopIconState", "紧急停止图标状态", "", "油装"},
    {0x18FF491E, 10, "WorkLightState", "工作灯状态", "", "油装"},
    {0x18FF491E, 11, "TurnSignalState", "转向信号状态", "", "油装"},
    {0x18FF491E, 12, "BrakePressure_MPa", "制动压力", "MPa", "油装"},
    {0x18FF491E, 13, "SingleBucketWeight_kg", "单斗重量", "kg", "油装"},

    {0x18FF4A1E, 0, "BoomAngle_deg_raw", "动臂角度原始值", "deg", "油装"},
    {0x18FF4A1E, 1, "BucketAngle_deg_raw", "铲斗角度原始值", "deg", "油装"},
    {0x18FF4A1E, 2, "SteeringAngle_deg", "转向角度", "deg", "油装"},
    {0x18FF4A1E, 3, "FuelWaterAlarm", "燃油水分报警", "", "油装"},
    {0x18FF4A1E, 4, "OilPressureAlarm", "机油压力报警", "", "油装"},
    {0x18FF4A1E, 5, "BrakeLowPressureAlarm", "制动低压报警", "", "油装"},
    {0x18FF4A1E, 6, "HighLowBeamState", "远近光灯状态", "", "油装"},

    {0x18FF4B1E, 0, "PitchAngle_deg", "俯仰角", "deg", "油装"},
    {0x18FF4B1E, 1, "RollAngle_deg", "翻滚角", "deg", "油装"},

    {0x18FF221E, 0, "OperationMode", "操作模式", "", "油装"},
    {0x18FF221E, 1, "RemoteSignalOK", "远程信号正常", "", "油装"},
    {0x18FF221E, 2, "ControllerHeartbeatOK", "控制器心跳正常", "", "油装"},
    {0x18FF221E, 3, "GearModeReady", "档位模式就绪", "", "油装"},
    {0x18FF221E, 4, "FNRAtN", "前进空倒挡处于空挡", "", "油装"},
    {0x18FF221E, 5, "ControllerStateConsistent", "控制器状态一致", "", "油装"},
    {0x18FF221E, 6, "BrakeSystemNoEStop", "制动系统无紧急停止", "", "油装"},
    {0x18FF221E, 7, "RemoteEStopNotPresent", "远程紧急停止不存在", "", "油装"},
    {0x18FF221E, 8, "ShiftLeverAtN", "换挡杆处于空挡", "", "油装"},
    {0x18FF221E, 9, "ParkingSwitchPulled", "驻车开关已拉起", "", "油装"},
    {0x18FF221E, 10, "FaultCodeRaw24", "故障码原始值（24位）", "", "油装"},
    {0x18FF221E, 11, "AirBrakePressure_kPa", "气制动压力", "kPa", "油装"},
    {0x18FF221E, 12, "UreaLevel_pct", "尿素液位百分比", "%", "油装"},

    {0x18FF231E, 0, "AvgFuelConsumption_Lph", "平均油耗", "L/h", "油装"},
    {0x18FF231E, 1, "VehicleBatteryVoltage_V", "车辆电池电压", "V", "油装"},
    {0x18FF231E, 2, "EngineFaultLamp", "发动机故障灯", "", "油装"},

    {0x18FF2CE4, 0, "iip_LeftUltrasonicDist_m", "左侧超声波距离", "m", "油装"},
    {0x18FF2CE4, 1, "iip_LeftUltrasonicFault", "左侧超声波故障", "", "油装"},
    {0x18FF2CE4, 2, "iip_RightUltrasonicDist_m", "右侧超声波距离", "m", "油装"},
    {0x18FF2CE4, 3, "iip_RightUltrasonicFault", "右侧超声波故障", "", "油装"},
    {0x18FF2CE4, 4, "iip_RearLeftMidUltrasonicDist_m", "左后中超声波距离", "m", "油装"},
    {0x18FF2CE4, 5, "iip_RearLeftMidUltrasonicFault", "左后中超声波故障", "", "油装"},
    {0x18FF2CE4, 6, "iip_RearLeftOuterUltrasonicDist_m", "左后外超声波距离", "m", "油装"},
    {0x18FF2CE4, 7, "iip_RearLeftOuterUltrasonicFault", "左后外超声波故障", "", "油装"},
    {0x18FF2CE4, 8, "iip_RearRightOuterUltrasonicDist_m", "右后外超声波距离", "m", "油装"},
    {0x18FF2CE4, 9, "iip_RearRightOuterUltrasonicFault", "右后外超声波故障", "", "油装"},
    {0x18FF2CE4, 10, "iip_RearRightMidUltrasonicDist_m", "右后中超声波距离", "m", "油装"},
    {0x18FF2CE4, 11, "iip_RearRightMidUltrasonicFault", "右后中超声波故障", "", "油装"},
    {0x18FF2CE4, 12, "iip_LeftFrontRadarDist_m", "左前雷达距离", "m", "油装"},
    {0x18FF2CE4, 13, "iip_RightFrontRadarDist_m", "右前雷达距离", "m", "油装"},

    {0x18FEE51E, 0, "MachineWorkHours_hr", "机器工作小时数", "hr", "油装"},
    {0x18FF2D1E, 0, "RemoteWorkHours_hr", "远程工作小时数", "hr", "油装"},
    {0x18FF2D1E, 1, "TotalPayload_t", "总装载量", "t", "油装"},
    {0x18FEEF00, 0, "EngineOilPressure_kPa", "发动机机油压力", "kPa", "油装"},

    {0x18FF15E4, 0, "HydraulicMotorSpeed_rpm", "液压电机转速", "rpm", "电装"},
    {0x18FF15E4, 1, "PowerBatterySOC_pct", "动力电池SOC百分比", "%", "电装"},
    {0x18FF15E4, 2, "VehicleSpeed_kmh", "车速", "km/h", "电装"},
    {0x18FF15E4, 3, "TravelMotorTemp_degC", "行走电机温度", "degC", "电装"},
    {0x18FF15E4, 4, "HydraulicMotorTemp_degC", "液压电机温度", "degC", "电装"},

    {0x18FF30E4, 0, "FNRState", "前进空倒挡状态", "", "电装"},
    {0x18FF30E4, 1, "GearDisplay", "档位显示", "", "电装"},
    {0x18FF30E4, 2, "RemoteLinkState", "远程连接状态", "", "电装"},
    {0x18FF30E4, 3, "SymbolPairState", "符号对状态", "", "电装"},
    {0x18FF30E4, 4, "ControlledVehicleNo", "受控车辆号", "", "电装"},
    {0x18FF30E4, 5, "RemoteIconState", "远程图标状态", "", "电装"},
    {0x18FF30E4, 6, "WorkModeDisplay", "工作模式显示", "", "电装"},
    {0x18FF30E4, 7, "WorkModeFlashState", "工作模式闪烁状态", "", "电装"},
    {0x18FF30E4, 8, "ParkingBrakeIcon", "驻车制动图标", "", "电装"},
    {0x18FF30E4, 9, "EStopIconState", "紧急停止图标状态", "", "电装"},
    {0x18FF30E4, 10, "HighVoltageReady", "高压就绪", "", "电装"},
    {0x18FF30E4, 11, "WorkLightState", "工作灯状态", "", "电装"},
    {0x18FF30E4, 12, "TurnSignalState", "转向信号状态", "", "电装"},
    {0x18FF30E4, 13, "BrakePressure_MPa", "制动压力", "MPa", "电装"},
    {0x18FF30E4, 14, "SingleBucketWeight_kg", "单斗重量", "kg", "电装"},

    {0x18FF20E4, 0, "BoomAngle_deg_raw", "动臂角度原始值", "deg", "电装"},
    {0x18FF20E4, 1, "BucketAngle_deg_raw", "铲斗角度原始值", "deg", "电装"},
    {0x18FF20E4, 2, "SteeringAngle_deg", "转向角度", "deg", "电装"},
    {0x18FF20E4, 3, "BrakeLowPressureAlarm", "制动低压报警", "", "电装"},
    {0x18FF20E4, 4, "HighLowBeamState", "远近光灯状态", "", "电装"},
    {0x18FF20E4, 5, "Start24VIndicator", "24V启动指示", "", "混动"},
    {0x18FF20E4, 6, "AutoStartStopIndicator", "自动启停指示", "", "混动"},
    {0x18FF20E4, 7, "Byte7Bit2_Indicator", "字节7位2指示", "", "混动"},
    {0x18FF20E4, 8, "Byte7Bit3_Indicator", "字节7位3指示", "", "混动"},

    {0x18FF22E4, 0, "OperationMode", "操作模式", "", "电装"},
    {0x18FF22E4, 1, "RemoteSignalOK", "远程信号正常", "", "电装"},
    {0x18FF22E4, 2, "ControllerHeartbeatOK", "控制器心跳正常", "", "电装"},
    {0x18FF22E4, 3, "ManualModeReady", "手动模式就绪", "", "电装"},
    {0x18FF22E4, 4, "FNRAtN", "前进空倒挡处于空挡", "", "电装"},
    {0x18FF22E4, 5, "ControllerModeConsistent", "控制器模式一致", "", "电装"},
    {0x18FF22E4, 6, "BrakeSystemOK", "制动系统正常", "", "电装"},
    {0x18FF22E4, 7, "RemoteEStopNotPresent", "远程紧急停止不存在", "", "电装"},
    {0x18FF22E4, 8, "ShiftLeverAtN1", "换挡杆处于空挡1", "", "电装"},
    {0x18FF22E4, 9, "ParkingSwitchPulled", "驻车开关已拉起", "", "电装"},
    {0x18FF22E4, 10, "FaultCodeRaw24", "故障码原始值（24位）", "", "电装"},
    {0x18FF22E4, 11, "TravelMotorSpeed_rpm", "行走电机转速", "rpm", "电装"},

    {0x18FF23E4, 0, "PowerBatteryTemp_degC", "动力电池温度", "degC", "电装"},
    {0x18FF23E4, 1, "VehicleBatteryVoltage_V", "车辆电池电压", "V", "电装"},

    {0x18FF21E4, 0, "PitchAngle_deg", "俯仰角", "deg", "电装"},
    {0x18FF21E4, 1, "RollAngle_deg", "翻滚角", "deg", "电装"},
    {0x18FF21E4, 2, "HighVoltageValue_V", "高压值", "V", "电装"},

    {0x18FEE5E4, 0, "MachineWorkHours_hr", "机器工作小时数", "hr", "电装"},
    {0x18FF2DE4, 0, "RemoteWorkHours_hr", "远程工作小时数", "hr", "电装"},
    {0x18FF2DE4, 1, "TotalPayload_t", "总装载量", "t", "电装"},

    {0x18050531, 0, "AngleRoll_deg", "翻滚角", "deg", "IMU"},
    {0x18050531, 1, "AnglePitch_deg", "俯仰角", "deg", "IMU"},
    {0x18050531, 2, "AngleYaw_deg", "偏航角", "deg", "IMU"},
    {0x18050531, 3, "Reserved_Angle", "保留角度", "", "IMU"},

    {0x18050631, 0, "AccelX_g", "X轴加速度", "g", "IMU"},
    {0x18050631, 1, "AccelY_g", "Y轴加速度", "g", "IMU"},
    {0x18050631, 2, "AccelZ_g", "Z轴加速度", "g", "IMU"},
    {0x18050631, 3, "Temp_cel", "温度", "degC", "IMU"},

    {0x18050731, 0, "AngRateX_dps", "X轴角速率", "deg/s", "IMU"},
    {0x18050731, 1, "AngRateY_dps", "Y轴角速率", "deg/s", "IMU"},
    {0x18050731, 2, "AngRateZ_dps", "Z轴角速率", "deg/s", "IMU"},
    {0x18050731, 3, "Reserved_AngRate", "保留角速率", "", "IMU"},

    {0x60B, 0, "LeftUltrasonicDist_m", "左侧雷达距离", "m", "雷达"},
    {0x61B, 0, "RightUltrasonicDist_m", "右侧雷达距离", "m", "雷达"},
    {0x62B, 0, "RearLeftOuterUltrasonicDist_m", "左后外雷达距离", "m", "雷达"},
    {0x63B, 0, "RearLeftMidUltrasonicDist_m", "左后中雷达距离", "m", "雷达"},
    {0x64B, 0, "RearRightMidUltrasonicDist_m", "右后中雷达距离", "m", "雷达"},
    {0x65B, 0, "RearRightOuterUltrasonicDist_m", "右后外雷达距离", "m", "雷达"},
    {0x66B, 0, "LeftFrontRadarDist_m", "左前雷达距离", "m", "雷达"},
    {0x67B, 0, "RightFrontRadarDist_m", "右前雷达距离", "m", "雷达"},
}};

inline const CanDecodedSignalDef *findCanDecodedSignalDef(std::uint32_t canId, int dataIndex)
{
    for (const auto &def : kCanDecodedSignalDefs) {
        if (def.canId == canId && def.dataIndex == dataIndex) {
            return &def;
        }
    }
    return nullptr;
}
