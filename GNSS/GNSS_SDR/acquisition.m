function acqResults = acquisition(longSignal, settings)
% 合并噪声处理和捕获算法的主函数
% 输入参数：
%   longSignal - 原始中频信号（11ms）
%   settings   - 包含系统参数的结构体（需新增SNRdB/useDenoise字段）
% 输出：
%   acqResults - 捕获结果（含噪声抑制指标）

%% ========================= 初始化设置 ===============================
settings = initSettings();
settings.SNRdB = -10;       % 设置信噪比
settings.useDenoise = 1;  % 启用去噪1/0

%% ========================= 15次噪声干扰模块 ===============================
numNoises = 30; % 噪声次数
noisySignals = cell(1, numNoises); % 存储带噪信号
noisePower = 0;

% 计算噪声功率
signalPower = mean(longSignal.^2);
noisePower = signalPower / (10^(settings.SNRdB/10));
if isfield(settings, 'SNRdB') 
    % 生成15次带噪信号
    for i = 1:numNoises
        noise = sqrt(noisePower) * randn(size(longSignal));
        noisySignals{i} = longSignal + noise;
    end
else
    % 无噪声模式
    for i = 1:numNoises
        noisySignals{i} = longSignal;
    end
end

%% ========================= 去噪预处理模块 =============================
processedSignals = cell(1, numNoises); % 存储去噪后的信号
denoiseTime = 0;
tic;
if isfield(settings, 'useDenoise') && settings.useDenoise==1
    
    if settings.SNRdB <= -20
        % 使用带通滤波
        for i = 1:numNoises
            processedSignals{i} = bandpass_filter(noisySignals{i}, settings);
        end
    else
        % 使用小波变换或深度学习
        for i = 1:numNoises
            threshold=2.6;%根据信噪比设置，例如-10dB设置2.6，-15dB设置9.5
            processedSignals{i} = wavelet_denoise(noisySignals{i},threshold);
            % 或者使用深度学习
            % load('denoising_model.mat');
            % processedSignals{i} = deep_learning_denoise(noisySignals{i}, denoising_model);
        end
    end
    
    fprintf('去噪完成，耗时 %.3f 秒\n', denoiseTime);
else
    % 不去噪
    processedSignals = noisySignals;
end

% 提取相同部分信号以提高信噪比
if settings.useDenoise
combinedSignal = zeros(size(longSignal));
for i = 1:numNoises
    combinedSignal = combinedSignal + processedSignals{i};
end
combinedSignal = combinedSignal / numNoises;
else
    combinedSignal=noisySignals{1};
end
denoiseTime = toc;

if settings.useDenoise ==3
combinedSignal = longSignal;
end

%% ====================== 原捕获算法核心部分 ============================
tic;
samplesPerCode = round(settings.samplingFreq / ...
    (settings.codeFreqBasis / settings.codeLength));

signal1 = combinedSignal(1 : samplesPerCode);
signal2 = combinedSignal(samplesPerCode + 1 : 2 * samplesPerCode);
signal0DC = combinedSignal - mean(combinedSignal);

ts = 1 / settings.samplingFreq;
phasePoints = (0 : samplesPerCode - 1) * 2 * pi * ts;
numberOfFrqBins = floor(settings.acqSearchBand) * 2 + 1;

caCodesTable = makeCaTable(settings);

results = zeros(numberOfFrqBins, samplesPerCode);
frqBins = zeros(1, numberOfFrqBins);

acqResults.carrFreq = zeros(1, length(settings.acqSatelliteList));
acqResults.codePhase = zeros(1, length(settings.acqSatelliteList));
acqResults.peakMetric = zeros(1, length(settings.acqSatelliteList));

