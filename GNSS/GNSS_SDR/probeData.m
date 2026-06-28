% 该函数主要任务：绘制原始数据信息:时域图，频域图和直方图。
function probeData(varargin)

% 判断传入参数个数 
if nargin==1
    settings=varargin{1};
    fileNameStr=settings.fileName;
elseif nargin==2
    fileNameStr=varargin{1};
    settings=varargin{2};
else
    error("函数参数不正确")
end

fid=fopen(fileNameStr,'rb');
% 设置读数据格式
if fid>0
    fseek(fid,settings.skipNumberOfBytes,-1);
    %计算一个码周期样本数
    samplesPerCode=settings.samplingFreq/...
        (settings.codeFreqBasis/settings.codeLength);
    [data,count]=fread(fid,[1,10*samplesPerCode],settings.dataType);
    fclose(fid);
    if count<10*samplesPerCode
        error("数据文件过小，没有足够的数据")
    end
    
    %绘图
    T=0:1/settings.samplingFreq:1e-3;
    figure(101)
    subplot(2,2,1)
    plot(T(1:round(samplesPerCode/50)),data(1:round(samplesPerCode/50)))
    %每50个点采样一次
    grid on;
    title ('时域分布情况');
    xlabel('时间/微秒'); 
    ylabel('幅值');
    
    subplot(2,2,2)
    pwelch(data-mean(data),16384, 1024, 2048, settings.samplingFreq/1e6)
    %此处未弄清楚参数为何这样设置？
    
    axis tight;
    grid on;
    title ('频谱分布情况');
    xlabel('频率(MHz)'); 
    ylabel('幅值');
    
    subplot(2,2,3)
    hist(data)      %此处需要设置间距，间距设置需要合理，否则会出现图三的情况
    axis tight;
    xmax = max(abs(data)) + 1;
    %xlim([-xmax xmax]);
    adata = axis;
    axis([-xmax xmax adata(3) adata(4)]);
    title ('Histogram'); 
    xlabel('Bin'); ylabel('Number in bin');
    
    subplot(2, 2, 4);
    hist(data,-10:10)
    dmax = max(abs(data)) + 1;
    axis tight;
    adata = axis;
    axis([-dmax dmax adata(3) adata(4)]);
    grid on;
    title ('Histogram'); 
    xlabel('Bin'); ylabel('Number in bin');
else
    error("函数参数数量不正确");
end