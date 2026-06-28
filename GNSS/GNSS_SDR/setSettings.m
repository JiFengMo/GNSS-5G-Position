function varargout = setSettings(varargin)
% SETSETTINGS M-file for setSettings.fig
%      SETSETTINGS, by itself, creates a new SETSETTINGS or raises the existing
%      singleton*.
%
%      H = SETSETTINGS returns the handle to a new SETSETTINGS or the handle to
%      the existing singleton*.
%
%      SETSETTINGS('CALLBACK',hObject,eventData,handles,...) calls the local
%      function named CALLBACK in SETSETTINGS.M with the given input arguments.
%
%      SETSETTINGS('Property','Value',...) creates a new SETSETTINGS or raises the
%      existing singleton*.  Starting from the left, property value pairs are
%      applied to the GUI before setSettings_OpeningFunction gets called.  An
%      unrecognized property name or invalid value makes property application
%      stop.  All inputs are passed to setSettings_OpeningFcn via varargin.
%
%      *See GUI Options on GUIDE's Tools menu.  Choose "GUI allows only one
%      instance to run (singleton)".
%
% See also: GUIDE, GUIDATA, GUIHANDLES

% Edit the above text to modify the response to help setSettings


% setSettings本身会创建一个新的setSettings或引用现有的单元集*。
% H = setSettings返回一个新的setSettings句柄或现有单元集的句柄。

% setSettings('CALLBACK'，hObject,eventData,handles，…)调用
% 在setSettings中的本地函数CALLBACK，并给定的输入参数。
% setSettings('Property'，'Value'，…)创建一个新的setSettings或使用现有的单例对象。

% 从左边开始，在调用setSettings_OpeningFunction之前，将属性值对应用于GUI。
% 无法识别的属性名或无效值将使属性应用程序停止。
% 所有输入都通过varargin传递给setSettings_OpeningFcn。

% *参见指南工具菜单中的GUI选项。
% 选择“GUI只允许一个实例运行(单例)”。
% 参见:指南，指南，GUIHANDLES
% 编辑上面的文本以修改响应以帮助设置设置


% Last Modified by GUIDE v2.5 02-Aug-2006 10:33:09

% Begin initialization code - DO NOT EDIT
gui_Singleton = 1;
gui_State = struct('gui_Name',       mfilename, ...
    'gui_Singleton',  gui_Singleton, ...
    'gui_OpeningFcn', @setSettings_OpeningFcn, ...
    'gui_OutputFcn',  @setSettings_OutputFcn, ...
    'gui_LayoutFcn',  [] , ...
    'gui_Callback',   []);
if nargin && ischar(varargin{1})
    gui_State.gui_Callback = str2func(varargin{1});
end

if nargout
    [varargout{1:nargout}] = gui_mainfcn(gui_State, varargin{:});
else
    gui_mainfcn(gui_State, varargin{:});
end
% End initialization code - DO NOT EDIT


% --- Executes just before setSettings is made visible. 尝试从变量“settings”中读取数据
function setSettings_OpeningFcn(hObject, eventdata, handles, varargin)
% This function has no output args, see OutputFcn. 这个函数没有输出参数，请参见OutputFcn。
% hObject    handle to figure处理图
% eventdata  reserved - to be defined in a future version of MATLAB
%            保留参数 -在MATLAB的未来版本中定义
% handles    structure with handles and user data (see GUIDATA)
%            具有句柄和用户数据的结构(参见GUIDATA)
% varargin   command line arguments to setSettings (see VARARGIN)
%            setSettings的命令行参数(参见VARARGIN)

%--- Try to read data from the variable "settings" ------------------------
% 尝试从变量“settings”中读取数据
% (variable "settings" is in the base Matlab workspace)
try
    handles.settings = evalin('base', 'settings');
catch
    %--- Creat a new settings structure in case of an error ---------------
    handles.settings = initSettings();
end

%--- Assign it to the GUI data structure 将其分配给GUI数据结构----------------------------------
loadSettings(handles);

% Choose default command line output for setSettings 
% 为setSettings选择默认的命令行输出
handles.output = hObject;

% Update handles structure
guidata(hObject, handles);

% UIWAIT makes setSettings wait for user response (see UIRESUME)
% UIWAIT设置等待用户响应(参见UIRESUME)
% uiwait(handles.figure1);


