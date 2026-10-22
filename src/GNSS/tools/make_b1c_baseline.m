function make_b1c_baseline(referenceRoot, iqPath, outputDir)
% Re-run the unmodified CU-SDR B1C acquisition on the first 20 ms of IQ.
% Run from the project root (MATLAB R2024b was used for validation):
% addpath('src/GNSS/tools'); make_b1c_baseline(REFERENCE_ROOT, IQ_FILE, OUTPUT_DIR)
% Outputs: manifest.json, acquisition.csv, baseline.mat, code/waveform vectors.
% Next: run compare_b1c_baseline.py with the built C receiver and code test.
arguments
    referenceRoot (1,:) char
    iqPath (1,:) char
    outputDir (1,:) char
end
oldPath = path;
restorePath = onCleanup(@() path(oldPath)); %#ok<NASGU>
b1cRoot = fullfile(referenceRoot, 'BDS', 'B1C');
addpath(fullfile(b1cRoot, 'Common'), '-begin');
addpath(fullfile(b1cRoot, 'include'), '-begin');
addpath(b1cRoot, '-begin');
assert(strcmp(which('acquisition'), fullfile(b1cRoot, 'include', 'acquisition.m')), ...
    'Another acquisition.m shadows the B1C reference');
if ~isfolder(outputDir), mkdir(outputDir); end
settings = initSettings();
settings.fileName = iqPath;
settings.acqSatelliteList = 1:63;
settings.skipNumberOfBytes = 0;
settings.plotAcquisition = 0;
settings.plotTracking = 0;
settings.plotNavigation = 0;
assert(settings.fileType == 2 && strcmp(settings.dataType, 'schar'));
assert(settings.samplingFreq == 18e6 && settings.IF == 20e3);
assert(settings.acqCohT == 10 && settings.acqStep == 50);
assert(settings.pilotACQflag == 1 && settings.resamplingflag == 0);
count = round(0.02 * settings.samplingFreq);
fid = fopen(iqPath, 'rb');
assert(fid >= 0, 'Cannot open IQ file');
closeInput = onCleanup(@() fclose(fid)); %#ok<NASGU>
raw = fread(fid, 2 * count, '*int8');
assert(numel(raw) == 2 * count, 'IQ file is shorter than 20 ms');
iq = double(raw(1:2:end))' + 1i * double(raw(2:2:end))';
info = dir(iqPath);
manifest = struct('schema_version', 1, 'matlab_version', version, ...
    'iq_path', iqPath, 'iq_file_bytes', info.bytes, ...
    'input_format', 'ci8', 'iq_order', 'I+jQ', 'sample_rate_hz', 18e6, ...
    'if_hz', 20e3, 'first_sample_index', 0, 'input_samples', count, ...
    'input_window_sha256', sha256(raw), 'coherent_integration_ms', 10, ...
    'doppler_min_hz', -5000, 'doppler_max_hz', 5000, ...
    'doppler_step_hz', 50, 'fine_step_hz', 25, 'threshold', 10, ...
    'prns', 1:63, 'phase_convention', 'zero-based first code sample in input window');
sources = {'initSettings.m', 'include/acquisition.m', ...
    'include/generateDataBOC11.m', 'include/generatePilotBOC11.m', ...
    'include/generate2ndCode.m', 'include/makeDataTable.m', ...
    'include/makePilotTable.m', 'include/JacobiSymbol.m'};
manifest.reference_files = struct('path', {}, 'sha256', {});
for k = 1:numel(sources)
    sourcePath = fullfile(b1cRoot, sources{k});
    f = fopen(sourcePath, 'rb'); assert(f >= 0);
    content = fread(f, Inf, '*int8'); fclose(f);
    manifest.reference_files(k) = struct('path', sourcePath, 'sha256', sha256(content));
end
writeJson(fullfile(outputDir, 'manifest.json'), manifest);

% PRN-major signed int8 bipolar vectors, independently exported by MATLAB.
dataFile = fopen(fullfile(outputDir, 'data_primary.i8'), 'wb');
pilotFile = fopen(fullfile(outputDir, 'pilot_primary.i8'), 'wb');
secondaryFile = fopen(fullfile(outputDir, 'pilot_secondary.i8'), 'wb');
assert(all([dataFile pilotFile secondaryFile] >= 0));
closeCodes = onCleanup(@() closeFiles([dataFile pilotFile secondaryFile])); %#ok<NASGU>
for prn = 1:63
    data = generateDataBOC11(settings, prn);
    pilot = generatePilotBOC11(settings, prn);
    secondary = generate2ndCode(prn);
    fwrite(dataFile, data(2:2:end), 'int8');
    fwrite(pilotFile, pilot(2:2:end), 'int8');
    fwrite(secondaryFile, secondary, 'int8');
end
for fs = [4092000 18000000]
    sampledSettings = settings;
    sampledSettings.samplingFreq = fs;
    waveformFile = fopen(fullfile(outputDir, sprintf('sampled_%d.i8', fs)), 'wb');
    assert(waveformFile >= 0);
    closeWaveform = onCleanup(@() fclose(waveformFile));
    for prn = [1 19 23 63]
        fwrite(waveformFile, makeDataTable(sampledSettings, prn), 'int8');
        fwrite(waveformFile, makePilotTable(sampledSettings, prn), 'int8');
    end
    clear closeWaveform;
end

fprintf('Re-running reference acquisition: 63 PRNs, 20 ms, 18 Msps...\n');
tic;
acqResults = acquisition(iq, settings);
manifest.acquisition_elapsed_s = toc;
writeJson(fullfile(outputDir, 'manifest.json'), manifest);
csvFile = fopen(fullfile(outputDir, 'acquisition.csv'), 'w');
assert(csvFile >= 0);
closeCsv = onCleanup(@() fclose(csvFile)); %#ok<NASGU>
fprintf(csvFile, 'prn,carrier_hz,doppler_hz,code_phase_samples,peak_metric,acquired\n');
for prn = settings.acqSatelliteList
    acquired = acqResults.carrFreq(prn) ~= 0;
    phase = acqResults.codePhase(prn) - 1;
    fprintf(csvFile, '%d,%.12g,%.12g,%d,%.17g,%d\n', prn, ...
        acqResults.carrFreq(prn), acqResults.carrFreq(prn) - settings.IF, ...
        phase, acqResults.peakMetric(prn), acquired);
end
save(fullfile(outputDir, 'baseline.mat'), 'settings', 'manifest', 'acqResults');
fprintf('Baseline complete: %.1f s; acquired PRNs: ', manifest.acquisition_elapsed_s);
disp(find(acqResults.carrFreq));
end

function hex = sha256(bytes)
digest = java.security.MessageDigest.getInstance('SHA-256');
digest.update(bytes);
hex = lower(reshape(dec2hex(typecast(digest.digest(), 'uint8'), 2)', 1, []));
end

function writeJson(filePath, value)
f = fopen(filePath, 'w'); assert(f >= 0);
cleanup = onCleanup(@() fclose(f)); %#ok<NASGU>
fprintf(f, '%s\n', jsonencode(value, 'PrettyPrint', true));
end

function closeFiles(files)
for f = files, fclose(f); end
end
