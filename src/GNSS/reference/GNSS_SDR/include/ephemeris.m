function [eph, TOW] = ephemeris(bits, D30Star)
%Function decodes ephemerides and TOW from the given bit stream. The stream
%(array) in the parameter BITS must contain 1500 bits. The first element in
%the array must be the first bit of a subframe. The subframe ID of the
%first subframe in the array is not important.
%
% 函数从给定的位流中解码星历表和TOW。位流(数组)必须包含1500位。数组中的第一个元素
% 必须是子帧的第一个比特。数组中第一个子帧的子帧ID不重要。


% Function does not check parity!
% 该函数不检查奇偶校验
%
%[eph, TOW] = ephemeris(bits, D30Star)
%
%   Inputs:
%       bits        - bits of the navigation messages (5 subframes).
%                   Type is character array and it must contain only
%                   characters '0' or '1'.
%                   导航信息的位(5个子帧)。类型是字符数组，它必须只包含字符'0'或'1'。
%       D30Star     - The last bit of the previous nav-word. Refer to the
%                   GPS interface control document ICD (IS-GPS-200D) for
%                   more details on the parity checking. Parameter type is
%                   char. It must contain only characters '0' or '1'.
%                   前一个导航字的最后一个比特。关于奇偶校验的详细信息，
%                   请参考GPS接口控制文档ICD (IS-GPS-200D)。参数类型为char。
%                   只能包含字符'0'或'1'。
%   Outputs:
%       TOW         - Time Of Week (TOW) of the first sub-frame in the bit
%                   stream (in seconds) 位流中第一个子帧的每周时间(TOW)(以秒为单位)
%       eph         - SV ephemeris SV星历表

%--------------------------------------------------------------------------
%                           SoftGNSS v3.0
% 
% Copyright (C) Darius Plausinaitis and Kristin Larson
% Written by Darius Plausinaitis and Kristin Larson
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
%$Id: ephemeris.m,v 1.1.2.7 2006/08/14 11:38:22 dpl Exp $


%% Check if there is enough data 检查数据数否充足 ====================
if length(bits) < 1500
    error('The parameter BITS must contain 1500 bits!');
end

%% Check if the parameters are strings 确定星历表是否为strings类型====================================
if ~ischar(bits)
    error('The parameter BITS must be a character array!');
end

if ~ischar(D30Star)
    error('The parameter D30Star must be a char!');
end

% Pi used in the GPS coordinate system
gpsPi = 3.1415926535898; 

%% Decode all 5 sub-frames 解码5个子帧================================
for i = 1:5

    %--- "Cut" one sub-frame's bits 切割子帧--------------------------------
    subframe = bits(300*(i-1)+1 : 300*i);

    %--- Correct polarity of the data bits in all 10 words ----------------
    % 纠正所有10个字的数据位的极性
    for j = 1:10
        [subframe(30*(j-1)+1 : 30*j)] = ...
            checkPhase(subframe(30*(j-1)+1 : 30*j), D30Star);
        
        D30Star = subframe(30*j);
    end

    %--- Decode the sub-frame id ------------------------------------------
    % 解码子帧id（包含子帧1-5）
    % For more details on sub-frame contents please refer to GPS IS.
    % 详细的子帧内容参考GPS IS
    subframeID = bin2dec(subframe(50:52));

    %--- Decode sub-frame based on the sub-frames id ----------------------
    % 基于子帧id进行解码子帧
    % The task is to select the necessary bits and convert them to decimal
    % numbers. For more details on sub-frame contents please refer to GPS
    % ICD (IS-GPS-200D).
    % 任务是选择必要的位并将其转换为十进制数，详细的子帧内容请参考GPS ICD (IS-GPS-200D)。
    switch subframeID
        case 1  %--- It is subframe 1 -------------------------------------
            % It contains WN, SV clock corrections, health and accuracy
            eph.weekNumber  = bin2dec(subframe(61:70)) + 1024;
            eph.accuracy    = bin2dec(subframe(73:76));
            eph.health      = bin2dec(subframe(77:82));
            eph.T_GD        = twosComp2dec(subframe(197:204)) * 2^(-31);
            eph.IODC        = bin2dec([subframe(83:84) subframe(197:204)]);
            eph.t_oc        = bin2dec(subframe(219:234)) * 2^4;
            eph.a_f2        = twosComp2dec(subframe(241:248)) * 2^(-55);
            eph.a_f1        = twosComp2dec(subframe(249:264)) * 2^(-43);
            eph.a_f0        = twosComp2dec(subframe(271:292)) * 2^(-31);

        case 2  %--- It is subframe 2 -------------------------------------
            % It contains first part of ephemeris parameters
            eph.IODE_sf2    = bin2dec(subframe(61:68));
            eph.C_rs        = twosComp2dec(subframe(69: 84)) * 2^(-5);
            eph.deltan      = ...
                twosComp2dec(subframe(91:106)) * 2^(-43) * gpsPi;
            eph.M_0         = ...
                twosComp2dec([subframe(107:114) subframe(121:144)]) ...
                * 2^(-31) * gpsPi;
            eph.C_uc        = twosComp2dec(subframe(151:166)) * 2^(-29);
            eph.e           = ...
                bin2dec([subframe(167:174) subframe(181:204)]) ...
                * 2^(-33);
            eph.C_us        = twosComp2dec(subframe(211:226)) * 2^(-29);
            eph.sqrtA       = ...
                bin2dec([subframe(227:234) subframe(241:264)]) ...
                * 2^(-19);
            eph.t_oe        = bin2dec(subframe(271:286)) * 2^4;

        case 3  %--- It is subframe 3 -------------------------------------
            % It contains second part of ephemeris parameters
            eph.C_ic        = twosComp2dec(subframe(61:76)) * 2^(-29);
            eph.omega_0     = ...
                twosComp2dec([subframe(77:84) subframe(91:114)]) ...
                * 2^(-31) * gpsPi;
            eph.C_is        = twosComp2dec(subframe(121:136)) * 2^(-29);
            eph.i_0         = ...
                twosComp2dec([subframe(137:144) subframe(151:174)]) ...
                * 2^(-31) * gpsPi;
            eph.C_rc        = twosComp2dec(subframe(181:196)) * 2^(-5);
            eph.omega       = ...
                twosComp2dec([subframe(197:204) subframe(211:234)]) ...
                * 2^(-31) * gpsPi;
            eph.omegaDot    = twosComp2dec(subframe(241:264)) * 2^(-43) * gpsPi;
            eph.IODE_sf3    = bin2dec(subframe(271:278));
            eph.iDot        = twosComp2dec(subframe(279:292)) * 2^(-43) * gpsPi;

        case 4  %--- It is subframe 4 -------------------------------------
            % Almanac, ionospheric model, UTC parameters.
            % SV health (PRN: 25-32).
            % Not decoded at the moment.

        case 5  %--- It is subframe 5 -------------------------------------
            % SV almanac and health (PRN: 1-24).
            % Almanac reference week number and time.
            % Not decoded at the moment.

    end % switch subframeID ...

end % for all 5 sub-frames ...

%% Compute the time of week (TOW) of the first sub-frames in the array ====
% Also correct the TOW. The transmitted TOW is actual TOW of the next
% subframe and we need the TOW of the first subframe in this data block
% (the variable subframe at this point contains bits of the last subframe). 
TOW = bin2dec(subframe(31:47)) * 6 - 30; % 计算数组中第一个子帧的每周时间(TOW)
