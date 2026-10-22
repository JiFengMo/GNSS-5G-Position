function make_b1c_tracking_baseline(referenceRoot, iqPath, acquisitionDir, outputDir, durationSeconds)
% Re-run CU-SDR NB_tracking with only progress-window code removed.
% Run from project root: addpath('src/GNSS/tools');
% make_b1c_tracking_baseline(REFERENCE_ROOT, IQ, 'build/gnss-baseline/matlab', ...
%                          'build/gnss-tracking/matlab', 60)
% Outputs: per-PRN CSVs, tracking.mat, manifest.json and GUI-only adapted source.
% Next: compare with C using compare_b1c_tracking.py; see docs/tracking.md.
arguments
    referenceRoot (1,:) char
    iqPath (1,:) char
    acquisitionDir (1,:) char
    outputDir (1,:) char
    durationSeconds (1,1) double = 60
end
assert(isfinite(durationSeconds) && durationSeconds >= 1 && durationSeconds <= 60);
oldPath = path;
restorePath = onCleanup(@() path(oldPath)); %#ok<NASGU>
b1cRoot = fullfile(referenceRoot, 'BDS', 'B1C');
addpath(fullfile(b1cRoot, 'Common'), '-begin');
addpath(fullfile(b1cRoot, 'include'), '-begin');
addpath(b1cRoot, '-begin');
if ~isfolder(outputDir), mkdir(outputDir); end
loaded = load(fullfile(acquisitionDir, 'baseline.mat'));
settings = loaded.settings;
assert(settings.samplingFreq == 18e6 && settings.IF == 20e3);
settings.fileName = iqPath;
settings.msToProcess = round(durationSeconds * 1000);
settings.numberOfChannels = sum(loaded.acqResults.carrFreq ~= 0);
channel = preRun(loaded.acqResults, settings);

sourcePath = fullfile(b1cRoot, 'include', 'NB_tracking.m');
source = fileread(sourcePath);
adapted = strrep(source, '= NB_tracking(', '= NB_tracking_headless(');
adapted = regexprep(adapted, '(?s)% Start waitbar.*?(?=if \(settings.fileType==1\))', 'hwb = [];\n\n');
adapted = regexprep(adapted, '(?s)%% GUI update.*?(?=%% Read next block)', '% Headless progress: numerical loop unchanged.\n\n            ');
adapted = strrep(adapted, 'close(hwb)', '% No GUI to close.');
assert(~contains(adapted, 'waitbar(') && ~contains(adapted, 'get(hwb'));
adaptedPath = fullfile(outputDir, 'NB_tracking_headless.m');
f = fopen(adaptedPath, 'w'); assert(f >= 0); fwrite(f, adapted, 'char'); fclose(f);
addpath(outputDir, '-begin');

f = fopen(iqPath, 'rb'); assert(f >= 0);
closeInput = onCleanup(@() fclose(f)); %#ok<NASGU>
window = fread(f, 720000, '*int8');
assert(strcmp(sha256(window), loaded.manifest.input_window_sha256));
info = dir(iqPath);
manifest = struct('schema_version', 1, 'matlab_version', version, ...
    'sample_rate_hz', settings.samplingFreq, 'if_hz', settings.IF, ...
    'iq_file_bytes', info.bytes, 'input_format', 'ci8', 'duration_seconds', durationSeconds, ...
    'integration_s', settings.intTime, 'dll_bandwidth_hz', settings.dllNoiseBandwidth, ...
    'dll_damping', settings.dllDampingRatio, 'early_late_offset_chips', settings.dllCorrelatorSpacing, ...
    'pll_bandwidth_hz', settings.pllNoiseBandwidth, 'quality_epochs', settings.CNoInterval, ...
    'input_window_sha256', loaded.manifest.input_window_sha256, ...
    'reference_source_sha256', sha256(int8(unicode2native(source, 'UTF-8'))), ...
    'headless_source_sha256', sha256(int8(unicode2native(adapted, 'UTF-8'))), ...
    'adaptation', 'Only waitbar setup/update/close removed; loop mathematics unchanged', ...
    'prns', [channel.PRN]);
manifest.iq_sha256 = sha256file(iqPath);
fprintf('Fresh MATLAB narrowband tracking: %d channels, %.1f s each...\n', settings.numberOfChannels, durationSeconds);
tic;
[trackResults, channel] = NB_tracking_headless(f, channel, settings);
manifest.elapsed_s = toc;
save(fullfile(outputDir, 'tracking.mat'), 'trackResults', 'channel', 'settings', 'manifest');
for k = 1:numel(trackResults)
    t = trackResults(k);
    n = numel(t.absoluteSample);
    names = {'epoch','absolute_sample','carrier_hz','code_rate_hz','rem_code_phase', ...
        'rem_carrier_phase','dll_error','pll_error','code_nco_hz','carrier_nco_hz', ...
        'data_i_e','data_q_e','data_i_p','data_q_p','data_i_l','data_q_l','pilot_i_p','pilot_q_p'};
    values = [(0:n-1)' t.absoluteSample' t.carrFreq' t.codeFreq' t.remCodePhase' ...
        t.remCarrPhase' t.dllDiscr' t.pllDiscr' t.dllDiscrFilt' t.pllDiscrFilt' ...
        t.I_E' t.Q_E' t.I_P' t.Q_P' t.I_L' t.Q_L' t.Pilot_I_P' t.Pilot_Q_P'];
    csvPath = fullfile(outputDir, sprintf('prn_%02d.csv', t.PRN));
    out = fopen(csvPath, 'w'); assert(out >= 0);
    fprintf(out, '%s\n', strjoin(names, ','));
    fprintf(out, [repmat('%.17g,', 1, size(values,2)-1) '%.17g\n'], values');
    fclose(out);
end
fout = fopen(fullfile(outputDir, 'manifest.json'), 'w'); assert(fout >= 0);
fprintf(fout, '%s\n', jsonencode(manifest, 'PrettyPrint', true)); fclose(fout);
fprintf('Tracking baseline complete: %.1f s\n', manifest.elapsed_s);
end

function hex = sha256(bytes)
digest = java.security.MessageDigest.getInstance('SHA-256');
digest.update(bytes);
hex = lower(reshape(dec2hex(typecast(digest.digest(), 'uint8'), 2)', 1, []));
end

function hex = sha256file(filePath)
f = fopen(filePath, 'rb'); assert(f >= 0);
cleanup = onCleanup(@() fclose(f)); %#ok<NASGU>
digest = java.security.MessageDigest.getInstance('SHA-256');
while ~feof(f)
    digest.update(fread(f, 1048576, '*int8'));
end
hex = lower(reshape(dec2hex(typecast(digest.digest(), 'uint8'), 2)', 1, []));
end
