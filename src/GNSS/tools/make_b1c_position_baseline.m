function make_b1c_position_baseline(referenceRoot, trackingInput, outputDir)
% Run original CU-SDR postNavigation, satpos, pseudoranges and least squares.
% Run from project root: addpath('src/GNSS/tools');
% make_b1c_position_baseline(REFERENCE_ROOT, TRACKING_MAT_OR_CSV_DIR, OUTPUT_DIR)
% Outputs: solutions.csv, observations.csv, satellite_vectors.csv,
% navigation.mat, original_console.log, manifest.json. Requires MATLAB's
% original reference dependencies. Next: compare_b1c_position.py; docs/positioning.md.
arguments
    referenceRoot (1,:) char
    trackingInput (1,:) char
    outputDir (1,:) char
end
oldPath=path; cleanup=onCleanup(@() path(oldPath)); %#ok<NASGU>
r=fullfile(referenceRoot,'BDS','B1C');
addpath(r,'-begin'); addpath(fullfile(r,'Common'),'-begin'); addpath(fullfile(r,'include'),'-begin');
if ~isfolder(outputDir), mkdir(outputDir); end
settings=initSettings(); settings.plotNavigation=0;
inputHashes=struct();
if isfolder(trackingInput)
    files=dir(fullfile(trackingInput,'prn_*.csv')); trackResults=struct([]);
    for k=1:numel(files)
        p=fullfile(files(k).folder,files(k).name); t=readtable(p);
        one=struct('PRN',sscanf(files(k).name,'prn_%d.csv'),'status','+', ...
            'I_P',t.data_i_p','Pilot_Q_P',t.pilot_q_p','absoluteSample',t.absolute_sample', ...
            'codeFreq',t.code_rate_hz','remCodePhase',t.rem_code_phase');
        if isempty(trackResults), trackResults=one; else, trackResults(end+1)=one; end %#ok<AGROW>
        inputHashes.(erase(files(k).name,'.csv'))=hashfile(p);
    end
else
    loaded=load(trackingInput,'trackResults','settings');
    trackResults=loaded.trackResults;
    inputHashes.tracking_mat=hashfile(trackingInput);
