%1、传入参量settings，计算一个C/A码周期的采样点数samplesPerCode，初始化caCodesTable矩阵
function caCodesTable=makeCaTable(settings)
samplesPerCode = round(settings.samplingFreq / (settings.codeFreqBasis / settings.codeLength));
caCodesTable=zeros(32,samplesPerCode);
% 2、计算采样时间ts和码片周期tc
ts = 1/settings.samplingFreq;
tc = 1/settings.codeFreqBasis;

% 分别产生各自PRN的码序列（1023个长度）caCode
for PRN=1:32
    caCode = generateCAcode(PRN);
% 4、产生samplesPerCode个与C/A码率相对应的codeValueIndex（数值从1到1023，共samplesPerCode个数据），调整codeValueIndex最后的数值为1023
    codeValueIndex = ceil((ts * (1:samplesPerCode)) / tc);
    codeValueIndex(end) = 1023;
% 5、将codeValueIndex的值对应于caCode，最终产生从1到1023对应的C/A码值（1或-1），并存入caCodesTable中
    caCodesTable(PRN, :) = caCode(codeValueIndex);
end