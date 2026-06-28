% 定义信噪比dB值
snr_dB = [-25, -20, -15, -10, -5];

% 定义改进算法和传统算法的捕获概率（百分比）
improved_algorithm = [66, 76, 89, 93, 98];
traditional_algorithm = [23, 28, 46, 72, 96];

% 创建图形
figure('Position', [100, 100, 800, 600]);

% 绘制改进算法和传统算法的性能曲线
plot(snr_dB, improved_algorithm, 'LineWidth', 2, 'Color', [0 0.4470 0.7410], 'Marker', 'o', 'MarkerSize', 8, 'MarkerFaceColor', [0 0.4470 0.7410]);
hold on;
plot(snr_dB, traditional_algorithm, 'LineWidth', 2, 'Color', [0.8500 0.3250 0.0980], 'Marker', 's', 'MarkerSize', 8, 'MarkerFaceColor', [0.8500 0.3250 0.0980]);

% 添加标题和轴标签
title('不同信噪比下捕获算法概率对比', 'FontSize', 14);
xlabel('信噪比 (dB)', 'FontSize', 12);
ylabel('捕获概率 (%)', 'FontSize', 12);

% 添加网格线
grid on;

% 设置x轴和y轴的范围
xlim([-30 0]);
ylim([-30 100]);

% 添加图例
legend('改进的时域并行捕获算法', '传统的时域并行捕获算法', 'Location', 'southeast');

% 添加网格线
grid on;
grid minor;

% 设置刻度间隔
set(gca, 'XTick', -30:5:0);
set(gca, 'YTick', 0:10:100);

% 为每个数据点添加标签
for i = 1:length(snr_dB)
    text(snr_dB(i)-0.5, improved_algorithm(i)+3, [num2str(improved_algorithm(i)), '%'], 'FontSize', 9);
    text(snr_dB(i)-0.5, traditional_algorithm(i)-5, [num2str(traditional_algorithm(i)), '%'], 'FontSize', 9);
end