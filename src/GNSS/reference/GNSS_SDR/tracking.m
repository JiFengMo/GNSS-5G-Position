function [trackResults,channel]=tracking(fid,channel,settings)
tic
%% 对跟踪结果结构体trackResults的初始化，包括初始化信道状态（status）、
% 数据文件的当前位置指针（absoluteSample）、C/A码的频率（codeFreq）、
% 跟踪结果的当前载波频率（carrFreq）、相关器I通道的输出（I_P、I_E、I_L）、
% 相关器Q通道的输出、码环输入信号与复制信号的相位差异（dllDiscr）、
% 码NCO偏移量（dllDiscrFilt）、载波输入信号与复制信号的相位差异（pllDiscr）、
% 载波NCO偏移量（pllDiscrFilt），并根据捕获卫星的数量，将初始化参数赋给所有信道。
trackResults.status='-';
trackResults.absoluteSample=zeros(1,settings.msToProcess);
trackResults.codeFreq=inf(1, settings.msToProcess);
% 注意此处
trackResults.carrFreq=inf(1,settings.msToProcess);
trackResults.I_P=zeros(1,settings.msToProcess);
trackResults.I_E=zeros(1,settings.msToProcess);
trackResults.I_L=zeros(1,settings.msToProcess);
trackResults.Q_P=zeros(1,settings.msToProcess);
trackResults.Q_E=zeros(1,settings.msToProcess);
trackResults.Q_L=zeros(1,settings.msToProcess);
trackResults.dllDiscr=inf(1,settings.msToProcess);
trackResults.dllDiscrFilt=inf(1,settings.msToProcess);
trackResults.pllDiscr=inf(1,settings.msToProcess);
trackResults.pllDiscrFilt=inf(1,settings.msToProcess);
% ※需要将初始化参数赋给所有被捕获的卫星※
trackResults = repmat(trackResults, 1, settings.numberOfChannels);

%% 初始化过程参数：确定追踪的循环数为codePeriods=37000，
% 确定码相位间距earlyLateSpc，确定求和间隔PDIcode、PDIcarr为0.001，
% 计算滤波系数值[tau1code, tau2code]和[tau1carr, tau2carr]，
% 此处需要调用calcLoopCoef函数，启动GUI显示处理进度条waitbar。
codePeriods=settings.msToProcess;
earlyLateSpc=settings.dllCorrelatorSpacing;
% 注意此处
PDIcode=0.001;
PDIcarr=0.001;
[tau1code, tau2code]=calcLoopCoef(settings.dllNoiseBandwidth,settings.dllDampingRatio,1);
% 传入参数分别为：噪声带宽、阻尼系数、环路增益
[tau1carr, tau2carr]=calcLoopCoef(settings.pllNoiseBandwidth,settings.pllDampingRatio,0.25)
hwb = waitbar(0,'Tracking...');
% help waitbar: The handle to the waitbar figure is returned in H.

%% 对捕获的通道逐一进行处理（channelNr），首先判断该编号是否是被捕获的卫星编号，
% 如果是，将卫星的PRN赋给trackResults（channelNr）.PRN，将文件读取的指针位置，
% 移动到和捕获时码相位相关程度最高的码片位置（channel(channelNr).codePhase-1）处。
for channelNr = 1 : settings.numberOfChannels
    if channel(channelNr).PRN ~= 0
        trackResults(channelNr).PRN = channel(channelNr).PRN;
        fseek(fid,settings.skipNumberOfBytes + channel(channelNr).codePhase-1,'bof');% 注意使用fseek
        
% 使用generateCAcode函数产生C/A码caCode，同时，在C/A码的前后各加一个码。
        caCode=generateCAcode(channel(channelNr).PRN);
        caCode=[caCode(1023) caCode caCode(1)];
        
% 初始化基于NCO的C/A码率codeFreq，初始化码相位偏差remCodePhase，
% 定义捕获时的载波频率carrFreq和carrFreqBasis，初始化载波相位差（remCarrPhase），
% 码跟踪环参数设置（oldCodeNco，oldCodeError）、载波跟踪环参数设置（oldCarrNco、oldCarrError）
        codeFreq=settings.codeFreqBasis;
        remCodePhase=0.0;
        carrFreq=channel(channelNr).acquiredFreq;
        carrFreqBasis=carrFreq;
        oldCodeNco=0.0;
        oldCodeError=0.0;
        oldCarrNco=0.0;
        oldCarrError=0.0;
        remCarrPhase=0.0;
