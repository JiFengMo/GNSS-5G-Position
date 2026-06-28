function [firstSubFrame, activeChnList] = findPreambles(trackResults, ...
                                                        settings)
% findPreambles finds the first preamble occurrence in the bit stream of
% each channel. The preamble is verified by check of the spacing between
% preambles (6sec) and parity checking of the first two words in a
% subframe. At the same time function returns list of channels, that are in
% tracking state and with valid preambles in the nav data stream.

% 实现帧同步：findPreambles在每个信道的比特流中查找遥测字与交接字。通过检查时间间隔(6秒)
% 和对子帧中的前两个字进行奇偶校验来验证前置符。同时函数返回处于跟踪状态的通道
% 列表，以及具有有效导航数据的导航帧头的位置。

%[firstSubFrame, activeChnList] = findPreambles(trackResults, settings)
%
%   Inputs:
%       trackResults    - output from the tracking function
%       settings        - Receiver settings.
% 
%   Outputs:
%       firstSubframe   - the array contains positions of the first
%                       preamble in each channel. The position is ms count 
%                       since start of tracking. Corresponding value will
%                       be set to 0 if no valid preambles were detected in
%                       the channel.
%       activeChnList   - list of channels containing valid preambles

%--------------------------------------------------------------------------
%                           SoftGNSS v3.0
% 
% Copyright (C) Darius Plausinaitis, Peter Rinder and Nicolaj Bertelsen
% Written by Darius Plausinaitis, Peter Rinder and Nicolaj Bertelsen
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
% $Id: findPreambles.m,v 1.1.2.10 2006/08/14 11:38:22 dpl Exp $

% Preamble search can be delayed to a later point in the tracking results
% to avoid noise due to tracking loop transients 
% 该函数可以延迟到跟踪结果中的一个较晚的点，以避免跟踪环路瞬变引起的噪声
searchStartOffset = 0;

%--- Initialize the firstSubFrame array -----------------------------------
% 初始化firstSubFrame数组
firstSubFrame = zeros(1, settings.numberOfChannels);

%--- Generate the preamble pattern ----------------------------------------
% 生成preamble模式，每一帧的第一个字为遥测字，遥测字的首八个比特固定位10001011的同步码
preamble_bits = [1 -1 -1 -1 1 -1 1 1];

% "Upsample" the preamble - make 20 vales per one bit. The preamble must be
% found with precision of a sample.
preamble_ms = kron(preamble_bits, ones(1, 20));
% kron函数：如果 A 是 m×n 矩阵，而 B 是 p×q 矩阵，则 kron(A,B) 是通过获取 A 元素与
% 矩阵 B 元素之间的所有可能积而形成的一个 m*p×n*q 矩阵。

%--- Make a list of channels excluding not tracking channels --------------
% 列出只包含已追踪到的卫星通道
activeChnList = find([trackResults.status] ~= '-');

%=== For all tracking channels ...
for channelNr = activeChnList

%% Correlate tracking output with preamble ================================
% 将追踪函数的输出，与遥测字求相关
    % Read output from tracking. It contains the navigation bits. The start
    % of record is skiped here to avoid tracking loop transients.
    % 从跟踪读取输出，它包含导航位。记录的开始在这里跳过，以避免跟踪环路瞬变。
    bits = trackResults(channelNr).I_P(1 + searchStartOffset : end);

    % Now threshold the output and convert it to -1 and +1 
    % 理论上1ms得到1bit。但是，所处理的是带有噪声的微弱信号，
    % 因此每20ms计算一个平均值，并二值化为1或-1
    bits(bits > 0)  =  1;
    bits(bits <= 0) = -1;

    % Correlate tracking output with the preamble 求相关
    tlmXcorrResult = xcorr(bits, preamble_ms);

%% Find all starting points off all preamble like patterns ================
% 找到所有与遥测字相类似的起始点
    clear index
    clear index2

    xcorrLength = (length(tlmXcorrResult) +  1) /2;

    %--- Find at what index/ms the preambles start ------------------------
    % 找到遥测字开始的地方/毫秒
    index = find(...
        abs(tlmXcorrResult(xcorrLength : xcorrLength * 2 - 1)) > 153)' + ...
        searchStartOffset;

%% Analyze detected preamble like patterns ================================
% 分析检测到的遥测字疑似起始点
    for i = 1:size(index) % For each occurrence 对每个疑似点进行排查

        %--- Find distances in time between this occurrence and the rest of
        %preambles like patterns. If the distance is 6000 milliseconds (one
        %subframe), the do further verifications by validating the parities
        %of two GPS words
        % 找出这个起点和其他疑似起点间的时间间隔。如果间隔是6000毫秒(一个子帧)，
        % 则通过验证两个GPS子帧的奇偶性来进一步验证
        index2 = index - index(i);

        if (~isempty(find(index2 == 6000)))

            %=== Re-read bit vales for preamble verification ==============
            % Preamble occurrence is verified by checking the parity of
            % the first two words in the subframe. Now it is assumed that
            % bit boundaries a known. Therefore the bit values over 20ms are
            % combined to increase receiver performance for noisy signals.
            % in Total 62 bits mast be read :
            % 2 bits from previous subframe are needed for parity checking;
            % 60 bits for the first two 30bit words (TLM and HOW words).
            % The index is pointing at the start of TLM word.
            % 通过检查子帧中前两个单词的奇偶性来验证起始点的真实性。
            % 假定数据跳变的边界是已知的。因此，将超过20ms的位值组合起来，
            % 以提高接收机在噪声信号下的性能。共读取62位:需要前一帧的2位进行奇偶校验;
            % 前两个30位字(遥测字TLM和交接字HOW)用60位。
            % 索引指向遥测字字的开始。
            bits = trackResults(channelNr).I_P(index(i)-40 : ...
                                               index(i) + 20 * 60 -1)';

            %--- Combine the 20 values of each bit ------------------------
            bits = reshape(bits, 20, (size(bits, 1) / 20));
            bits = sum(bits);

            % Now threshold and make it -1 and +1 
            bits(bits > 0)  = 1;
            bits(bits <= 0) = -1;

            %--- Check the parity of the TLM and HOW words ----------------
            if (navPartyChk(bits(1:32)) ~= 0) && ...
               (navPartyChk(bits(31:62)) ~= 0)
                % Parity was OK. Record the preamble start position. Skip
                % the rest of preamble pattern checking for this channel
                % and process next channel. 
                % 判断奇偶校验是否正常，记录遥测字的起始位置，并跳过此通道的遥测字
                % 检查的其余部分，处理下一个通道。
                firstSubFrame(channelNr) = index(i);
                break;    
            end % if parity is OK ...
            
        end % if (~isempty(find(index2 == 6000)))
    end % for i = 1:size(index)

    % Exclude channel from the active channel list if no valid preamble was
    % detected
    %如果没有检测到有效的起始点，则从有效通道列表中排除该内容
    if firstSubFrame(channelNr) == 0
        
        % Exclude channel from further processing. It does not contain any
        % valid preamble and therefore nothing more can be done for it.
        activeChnList = setdiff(activeChnList, channelNr);
        
        disp(['Could not find valid preambles in channel ', ...
                                                  num2str(channelNr),'!']);
    end
    
end % for channelNr = activeChnList
