function plot_b1c_position(inputDir, outputPath)
% Plot real offline SPP stability, not absolute accuracy against antenna truth.
% Run: addpath('src/GNSS/tools');
% plot_b1c_position('build/gnss-position/verified_automatic_60', ...
%                   'build/gnss-position/position_overview.png')
% Output: standalone PNG. Next: inspect exclusions/residuals and obtain an
% antenna reference position for absolute error analysis; docs/positioning.md.
t=readtable(fullfile(inputDir,'solutions.csv'));
valid=t.valid==1; assert(any(valid));
xyz=[t.x_m t.y_m t.z_m]; centre=median(xyz(valid,:),1);
lat=median(t.latitude_deg(valid))*pi/180; lon=median(t.longitude_deg(valid))*pi/180;
r=[-sin(lon) cos(lon) 0; -sin(lat)*cos(lon) -sin(lat)*sin(lon) cos(lat); ...
    cos(lat)*cos(lon) cos(lat)*sin(lon) sin(lat)];
enu=(xyz-centre)*r'; seconds=(t.absolute_sample-t.absolute_sample(1));
run=jsondecode(fileread(fullfile(inputDir,'run.json'))); seconds=seconds/run.sample_rate_hz;
fig=figure('Visible','off','Color','w','Position',[100 100 1250 820]); cleanup=onCleanup(@() close(fig)); %#ok<NASGU>
tl=tiledlayout(2,2,'Padding','compact','TileSpacing','compact');
nexttile; scatter(enu(valid,1),enu(valid,2),20,seconds(valid),'filled'); axis equal; grid on;
xlabel('East relative to median (m)'); ylabel('North relative to median (m)'); cb=colorbar; cb.Label.String='Time (s)';
title('Horizontal scatter (all valid fixes)');
nexttile; plot(seconds,enu,'LineWidth',1); grid on; xlabel('Time since first solution (s)'); ylabel('Relative position (m)');
legend('E','N','U','Location','best'); title('ENU offsets; initialization retained');
nexttile; plot(seconds,t.residual_rms_m,'LineWidth',1); grid on;
xlabel('Time (s)'); ylabel('Postfit pseudorange RMS (m)'); title('Residuals do not establish absolute accuracy');
nexttile; yyaxis left; stairs(seconds,t.satellites,'LineWidth',1); ylabel('Used satellites'); ylim([0 max(t.satellites)+1]);
yyaxis right; plot(seconds,t.pdop,'LineWidth',1); ylabel('PDOP'); grid on; xlabel('Time (s)');
title('5 degree elevation mask after initialization');
title(tl,sprintf('B1C offline SPP: %d/%d valid epochs | no surveyed antenna truth',sum(valid),height(t)));
[folder,~,~]=fileparts(outputPath); if ~isempty(folder)&&~isfolder(folder), mkdir(folder); end
exportgraphics(fig,outputPath,'Resolution',150);
end