end
settings.numberOfChannels=numel(trackResults); settings.msToProcess=60000;
fprintf('Original MATLAB navigation: %d channels...\n',numel(trackResults)); tic;
console=evalc('[navSolutions,eph]=postNavigation(trackResults,settings);'); elapsed=toc;
assert(~isempty(navSolutions)&&all(isfinite(navSolutions.X)));
writeText(fullfile(outputDir,'original_console.log'),console);
save(fullfile(outputDir,'navigation.mat'),'navSolutions','eph','settings');
n=navSolutions; count=numel(n.X);
f=fopen(fullfile(outputDir,'solutions.csv'),'w'); assert(f>=0);
fprintf(f,'index,absolute_sample,local_time_before_s,local_time_s,x_m,y_m,z_m,clock_correction_m,latitude_deg,longitude_deg,height_m,gdop,pdop,ecef_xy_dop,ecef_z_dop,tdop,satellites\n');
g=fopen(fullfile(outputDir,'observations.csv'),'w'); assert(g>=0);
fprintf(g,'index,prn,absolute_sample,transmit_time_s,raw_pseudorange_m,satellite_x_m,satellite_y_m,satellite_z_m,satellite_clock_s,clock_corrected_pseudorange_m,receiver_corrected_pseudorange_m,elevation_deg,azimuth_deg\n');
for k=1:count
    active=find(n.PRN(:,k)>0)'; prns=n.PRN(active,k)';
    [xyz,clock]=satpos(n.transmitTime(active,k)',prns,eph);
    bias=n.rawP(active(1),k)+clock(1)*settings.c-n.correctedP(active(1),k);
    before=n.localTime(k)+bias/settings.c;
    values=[k-1 n.currMeasSample(k) before n.localTime(k) n.X(k) n.Y(k) n.Z(k) bias ...
        n.latitude(k) n.longitude(k) n.height(k) n.DOP(:,k)' numel(active)];
    fprintf(f,[repmat('%.17g,',1,numel(values)-1) '%.17g\n'],values);
    for j=1:numel(active)
        a=active(j);
        values=[k-1 prns(j) n.currMeasSample(k) n.transmitTime(a,k) n.rawP(a,k) xyz(:,j)' ...
            clock(j) n.rawP(a,k)+clock(j)*settings.c n.correctedP(a,k) n.el(a,k) n.az(a,k)];
        fprintf(g,[repmat('%.17g,',1,numel(values)-1) '%.17g\n'],values);
    end
end
fclose(f); fclose(g);
% Independent satpos vectors include week crossover and negative tk, plus
% synthetic IGSO derived from real valid ephemeris. GEO is outside this port.
v=fopen(fullfile(outputDir,'satellite_vectors.csv'),'w'); assert(v>=0);
fprintf(v,'prn,satellite_type,transmit_week,transmit_time_s,t_oe,t_oc,frame_tow,x_m,y_m,z_m,clock_s\n');
prns=find(arrayfun(@(e) ~isempty(e.flag)&&e.flag==1,eph));
for prn=prns
    for satType=[3 2]
        testEph=eph; testEph(prn).SatType=char("MEO");
        if satType==2, testEph(prn).SatType='IGSO'; end
        for offset=[-300 -.01 0 .01 18 90 300]
            t=eph(prn).t_oe+offset; [xyz,clock]=satpos(t,prn,testEph);
            values=[prn satType eph(prn).WN t eph(prn).t_oe eph(prn).t_oc eph(prn).TOW xyz' clock];
            fprintf(v,[repmat('%.17g,',1,10) '%.17g\n'],values);
        end
    end
    for edge=1:2
        testEph=eph;
        if edge==1
            testEph(prn).t_oe=604799; testEph(prn).t_oc=604799; testEph(prn).TOW=604799;
            t=.01; week=eph(prn).WN+1;
        else
            testEph(prn).t_oe=0; testEph(prn).t_oc=0; testEph(prn).TOW=0;
            t=604799.99; week=eph(prn).WN-1;
        end
        [xyz,clock]=satpos(t,prn,testEph);
        values=[prn 3 week t testEph(prn).t_oe testEph(prn).t_oc testEph(prn).TOW xyz' clock];
        fprintf(v,[repmat('%.17g,',1,10) '%.17g\n'],values);
    end
end
fclose(v);
sources={'include/postNavigation.m','include/satpos.m','Common/calculatePseudoranges.m', ...
    'Common/leastSquarePos.m','Common/e_r_corr.m','include/topocent.m', ...
    'Common/togeod.m','Common/tropo.m','include/check_t.m'};
hashes=struct();
for k=1:numel(sources), hashes.(matlab.lang.makeValidName(sources{k}))=hashfile(fullfile(r,sources{k})); end
manifest=struct('schema_version',1,'matlab_version',version,'tracking_input',trackingInput, ...
    'tracking_sha256',inputHashes,'reference_source_sha256',hashes,'sample_rate_hz',settings.samplingFreq, ...
    'period_ms',settings.navSolPeriod,'elevation_mask_deg',settings.elevationMask, ...
    'troposphere',settings.useTropCorr,'start_offset_ms',settings.startOffset, ...
    'epochs',count,'prns',prns,'elapsed_s',elapsed,'adaptation','No original reference source modified');
writeText(fullfile(outputDir,'manifest.json'),jsonencode(manifest,'PrettyPrint',true));
fprintf('MATLAB baseline complete: %d fixes in %.1f s\n',count,elapsed);
end
function writeText(p,s)
f=fopen(p,'w'); assert(f>=0); c=onCleanup(@() fclose(f)); %#ok<NASGU>
fprintf(f,'%s\n',s);
end
function hex=hashfile(p)
f=fopen(p,'rb'); assert(f>=0); c=onCleanup(@() fclose(f)); %#ok<NASGU>
d=java.security.MessageDigest.getInstance('SHA-256');
while ~feof(f), d.update(fread(f,1048576,'*int8')); end
hex=lower(reshape(dec2hex(typecast(d.digest(),'uint8'),2)',1,[]));
end
