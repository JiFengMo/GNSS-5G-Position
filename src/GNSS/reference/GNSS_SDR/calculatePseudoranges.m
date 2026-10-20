function [pseudoranges] = calculatePseudoranges(trackResults, ...
                                               msOfTheSignal, ...
                                               channelList, settings)
% channelList=activeChnList;
% msOfTheSignal=subFrameStart + settings.navSolPeriod * (currMeasNr-1);
%calculatePseudoranges finds relative pseudoranges for all satellites
%listed in CHANNELLIST at the specified millisecond of the processed
%signal. The pseudoranges contain unknown receiver clock offset. It can be
%found by the least squares position search procedure. 
%
% [pseudoranges] = calculatePseudoranges(trackResults, msOfTheSignal, ...
%                                       channelList, settings)
% 
%   Inputs:
%       trackResults    - output from the tracking function
%       msOfTheSignal   - pseudorange measurement point (millisecond) in
%                       the trackResults structure
%       channelList     - list of channels to be processed
%       settings        - receiver settings
% 
%   Outputs:
%       pseudoranges    - relative pseudoranges to the satellites. 

%--------------------------------------------------------------------------
%                           SoftGNSS v3.0
% 
% Copyright (C) Darius Plausinaitis
% Written by Darius Plausinaitis
% Based on Peter Rinder and Nicolaj Bertelsen
%--------------------------------------------------------------------------
%
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

% CVS record:
% $Id: calculatePseudoranges.m,v 1.1.2.18 2006/08/09 17:20:11 dpl Exp $

%--- Set initial travel time to infinity 将初始化时间设置为无穷大-----------
% Later in the code a shortest pseudorange will be selected. Therefore
% pseudoranges from non-tracking channels must be the longest - e.g.
% infinite. 
% 因为需要在代码中将选择最短的伪距。因此，来自非跟踪信道的伪距必须是最长的，所以选择无穷大。
travelTime = inf(1, settings.numberOfChannels);

% Find number of samples per spreading code 找出每个码片的样本数量
samplesPerCode = round(settings.samplingFreq / ...
                        (settings.codeFreqBasis / settings.codeLength));

%--- For all channels in the list ... 
for channelNr = 1:numel(channelList)

    %--- Compute the travel times -----------------------------------------
    % 计算采样传输时间
    travelTime(channelNr) = ...
        trackResults(channelNr).absoluteSample(msOfTheSignal(channelNr)) / samplesPerCode;
end

%--- Truncate the travelTime and compute pseudoranges ---------------------
minimum         = floor(min(travelTime));
travelTime      = travelTime - minimum + settings.startOffset;

%--- Convert travel time to a distance ------------------------------------
% The speed of light must be converted from meters per second to meters
% per millisecond. 计算出每个卫星距离接收机的伪距
pseudoranges    = travelTime * (settings.c / 1000);
