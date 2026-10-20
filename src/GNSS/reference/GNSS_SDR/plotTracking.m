function plotTracking(channelList, trackResults, settings)

% 设置两个数组的交集，将channelList与所有卫星编号相交，
% 得到准确的已经正常跟踪的卫星列表，然后再对其逐一绘图，使用intersect函数
channelList=intersect(channelList,1:settings.numberOfChannels);

% 有多个通道追踪结果，设置结果图编号，设置横坐标参数（timeAxisInSeconds）
% 从1到settings.msToProcess
timeAxisInSeconds=(1:settings.msToProcess)/1000; %以秒为单位
for channelNr=channelList %注意：channelList已经是一个1*8的矩阵了，不需要1：channelList
    figure(200+channelNr)
% 离散时间散点图，即：绘制I_P支路和Q_P支路的散点分布，设置横纵坐标和标题    
    subplot(3,3,1)
    plot(trackResults(channelNr).I_P,trackResults(channelNr).Q_P,'.')
    axis  ('equal');
    %需注意！
    xlabel( 'I prompt');
    ylabel( 'Q prompt'); 
% 绘制bit数据图，设置标题名称“导航数据信息”    
    subplot(3,3, [2 3])
    % 该写法可以占据两个图位
    plot(timeAxisInSeconds,trackResults(channelNr).I_P)
    axis  ( 'tight');
% 绘制载波输入信号与复制信号的相位差异（pllDiscr）    
    subplot(3,3,4)
    plot(timeAxisInSeconds,trackResults(channelNr).pllDiscr)
    title('载波输入信号与复制信号的相位差异')
    xlabel( '时间/s');
    ylabel( '幅值(rad)');     
% 绘制相关结果图，根据公式（12-8）的形式，求出相关幅值，
% 放到同一张图中，使用legend函数，给结果添加图例    
    subplot(3,3,[5 6])
    plot(timeAxisInSeconds,sqrt(trackResults(channelNr).Q_P.^2+trackResults(channelNr).I_P.^2),'r')
    
    hold on
    plot(timeAxisInSeconds,sqrt(trackResults(channelNr).Q_E.^2+trackResults(channelNr).I_E.^2),'b')
    
    hold on
    plot(timeAxisInSeconds,sqrt(trackResults(channelNr).Q_L.^2+trackResults(channelNr).I_L.^2),'y')
    
    title('相关结果图')
    xlabel( '时间/s');
    ylabel( '幅值');  
    
    legend('超前相关结果',  '即时相关结果', '迟滞相关结果');

% 绘制载波环压控振荡NCO偏移量
    subplot(3,3,7)
    plot(timeAxisInSeconds,trackResults(channelNr).pllDiscrFilt)
    title('载波环压控振荡NCO偏移量')
    xlabel( '时间/s');
    ylabel( '幅值');      
% 绘制码环输入信号与复制信号的相位差异（dllDiscr）和码NCO偏移量（dllDiscrFilt）
    subplot(3,3,8)
    plot(timeAxisInSeconds,trackResults(channelNr).dllDiscr)
    title('码环输入信号与复制信号的相位差异')
    xlabel( '时间/s');
    ylabel( '幅值(码片)');  
    subplot(3,3,9)
    plot(timeAxisInSeconds,trackResults(channelNr).dllDiscrFilt)
    title('码NCO偏移量')
    xlabel( '时间/s');
    ylabel( '幅值');  
end