function plotAcquisition(acqResults)
figure(102)
bar(acqResults.peakMetric); 
hold on
a=acqResults.peakMetric .* (acqResults.carrFreq > 0);
bar(a,'g');
legend('未被捕获的信号', '被捕获信号');
title("捕获结果");
xlabel('PRN编号');
ylabel('相关第一峰值和第二峰值之比');