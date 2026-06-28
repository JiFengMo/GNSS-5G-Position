function [navSolutions, eph] = postNavigation(trackResults, settings)
% Function calculates navigation solutions for the receiver (pseudoranges,
% positions). At the end it converts coordinates from the WGS84 system to
% the UTM, geocentric or any additional coordinate system.
% 该函数功能：计算接收机的导航解决方案(伪距，位置)。最后，它将WGS84系统的坐标转
% 换为UTM、地心或任何其他坐标系统。
%[navSolutions, eph] = postNavigation(trackResults, settings)
%
%   Inputs:
%       trackResults    - results from the tracking function (structure
%                       array).跟踪函数(结构数组)的结果
%       settings        - receiver settings.
%   Outputs:
%       navSolutions    - contains measured pseudoranges, receiver
%                       clock error, receiver coordinates in several
%                       coordinate systems (at least ECEF and UTM).
%                       包含测量的伪距，接收机时钟误差，多个坐标系中的接收机
%                       坐标(至少是ECEF和UTM)。
%       eph             - received ephemerides of all SV (structure array).
%                       接收到所有SV的星历表(结构数组)。
%--------------------------------------------------------------------------
%                           SoftGNSS v3.0
% 
% Copyright (C) Darius Plausinaitis
% Written by Darius Plausinaitis with help from Kristin Larson
%--------------------------------------------------------------------------
%This program is free software; you can redistribute it and/or
%modify it under the terms of the GNU General Public License
%as published by the Free Software Foundation; either version 2
%of the License, or (at your option) any later version.
%
%This program is distributed in the hope that it will be useful,
%but WITHOUT ANY WARRANTY; without even the implied warranty of
%MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
%GNU General Public License for more details.
%
%You should have received a copy of the GNU General Public License
%along with this program; if not, write to the Free Software
%Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
%USA.
%--------------------------------------------------------------------------

%CVS record:
%$Id: postNavigation.m,v 1.1.2.22 2006/08/09 17:20:11 dpl Exp $

%% Check is there enough data to obtain any navigation solution ===========
% 检查是否有足够的数据来获得导航解决方案
% It is necessary to have at least three subframes (number 1, 2 and 3) to
% find satellite coordinates. Then receiver position can be found too.
% The function requires all 5 subframes, because the tracking starts at
% arbitrary point. Therefore the first received subframes can be any three
% from the 5.
% 必须至少有三个子帧(编号1、2和3)才能找到卫星坐标。然后也可以找到接收器的位置。
% 因为跟踪开始于任意点，因此该函数需要所有5个子帧。
% 因此，第一个接收到的子帧可以是5中的任意3。
% One subframe length is 6 seconds, therefore we need at least 30 sec long
% record (5 * 6 = 30 sec = 30000ms). We add extra seconds for the cases,
% when tracking has started in a middle of a subframe.
% 一个子帧长度是6秒，因此我们需要至少30秒长的记录(5 * 6 = 30秒= 30000ms)。
% 当跟踪在一个子帧的中间开始时，我们为这种情况添加额外的秒数。

% 判断是否符合执行条件
if (settings.msToProcess < 36000) || (sum([trackResults.status] ~= '-') < 4)
    % Show the error message and exit
    disp('Record is to short or too few satellites tracked. Exiting!');
    navSolutions = [];
    eph          = [];
    return
end

%% Find preamble start positions ==========================================

[subFrameStart, activeChnList] = findPreambles(trackResults, settings);

% findPreambles函数在每个信道的比特流中查找遥测字与交接字。通过检查时间间隔(6秒)
% 和对子帧中的前两个字进行奇偶校验来验证前置符。同时函数返回处于跟踪状态的通道
% 列表，以及具有有效导航数据的导航数据流。

%% Decode ephemerides 解码星历表======================================