% --- Outputs from this function are returned to the command line.
% 该函数的输出将返回到命令行
function varargout = setSettings_OutputFcn(hObject, eventdata, handles)
% varargout  cell array for returning output args (see VARARGOUT);用于返回输出参数的单元格数组
% hObject    handle to figure 注释见68-74行
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Get default command line output from handles structure
varargout{1} = handles.output;

%### Callback functions 回调函数 ###################################################

% --- Executes during object creation, after setting all properties.
% 设置所有属性后，在对象创建期间执行。
function edit_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit (see GCBO) 注释见68-74行
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
% 提示:在Windows上，编辑控件通常有白色的背景。参见ISPC和计算机。
if ispc && isequal(get(hObject,'BackgroundColor'), ...
        get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end


function edit_Callback(hObject, eventdata, handles)
% hObject    handle to edit (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit as text
%        str2double(get(hObject,'String')) returns contents of edit as a double
% 提示:get(hObject，'String')返回编辑的内容为文本str2double(get(hObject，'String'))
%      返回编辑的内容为double

%--- Enable the apply button on any input event 在任何输入事件上启用apply按钮 -----
set(handles.pushbuttonApply, 'Enable', 'on');


% --- Executes on button press in any checkbox.在任何checkbox中按下按钮时执行。
function checkbox_Callback(hObject, eventdata, handles)
% hObject    handle to PRN1checkbox (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)
% Hint: get(hObject,'Value') returns toggle state of PRN1checkbox
% 提示:get(hObject，'Value')返回PRN1checkbox的切换状态

%--- Enable the apply button on any input event 在任何输入事件上启用apply按钮----
set(handles.pushbuttonApply, 'Enable', 'on');

% --- Executes on button press in applybutton. 在applybutton中按下按钮时执行。
function pushbuttonApply_Callback(hObject, eventdata, handles)
% hObject    handle to applybutton (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

%--- Try to read values from the GUI input fields 尝试从GUI输入字段中读取值------
[settings, error] = saveSettings(handles);

%--- If no errors, then ...
if error == 0  
    %--- Save the updated settings in the main workspace ------------------
    % 将更新后的设置保存在主工作区中
    assignin('base', 'settings', settings);
        
    % Turn off the apply button 关闭apply按钮
    set(hObject, 'Enable', 'off');
end

% --- Executes on button press in resetbutton.  当reset按钮按下按钮时执行
function pushbuttonReset_Callback(hObject, eventdata, handles)
% hObject    handle to resetbutton (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

%--- Try to read data from the variable "settings" 尝试从变量“settings”中读取数据----
% (variable "settings" is in the base Matlab workspace)
try
    handles.settings = evalin('base', 'settings');
catch
    %--- Create a new settings structure in case of an error --------------
    handles.settings = initSettings();
end
    
%--- Assign it to the GUI data structure 将其分配给GUI数据结构--------------
loadSettings(handles);

%--- Save changes in the GUI data structure 保存GUI数据结构中的更改---------
guidata(hObject, handles);

%--- Turn off the apply button --------------------------------------------
set(handles.pushbuttonApply, 'Enable', 'off');

%@@@ Function reads values from the GUI and updates the settings structure
% 函数从GUI中读取值并更新设置参数
function [settings, error] = saveSettings(handles)
settings = handles.settings;
error = 0; % no error

try
    %Please read the Matlab help for mo details on TRY, CATCH and ERROR
    %commands. 
    
    %--- Signal properties related fields ---------------------------------
    settings.fileName           = get(handles.editFileName, 'String');
    settings.numberOfChannels   = edit2double(handles.editNumberOfChannels);
    settings.msToProcess        = edit2double(handles.editMsToProcess);    
    settings.skipNuberOfBytes   = edit2double(handles.editSkipNumberOfBytes);
    settings.IF                 = edit2double(handles.editIF);
    settings.samplingFreq       = edit2double(handles.editSamplingFreq);
    settings.dataType           = get(handles.editDataType, 'String');

    %--- Satellite PRN numbers --------------------------------------------
    for PRN = 1:32
        %If checkbox is checked 
        if getCheckbox(getfield(handles, ['checkboxPRN', num2str(PRN)])) == 1
            
            % Include satellite in the list
            settings.acqSatelliteList = ...
                                    union(settings.acqSatelliteList, PRN);
        else
            % Exclude satellite from the list
            settings.acqSatelliteList = ...
                                  setdiff(settings.acqSatelliteList, PRN);
        end
    end

    %--- Acquisition parameters 参数设置-----------------------------------
    settings.acqSearchBand     = edit2double(handles.editAcqSearchBand);
    settings.acqThreshold      = edit2double(handles.editAcqThreshold);
    settings.skipAcquisition   = getCheckbox(handles.checkboxSkipAcquisition);    
    
    %--- Tracking ---------------------------------------------------------
    settings.dllCorrelatorSpacing = edit2double(handles.editDllCorrelatorSpacing);
    settings.dllDampingRatio   = edit2double(handles.editDllDampingRatio);
    settings.dllNoiseBandwidth = edit2double(handles.editDllNoiseBandwidth);
    settings.pllDampingRatio   = edit2double(handles.editPllDampingRatio);
    settings.pllNoiseBandwidth = edit2double(handles.editPllNoiseBandwidth);
       
    %--- Nav solutions ----------------------------------------------------
    settings.elevationMask      = edit2double(handles.editElevationMask);
    settings.navSolPeriod       = edit2double(handles.editNavSolPeriod);
    settings.useTropCorr        = getCheckbox(handles.checkboxUseTropCorr);
    settings.truePosition.E     = edit2double(handles.editUtmE);
    settings.truePosition.N     = edit2double(handles.editUtmN);
    settings.truePosition.U     = edit2double(handles.editUtmU);
    
    %--- Plotting ---------------------------------------------------------    
    settings.plotTracking       = getCheckbox(handles.checkboxPlotTracking);

catch
    %Please read the Matlab help for mo details on TRY, CATCH and ERROR
    %commands. 
    
    %--- Read error information -------------------------------------------
    e = lasterror;
    
    %If this error caused by bad input 
    if strcmp(e.identifier, 'setSettings:badInput')
        % then do not save settings, return an error indication 
        error = 1;
    else
        % Not our error, this error must be handled/reported in the system 
        rethrow(e);
    end    
end

%@@@ Function loads the settings into the GUI 函数将设置加载到GUI中@@@@@@@@@
function loadSettings(handles)

%--- Signal properties related fields -------------------------------------
set(handles.editFileName, 'String', handles.settings.fileName);
set(handles.editNumberOfChannels, 'String', num2str(handles.settings.numberOfChannels));
set(handles.editSkipNumberOfBytes, 'String', num2str(handles.settings.skipNumberOfBytes));
set(handles.editMsToProcess, 'String', num2str(handles.settings.msToProcess));
set(handles.editIF, 'String', num2str(handles.settings.IF));
set(handles.editSamplingFreq, 'String', num2str(handles.settings.samplingFreq ));
set(handles.editDataType, 'String', handles.settings.dataType);

%--- Satellite PRN numbers ------------------------------------------------
for PRN = 1:32
    % If the PRN number is in the list
    if ismember(PRN, handles.settings.acqSatelliteList)
        % then set the checkbox to "checked" state
        setCheckbox(getfield(handles, ['checkboxPRN', num2str(PRN)]), 1);
    else
        % set the checkbox to "unchecked" state
        setCheckbox(getfield(handles, ['checkboxPRN', num2str(PRN)]), 0);
    end
end

%--- Acquisition parameters -------------------------------------------
set(handles.editAcqSearchBand, 'String', num2str(handles.settings.acqSearchBand));
set(handles.editAcqThreshold, 'String', num2str(handles.settings.acqThreshold));
setCheckbox(handles.checkboxSkipAcquisition, handles.settings.skipAcquisition);

%--- Tracking ---------------------------------------------------------
set(handles.editDllCorrelatorSpacing, 'String', num2str(handles.settings.dllCorrelatorSpacing));
set(handles.editDllDampingRatio, 'String', num2str(handles.settings.dllDampingRatio));
set(handles.editDllNoiseBandwidth, 'String', num2str(handles.settings.dllNoiseBandwidth));
set(handles.editPllDampingRatio, 'String', num2str(handles.settings.pllDampingRatio));
set(handles.editPllNoiseBandwidth, 'String', num2str(handles.settings.pllNoiseBandwidth));

%--- Nav solutions --------------------------------------------------------
set(handles.editElevationMask, 'String', num2str(handles.settings.elevationMask));
set(handles.editNavSolPeriod, 'String', num2str(handles.settings.navSolPeriod));
setCheckbox(handles.checkboxUseTropCorr, handles.settings.useTropCorr);
set(handles.editUtmE, 'String', num2str(handles.settings.truePosition.E));
set(handles.editUtmN, 'String', num2str(handles.settings.truePosition.N));
set(handles.editUtmU, 'String', num2str(handles.settings.truePosition.U));

%--- Plotting -------------------------------------------------------------
setCheckbox(handles.checkboxPlotTracking, handles.settings.plotTracking);

%@@@ Function reads current state of a checkbox "in the Matlab way" @@@@@@@
% 函数“以Matlab的方式”读取checkbox的当前状态
function value = getCheckbox(handle)

if (get(handle, 'Value') == get(handle,'Max'))
    % then checkbox is checked
    value = 1;
else
    % checkbox is not checked
    value = 0;
end

%@@@ Function sets checkbox state 设置checkbox状态@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
% Setting the "Value" variable to some number not equal to "Min" or "Max"
% will cause Matlab runtime error.
% 将“Value”变量设置为不等于“Min”或“Max”的数字会导致Matlab运行时错误。
function setCheckbox(handle, value)

if (value == 1)
    % "check" the checkbox
    set(handle, 'Value', get(handle,'Max'));
else
    % "uncheck" the checkbox
    set(handle, 'Value', get(handle,'Min'));
end

%@@@ Function checks if the edit field contains a numeric value. If yes,
%then it converts string type value to double. @@@@@@@@@@@@@@@@@@@@@@@@@@@@
% 函数检查编辑字段是否包含数字值，如果是，则将字符串类型值转换为double。
function value = edit2double(handle)

%--- Try to convert string in the entry field to double -------------------
value = str2double(get(handle, 'String'));

% If it is not a number, then handle the incorect input -------------------
if isnan(value) && ~strcmpi(get(handle, 'String'), 'NaN')
    %--- Make the message text ---
    text = ['Bad input in the field "', get(handle, 'UserData'),...
        '". You must enter a numeric value.'];
    
    % Show the error message in a message box
    errordlg(text, 'Bad Input', 'modal');
    
    %--- Stop code execution here and "Throw an error". The error will be
    % "cached" by the "CATCH" statement. The code execution resumes from
    % at the "CATCH" statement. Please read the Matlab help for mo details
    % on TRY, CATCH and ERROR commands.
    error('setSettings:badInput', text);
end


% --- Executes on button press in pushbuttonSelectDataFile.当按钮按下pushbuttonSelectDataFile时执行
function pushbuttonSelectDataFile_Callback(hObject, eventdata, handles)
% hObject    handle to pushbuttonSelectDataFile (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

[fileName, pathName] = uigetfile('*.bin', ...
                                 'Select data file...', ...
                                 get(handles.editFileName, 'String'));

if (~isequal(fileName, 0) && ~isequal(pathName, 0))
    set(handles.editFileName, 'String', fullfile(pathName, fileName));
    set(handles.pushbuttonApply, 'Enable', 'on');    
end

% --- Executes on button press in pushbuttonProbeData. 当按钮按下pushbuttonProbeData时执行
function pushbuttonProbeData_Callback(hObject, eventdata, handles)
% hObject    handle to pushbuttonProbeData (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

[settings, error] = saveSettings(handles);

%--- If no errors, then ...
if error == 0
    try
        probeData(settings);
    catch
        errStruct = lasterror;
        msgbox(errStruct.message, 'Error', 'error');
    end
end


% --- Executes on button press in pushbuttonDefault.
function pushbuttonDefault_Callback(hObject, eventdata, handles)
% hObject    handle to pushbuttonDefault (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

handles.settings = initSettings();

% Assign it to the GUI data structure
loadSettings(handles);

% Update handles structure
guidata(hObject, handles);

% Turn on the apply button
set(handles.pushbuttonApply, 'Enable', 'on');
