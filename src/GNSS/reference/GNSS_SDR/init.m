% GNSS软件接收机程序编写
%——————————————————————

%clear all
clc

%% 将命令行窗口中的输出显示格式更改为指定的格式
format compact
format long

%% 添加MATLAB路径
addpath C:\Users\111\Desktop\wsj\GNSS_SDR\include
addpath C:\Users\111\Desktop\wsj\GNSS_SDR\geoFunctions

%% 在命令行显示启动（欢迎）语句
disp("欢迎使用GNSS软件接收机")
disp("程序由lcx编写")
%% 初始化参数
settings=initSettings();

%% 处理数据

try
    disp("数据处理中，请稍后……")
    probeData(settings);
catch
    disp("文件处理异常")
    rethrow(ME.identifier)
end

%% 显示处理结果
disp("“原始中频数据如图所示”")
disp("“如需修改参数，请在'initSettings.m'中配置”)")

%% 确认是否运行
startchar=input('输入“1”开始数据处理，输入其他键退出程序：');

if startchar==1
    postProcessing
end