%% —————————————初始化完毕——————————————
% 逐个处理每个环路更新周期loopCnt，并更新进度条，
% 显示处理信息的进度（包含正在处理卫星的编号，完成度等信息）
        for loopCnt=1:settings.msToProcess
            if rem(loopCnt,50)==0
                waitbar(loopCnt/codePeriods,hwb,['正在第', int2str(channelNr), ...
                    '颗卫星 共需处理', int2str(settings.numberOfChannels), ...
                    '颗星，', int2str(channel(channelNr).PRN), ...
                    '号卫星已处理 ',int2str(100*loopCnt/codePeriods), ...
                    ' % ']);
            end
            
% 计算每个环路更新周期的采样步长codePhaseStep（NCO产生的C/A码率 / 采样频率），
%（码长-前一计算周期的码相位之差）/样本步长，向上取整，就是本周期的样本个数，并将值赋给blksize
            codePhaseStep=codeFreq/settings.samplingFreq;
            blksize=ceil((settings.codeLength-remCodePhase)/codePhaseStep);% 注意使用ceil函数
            
% 依次/不重复的读入适量的采样点（blksize）进行处理，处理结果给[rawSignal, samplesRead]，
% 为方便处理将rawSignal转置。添加一个判断语句（如果没有读取足够的样本，那么可能是数据不足-最好退出）
            [rawSignal, samplesRead]=fread(fid,blksize,settings.dataType);
            rawSignal=rawSignal';
            if samplesRead~=blksize
                disp("没有读取足够的样本，那么可能是数据不足-最好退出")
                break;
            end
            
% 分别生成的全部超前、延后、即时码的C/A码矩阵tcode，并将相位向上取整后+1得到tcode2，
% 然后调用caCode函数，产生earlyCode、lateCode和promptCode
            tcode=(remCodePhase-earlyLateSpc):codePhaseStep:...
                ((blksize-1)*codePhaseStep+remCodePhase-earlyLateSpc);
            tcode2=ceil(tcode)+1;
            earlyCode=caCode(tcode2);
            
            tcode=(remCodePhase+earlyLateSpc):codePhaseStep:...
                ((blksize-1)*codePhaseStep+remCodePhase+earlyLateSpc);
            tcode2=ceil(tcode)+1;
            lateCode=caCode(tcode2);
            
            tcode=(remCodePhase):codePhaseStep:...
                ((blksize-1)*codePhaseStep+remCodePhase);
            tcode2=ceil(tcode)+1;
            promptCode=caCode(tcode2);
            remCodePhase = (tcode(blksize) + codePhaseStep) - 1023.0;
            % 有必要写这一句话，意思是第blksize+1个与码相位与初始码相位之差
            
%% 下面通过载频将中频信号混频后到基带。首先将time划分为从0：blksize个步长
% 为采样周期的均匀时间；计算从（0：blksize）时刻时的载波相位trigarg (2*pi*f*t)，
% 并更新remCarrPhase，即第(blksize+1)个载波的相位值（除以2*pi的余数），
% 生成本地sin/cos载波carrCos和carrSin两个（1*blksize）的矩阵。
            time=(0:blksize)./settings.samplingFreq;
            trigarg=2*pi*carrFreq.*time+remCarrPhase;
            remCarrPhase=rem(trigarg(blksize+1),2*pi);
            carrCos=cos(trigarg(1:blksize));
            carrSin=sin(trigarg(1:blksize));
            
