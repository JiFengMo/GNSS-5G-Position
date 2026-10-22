function make_b1c_navigation_baseline(referenceRoot, trackingInput, outputDir)
% Execute original CU-SDR navigation functions independently on each frame.
% trackingInput: tracking.mat from the MATLAB tracking baseline, OR a C
% tracking output directory (to validate reacquired channels too).
% Run in project root: addpath('src/GNSS/tools');
% make_b1c_navigation_baseline(REFERENCE_ROOT, TRACKING_INPUT, OUTPUT_DIR)
% Outputs: frames.json (878 bits + all original ephemeris fields), manifest.
% Next: compare_b1c_navigation.py; see docs/navigation.md.
arguments
    referenceRoot (1,:) char
    trackingInput (1,:) char
    outputDir (1,:) char
end
oldPath=path; restore=onCleanup(@() path(oldPath)); %#ok<NASGU>
b1cRoot=fullfile(referenceRoot,'BDS','B1C');
addpath(fullfile(b1cRoot,'Common'),'-begin');
addpath(fullfile(b1cRoot,'include'),'-begin');
if ~isfolder(outputDir), mkdir(outputDir); end
if isfolder(trackingInput)
    files=dir(fullfile(trackingInput,'prn_*.csv'));
    tracks=cell(1,numel(files));
    for k=1:numel(files)
        t=readtable(fullfile(files(k).folder,files(k).name));
        prn=sscanf(files(k).name,'prn_%d.csv');
        tracks{k}=struct('PRN',prn,'I_P',t.data_i_p', ...
            'Pilot_Q_P',t.pilot_q_p','absoluteSample',t.absolute_sample', ...
            'generation',t.generation','epoch',t.epoch','valid',logical(t.valid'));
        tracks{k}.sampleCount=t.sample_count';
    end
else
    loaded=load(trackingInput,'trackResults');
    tracks=cell(1,numel(loaded.trackResults));
    for k=1:numel(tracks)
        t=loaded.trackResults(k); n=numel(t.I_P);
        t.generation=zeros(1,n); t.epoch=0:n-1; t.valid=true(1,n);
        % No quality gate in original MATLAB. Candidate positions will be
        % compared against C's stricter valid+LOCKED gate separately.
        t.sampleCount=[diff(t.absoluteSample) NaN];
        tracks{k}=t;
    end
end
settings.pilotTRKflag=1; frames=struct([]); candidates=0;
for k=1:numel(tracks)
    t=tracks{k}; secondary=generate2ndCode(t.PRN);
    signs=ones(size(t.Pilot_Q_P)); signs(t.Pilot_Q_P<=0)=-1;
    c=xcorr(signs,secondary); c=c((numel(c)+1)/2:end);
    indices=find(abs(c)>=1799.5);
    for first=indices(:)'
        if first+1799>numel(signs), continue; end
        span=first:first+1799;
        if ~all(t.valid(span)) || any(diff(t.generation(span))) || ...
                any(diff(t.epoch(span))~=1) || ...
                any(diff(t.absoluteSample(span))~=t.sampleCount(span(1:end-1)))
            continue
        end
        candidates=candidates+1;
        one=struct('PRN',t.PRN,'I_P',t.I_P(span),'Pilot_Q_P',t.Pilot_Q_P(span));
        % Original reference functions, unchanged. Single frame per call
        % keeps each ephemeris/TOW tied to its frame instead of accumulating
        % the first SF2 with later SF3 pages as the full reference call does.
        [eph,firstSubFrame,TOW]=BCNAV1decoding(one,1,settings);
        if isinf(firstSubFrame), continue; end
        assert(firstSubFrame==1 && TOW==eph.TOW);
        bits=double(one.I_P>0);
        [ok,prnBits]=BCH21_6Decoding(1-2*bits(1:21)); inverted=0;
        if ~ok
            bits=1-bits; inverted=1;
            [ok,prnBits]=BCH21_6Decoding(1-2*bits(1:21));
        end
        assert(ok);
        [ok,sohBits]=BCH51_8Decoding(1-2*bits(22:72)); assert(ok);
        temp=reshape(bits(73:end),36,48); rows3=3:3:35;
        sf2=reshape(temp(setdiff(1:36,rows3),:)',1,1200);
        sf3=reshape(temp(rows3,:)',1,528);
        decoded=[prnBits sohBits sf2(1:600) sf3(1:264)];
        record=struct('prn',t.PRN,'generation',t.generation(first), ...
            'first_epoch',t.epoch(first),'absolute_sample',t.absoluteSample(first), ...
            'secondary_correlation',round(c(first)),'inverted',inverted, ...
            'symbols',char(double(one.I_P>0)+'0'), ...
            'bits',char(decoded+'0'),'ephemeris',eph, ...
            'IODC_corrected',floor(eph.IODC/2));
        if isempty(frames), frames=record; else, frames(end+1)=record; end %#ok<AGROW>
    end
    fprintf('PRN %02d: %d secondary candidates\n',t.PRN,numel(indices));
end
writejson(fullfile(outputDir,'frames.json'),frames);
sources={'BCNAV1decoding.m','BCH21_6Decoding.m','BCH51_8Decoding.m','ephemeris.m','generate2ndCode.m'};
hashes=struct();
for k=1:numel(sources)
    name=sources{k}; hashes.(strrep(name,'.','_'))=hashfile(fullfile(b1cRoot,'include',name));
end
manifest=struct('schema_version',1,'matlab_version',version,'tracking_input',trackingInput, ...
    'reference_source_sha256',hashes,'secondary_candidates',candidates, ...
    'decoded_frames',numel(frames),'ldpc_error_correction',false, ...
    'adaptation','Original navigation called on each complete frame, fresh eph; original source files unchanged', ...
    'IODC_correction','ICD 10-bit IODC = floor(original 11-bit MATLAB value/2)');
if isfolder(trackingInput)
    for k=1:numel(files)
        manifest.tracking_sha256.(erase(files(k).name,'.csv'))=hashfile(fullfile(files(k).folder,files(k).name));
    end
else
    manifest.tracking_mat_sha256=hashfile(trackingInput);
end
writejson(fullfile(outputDir,'manifest.json'),manifest);
fprintf('Navigation baseline complete: %d checked frames\n',numel(frames));
end
function writejson(p,value)
f=fopen(p,'w'); assert(f>=0); cleanup=onCleanup(@() fclose(f)); %#ok<NASGU>
fprintf(f,'%s\n',jsonencode(value,'PrettyPrint',true));
end
function hex=hashfile(p)
f=fopen(p,'rb'); assert(f>=0); cleanup=onCleanup(@() fclose(f)); %#ok<NASGU>
d=java.security.MessageDigest.getInstance('SHA-256');
while ~feof(f), d.update(fread(f,1048576,'*int8')); end
hex=lower(reshape(dec2hex(typecast(d.digest(),'uint8'),2)',1,[]));
end