fprintf('(');
for PRN = settings.acqSatelliteList
    caCodeFreqDom = conj(fft(caCodesTable(PRN, :)));
    
    for frqBinIndex = 1 : numberOfFrqBins
        frqBins(frqBinIndex) = settings.IF - ...
            (settings.acqSearchBand / 2) * 1000 + ...
            0.5e3 * (frqBinIndex - 1);
        
        sinCarr = sin(frqBins(frqBinIndex) * phasePoints);
        cosCarr = cos(frqBins(frqBinIndex) * phasePoints);
        
        I1 = sinCarr .* signal1;
        Q1 = cosCarr .* signal1;
        I2 = sinCarr .* signal2;
        Q2 = cosCarr .* signal2;
        
        IQfreqDom1 = fft(I1 + 1j * Q1);
        IQfreqDom2 = fft(I2 + 1j * Q2);
        
        convCodeIQ1 = IQfreqDom1 .* caCodeFreqDom;
        convCodeIQ2 = IQfreqDom2 .* caCodeFreqDom;
        
        acqRes1 = abs(ifft(convCodeIQ1)) .^ 2;
        acqRes2 = abs(ifft(convCodeIQ2)) .^ 2;
        
        [~, maxIdx] = max([max(acqRes1), max(acqRes2)]);
        if maxIdx == 1
            results(frqBinIndex, :) = acqRes1;
        else
            results(frqBinIndex, :) = acqRes2;
        end
    end
    
    [peakSize, frequencyBinIndex] = max(max(results, [], 2));
    [peakSize, codePhase] = max(max(results));
    
    samplesPerCodeChip = round(settings.samplingFreq / settings.codeFreqBasis);
    excludeRangeIndex1 = codePhase - samplesPerCodeChip;
    excludeRangeIndex2 = codePhase + samplesPerCodeChip;
    
    if excludeRangeIndex1 < 2
        codePhaseRange = excludeRangeIndex2 : ...
            (samplesPerCode + excludeRangeIndex1);
    elseif excludeRangeIndex2 >= samplesPerCode
        codePhaseRange = (excludeRangeIndex2 - samplesPerCode) : ...
            excludeRangeIndex1;
    else
        codePhaseRange = [1 : excludeRangeIndex1, ...
            excludeRangeIndex2 : samplesPerCode];
    end
    
    secondPeakSize = max(max(results(frequencyBinIndex, codePhaseRange)));
    acqResults.peakMetric(PRN) = peakSize / secondPeakSize;
    
    if acqResults.peakMetric(PRN) > settings.acqThreshold
        fprintf('%02d ', PRN);
        caCode = generateCAcode(PRN);
        codeValueIndex = floor((ts * (1 : 10 * samplesPerCode)) / ...
            (1 / settings.codeFreqBasis));
        longCaCode = caCode(rem(codeValueIndex, settings.codeLength) + 1);
        
        xCarrier = longCaCode .* signal0DC(codePhase : ...
            (codePhase + 10 * samplesPerCode - 1));
        
        fftNumPts = 8 * (2 ^ nextpow2(length(xCarrier)));
        fftxc = abs(fft(xCarrier, fftNumPts));
        uniqFftPts = ceil((fftNumPts + 1) / 2);
        [fftMax, fftMaxIndex] = max(fftxc(5 : uniqFftPts - 5));
        fftFreqBins = (0 : uniqFftPts - 1) * settings.samplingFreq / fftNumPts;
        
        acqResults.carrFreq(PRN) = fftFreqBins(fftMaxIndex + 4); % 补偿索引偏移
        acqResults.codePhase(PRN) = codePhase;
    else
        fprintf('. ');
    end
end
fprintf(')\n');

%% ===================== 性能评估与输出 ========================
captureTime = toc;
fprintf('捕获总耗时: %.3f 秒 (含去噪 %.3f 秒)\n', captureTime, denoiseTime);

% 计算信噪比改善
if isfield(settings, 'SNRdB') && ...
        isfield(settings, 'useDenoise') 
    residualNoise = combinedSignal - longSignal;
    residualNoisePower = mean(residualNoise.^2);
    originalSNR = 10 * log10(signalPower / noisePower);
    processedSNR = 10 * log10(signalPower / residualNoisePower);
    fprintf('SNR改善: %.2f dB -> %.2f dB (提升 %.2f dB)\n', ...
        originalSNR, processedSNR, processedSNR - originalSNR);
end

% 统计捕获成功率
validPRNs = find(acqResults.peakMetric > settings.acqThreshold);
fprintf('成功捕获卫星: %d/%d\n', numel(validPRNs), ...
    length(settings.acqSatelliteList));

end

%% ===================== 局部函数：带通滤波器 ======================
function filtered = bandpass_filter(signal, settings)
fs = settings.samplingFreq;
f_center = settings.IF;
bandwidth = settings.acqSearchBand * 1e3 + 2 * 1.023e6; % 多普勒展宽+码片带宽

% 设计最小阶FIR滤波器
bpFilt = designfilt('bandpassfir', ...
    'StopbandFrequency1', max(0, f_center - bandwidth/2 - 0.5e6), ...
    'PassbandFrequency1', max(0, f_center - bandwidth/2), ...
    'PassbandFrequency2', f_center + bandwidth/2, ...
    'StopbandFrequency2', f_center + bandwidth/2 + 0.5e6, ...
    'StopbandAttenuation1', 60, ...
    'PassbandRipple', 0.5, ...
    'StopbandAttenuation2', 60, ...
    'SampleRate', fs, ...
    'DesignMethod', 'equiripple');

% 零相位滤波
filtered = filtfilt(bpFilt, signal);
end

%% ===================== 局部函数：小波变换去噪 ======================
function filtered = wavelet_denoise(signal, threshold)
    wavelet = 'db4'; % Daubechies小波
    level = 5; % 分解层数
    [c, l] = wavedec(signal, level, wavelet); % 小波分解
    % 软阈值去噪
    C_thresholded = wthresh(c, 's', threshold);% 阈值处理
    filtered = waverec(C_thresholded, l, wavelet); % 重构信号
end


%% ===================== 局部函数：CA码表生成 ======================
function caCodesTable = makeCaTable(settings)
samplesPerCode = round(settings.samplingFreq / ...
    (settings.codeFreqBasis / settings.codeLength));
caCodesTable = zeros(32, samplesPerCode);

for PRN = 1:32
    caCode = generateCAcode(PRN);
    codeValueIndex = floor((1/settings.samplingFreq * (0:samplesPerCode-1)) / ...
        (1/settings.codeFreqBasis));
    caCodesTable(PRN, :) = caCode(rem(codeValueIndex, 1023) + 1);
end
end