% 本部分：首先与载波混频，在跟C/A码求相关，最后积分-清除。
% 将载波与中频信号混频得到Q支路和I支路的信号：qBasebandSignal和iBasebandSignal，
% 然后与三组C/A码求相关，最后积分，求出I_E、Q_E、I_P、Q_P、I_L、Q_L之和
% 运用I/Q支路进行载波剥离的原理P258,为什么在进行载波分离时，不添加低通滤波器
            qBasebandSignal=rawSignal.*carrCos;
            iBasebandSignal=rawSignal.*carrSin;
            
            % 优化相关值计算，使用向量化操作
            I_E = sum(bsxfun(@times, earlyCode, iBasebandSignal));
            Q_E = sum(bsxfun(@times, earlyCode, qBasebandSignal));
            I_P = sum(bsxfun(@times, promptCode, iBasebandSignal));
            Q_P = sum(bsxfun(@times, promptCode, qBasebandSignal));
            I_L = sum(bsxfun(@times, lateCode, iBasebandSignal));
            Q_L = sum(bsxfun(@times, lateCode, qBasebandSignal));

% 进行载波环路鉴相(鉴相器)φ_e(t) = arctan( Q_P(t) / I_P(t) )，
% 得到载波相位差异carrError，接着将θ_e转化为θ_o，实现输出与输入相等，
% 根据tau2carr、tau1carr参数，计算需要调整的频率变化量carrNco，
% 将carrNco赋给oldCarrNco、将carrError赋给oldCarrError;更新载波频率carrFreq，
% 并将载波频率值保存在trackResults对应的carrFreq(loopCnt)中。
            carrError=atan( Q_P/ I_P )/(2*pi);
            carrNco=oldCarrNco + (tau2carr/tau1carr) * ...
                (carrError - oldCarrError) + carrError * (PDIcarr/tau1carr); % 原理未找到？
            oldCarrNco=carrNco;
            oldCarrError=carrError;
            carrFreq=carrFreqBasis+carrNco;
            % 需注意：载波频率是在carrFreqBasis上修改的！！
            trackResults(channelNr).carrFreq(loopCnt)=carrFreq;
            
% 根据P314中第一种方法定义的码环鉴别器，计算码相位误差codeError，
% 计算振荡器参数codeNco，分别将codeNco赋给oldCodeNco; codeError赋给oldCodeError;
% 修正码环频率codeFreq，并将码环频率值保存在trackResults对应的codeFreq(loopCnt)中。
            codeError = 0.5*(sqrt(I_E * I_E + Q_E * Q_E) - sqrt(I_L * I_L + Q_L * Q_L)) / ...
                (sqrt(I_E * I_E + Q_E * Q_E) + sqrt(I_L * I_L + Q_L * Q_L));
%             codeError = (max(abs(I_E)+0.5*abs(Q_E),abs(Q_E)+0.5*abs(I_E))-...
%                 max(abs(I_L)+0.5*abs(Q_L),abs(Q_L)+0.5*abs(I_L)))/...
%                 (max(abs(I_E)+0.5*abs(Q_E),abs(Q_E)+0.5*abs(I_E))+...
%                 max(abs(I_L)+0.5*abs(Q_L),abs(Q_L)+0.5*abs(I_L)));
            codeNco = oldCodeNco + (tau2code/tau1code) * ...
                (codeError - oldCodeError) + codeError * (PDIcode/tau1code);
            oldCodeNco   = codeNco;
            oldCodeError = codeError;
            codeFreq=settings.codeFreqBasis - codeNco;
            trackResults(channelNr).codeFreq(loopCnt)=codeFreq;
            % 此处注意
            
% 保存Tracking函数的追踪结果，存入trackResults结构体中，并close(hwb)。
            trackResults(channelNr).absoluteSample(loopCnt) = ftell(fid);

            trackResults(channelNr).dllDiscr(loopCnt)       = codeError;
            trackResults(channelNr).dllDiscrFilt(loopCnt)   = codeNco;
            trackResults(channelNr).pllDiscr(loopCnt)       = carrError;
            trackResults(channelNr).pllDiscrFilt(loopCnt)   = carrNco;

            trackResults(channelNr).I_E(loopCnt) = I_E;
            trackResults(channelNr).I_P(loopCnt) = I_P;
            trackResults(channelNr).I_L(loopCnt) = I_L;
            trackResults(channelNr).Q_E(loopCnt) = Q_E;
            trackResults(channelNr).Q_P(loopCnt) = Q_P;
            trackResults(channelNr).Q_L(loopCnt) = Q_L;
        end
        trackResults(channelNr).status=channel(channelNr).status;
    end
end
toc
disp(toc)
close(hwb);