for channelNr = 1:numel(activeChnList)

    %=== Convert tracking output to navigation bits =======================
    % 将tracking函数的输出转换为导航数据

    %--- Copy 5 sub-frames long record from tracking output ---------------
    % 从tracking函数的输出中，复制5个子帧长的记录
    navBitsSamples = trackResults(channelNr).I_P(subFrameStart(channelNr) - 20 : ...
                               subFrameStart(channelNr) + (1500 * 20) -1)';

    %--- Group every 20 vales of bits into columns ------------------------
    % 每20ms作为一个值
    navBitsSamples = reshape(navBitsSamples, ...
                             20, (size(navBitsSamples, 1) / 20));

    %--- Sum all samples in the bits to get the best estimate -------------
    % 将每20ms的平均值作为一个替换值
    navBits = sum(navBitsSamples);

    %--- Now threshold and make 1 and 0 -----------------------------------
    % The expression (navBits > 0) returns an array with elements set to 1
    % if the condition is met and set to 0 if it is not met.
    % 表达式(navBits > 0)返回一个数组，如果满足条件则元素设置为1，如果不满足则设置为0。
    navBits = (navBits > 0);

    %--- Convert from decimal to binary -----------------------------------
    % The function ephemeris expects input in binary form. In Matlab it is
    % a string array containing only "0" and "1" characters.
    % 函数的星历表需要二进制形式的输入。在Matlab中，它是一个只包含“0”和“1”字符的字符串数组。
    navBitsBin = dec2bin(navBits);
    
    %=== Decode ephemerides and TOW of the first sub-frame ================
    % 解码星历表和第一子帧的转换字
    [eph(trackResults(channelNr).PRN), TOW] = ...
                            ephemeris(navBitsBin(2:1501)', navBitsBin(1));

    %--- Exclude satellite if it does not have the necessary nav data -----
    % 排除数据不充足的导航数据
    if (isempty(eph(trackResults(channelNr).PRN).IODC) || ...
        isempty(eph(trackResults(channelNr).PRN).IODE_sf2) || ...
        isempty(eph(trackResults(channelNr).PRN).IODE_sf3))

        %--- Exclude channel from the list (from further processing) ------
        activeChnList = setdiff(activeChnList, channelNr);
        %setdiff 两个数组的差集
    end    
end

%% Check if the number of satellites is still above 3 =====================
% 检测卫星的数量是否一直在3颗以上
if (isempty(activeChnList) || (size(activeChnList, 2) < 4))
    % Show error message and exit
    disp('Too few satellites with ephemeris data for postion calculations. Exiting!');
    navSolutions = [];
    eph          = [];
    return
end

%% Initialization 初始化 =============================================

% Set the satellite elevations array to INF to include all satellites for
% the first calculation of receiver position. There is no reference point
% to find the elevation angle as there is no receiver position estimate at
% this point.
% 将卫星仰角阵列设置为无穷大，以包括第一次计算接收机位置时的所有卫星。
% 没有参考点来找到仰角，因为没有在这一点上的接收机位置估计。
satElev  = inf(1, settings.numberOfChannels);

% Save the active channel list. The list contains satellites that are
% tracked and have the required ephemeris data. In the next step the list
% will depend on each satellite's elevation angle, which will change over
% time.  
% 保存有效频道的列表。该列表包含被跟踪的卫星，并具有所需的星历数据。
% 在接下来的步骤中，列表包含的卫星将取决于每颗卫星的仰角，它将随着时间的变化而变化。
readyChnList = activeChnList;

transmitTime = TOW;

%##########################################################################
%#   Do the satellite and receiver position calculations                  #
%#   开始计算卫星和接收器的位置                                            #
%##########################################################################

%% Initialization of current measurement ==================================
% 初始化当前
for currMeasNr = 1:fix((settings.msToProcess - max(subFrameStart)) / ...
                                                     settings.navSolPeriod)
% fix四舍五入 settings.navSolPeriod计算伪距和位置的周期

    % Exclude satellites, that are belove elevation mask 
    activeChnList=intersect(find(satElev>=settings.elevationMask),readyChnList,'legacy');

    % Save list of satellites used for position calculation
    navSolutions.channel.PRN(activeChnList, currMeasNr) = ...
                                        [trackResults(activeChnList).PRN]; 

    % These two lines help the skyPlot function. The satellites excluded
    % do to elevation mask will not "jump" to possition (0,0) in the sky
    % plot.
    % 这两条线有助于skyPlot函数。被排除的卫星将不会“跳”到天空图中的位置(0,0)。
    navSolutions.channel.el(:, currMeasNr) = ...
                                         NaN(settings.numberOfChannels, 1);
    navSolutions.channel.az(:, currMeasNr) = ...
                                         NaN(settings.numberOfChannels, 1);

%% Find pseudoranges ======================================================
% 计算伪距
    navSolutions.channel.rawP(:, currMeasNr) = calculatePseudoranges(...
            trackResults, ...
            subFrameStart + settings.navSolPeriod * (currMeasNr-1), ...
            activeChnList, settings);
% subFrameStart：某一通道有效导航数据的起始位置
%% Find satellites positions and clocks corrections =======================
% 找到卫星位置并完成时钟校正，调用satpos函数
    [satPositions, satClkCorr] = satpos(transmitTime, ...
                                        [trackResults(activeChnList).PRN], ...
                                        eph, settings);

%% Find receiver position =================================================
% 计算接收机位置
    % 3D receiver position can be found only if signals from more than 3
    % satellites are available  
    % 只有当3颗以上卫星的信号可用时，才能找到接收器的三维位置
    if size(activeChnList, 2) > 3  %获取矩阵activeChnList的列数

        %=== Calculate receiver position ==================================
        [xyzdt, ...
         navSolutions.channel.el(activeChnList, currMeasNr), ...
         navSolutions.channel.az(activeChnList, currMeasNr), ...
         navSolutions.DOP(:, currMeasNr)] = ...
            leastSquarePos(satPositions, ...
                           navSolutions.channel.rawP(activeChnList, currMeasNr)' + satClkCorr * settings.c, ...
                           settings);

        %--- Save results -------------------------------------------------
        navSolutions.X(currMeasNr)  = xyzdt(1);
        navSolutions.Y(currMeasNr)  = xyzdt(2);
        navSolutions.Z(currMeasNr)  = xyzdt(3);
        navSolutions.dt(currMeasNr) = xyzdt(4);

        % Update the satellites elevations vector
        satElev = navSolutions.channel.el(:, currMeasNr);

        %=== Correct pseudorange measurements for clocks errors ===========
        navSolutions.channel.correctedP(activeChnList, currMeasNr) = ...
                navSolutions.channel.rawP(activeChnList, currMeasNr) + ...
                satClkCorr' * settings.c + navSolutions.dt(currMeasNr);

%% Coordinate conversion ==================================================

        %=== Convert to geodetic coordinates ==============================
        [navSolutions.latitude(currMeasNr), ...
         navSolutions.longitude(currMeasNr), ...
         navSolutions.height(currMeasNr)] = cart2geo(...
                                            navSolutions.X(currMeasNr), ...
                                            navSolutions.Y(currMeasNr), ...
                                            navSolutions.Z(currMeasNr), ...
                                            5);

        %=== Convert to UTM coordinate system =============================
        navSolutions.utmZone = findUtmZone(navSolutions.latitude(currMeasNr), ...
                                           navSolutions.longitude(currMeasNr));
        
        [navSolutions.E(currMeasNr), ...
         navSolutions.N(currMeasNr), ...
         navSolutions.U(currMeasNr)] = cart2utm(xyzdt(1), xyzdt(2), ...
                                                xyzdt(3), ...
                                                navSolutions.utmZone);
        
    else % if size(activeChnList, 2) > 3 
        %--- There are not enough satellites to find 3D position ----------
        disp(['   Measurement No. ', num2str(currMeasNr), ...
                       ': Not enough information for position solution.']);

        %--- Set the missing solutions to NaN. These results will be
        %excluded automatically in all plots. For DOP it is easier to use
        %zeros. NaN values might need to be excluded from results in some
        %of further processing to obtain correct results.
        % 将失踪方案置为NaN，这些结果将会在所有绘制中自动排除。对于DOP使用0将会更容易.
        % 为了在进一步的处理中得到正确的结果，值为NaN的变量也许需要被从结果中消除
        navSolutions.X(currMeasNr)           = NaN;
        navSolutions.Y(currMeasNr)           = NaN;
        navSolutions.Z(currMeasNr)           = NaN;
        navSolutions.dt(currMeasNr)          = NaN;
        navSolutions.DOP(:, currMeasNr)      = zeros(5, 1);
        navSolutions.latitude(currMeasNr)    = NaN;
        navSolutions.longitude(currMeasNr)   = NaN;
        navSolutions.height(currMeasNr)      = NaN;
        navSolutions.E(currMeasNr)           = NaN;
        navSolutions.N(currMeasNr)           = NaN;
        navSolutions.U(currMeasNr)           = NaN;

        navSolutions.channel.az(activeChnList, currMeasNr) = ...
                                             NaN(1, length(activeChnList));
        navSolutions.channel.el(activeChnList, currMeasNr) = ...
                                             NaN(1, length(activeChnList));

        % TODO: Know issue. Satellite positions are not updated if the
        % satellites are excluded do to elevation mask. Therefore rasing
        % satellites will be not included even if they will be above
        % elevation mask at some point. This would be a good place to
        % update positions of the excluded satellites.
        % 如果卫星因海拔能见度而被排除了，该卫星的位置将不会被更新。
        % 因此上升的卫星将不会被包含在内即使它们在某些点高于海拔能见度。
        % 这将会是一个更新那些被排除的卫星的位置的好地方
        
    end % if size(activeChnList, 2) > 3

    %=== Update the transmit time ("measurement time") ====================
    transmitTime = transmitTime + settings.navSolPeriod / 1000;

end %for currMeasNr...
