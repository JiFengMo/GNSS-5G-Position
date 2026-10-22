function plot_b1c_tracking(actualDir, referenceDir, outputPng)
% Run from project root: addpath('src/GNSS/tools');
% plot_b1c_tracking('build/gnss-tracking/final_reference', ...
%   'build/gnss-tracking/matlab', 'build/gnss-tracking/tracking_overview.png')
% Outputs a standalone scientific PNG. Next: review validation_tracking_2026-10-21.md.
files = dir(fullfile(actualDir, 'prn_*.csv'));
fig = figure('Visible', 'off', 'Color', 'w', 'Position', [100 100 1500 950]);
cleanup = onCleanup(@() close(fig)); %#ok<NASGU>
layout = tiledlayout(fig, 2, 2, 'Padding', 'compact', 'TileSpacing', 'compact');
title(layout, {'BDS B1C: 60 s continuous tracking / MATLAB comparison', ...
    'Reacquisition disabled for trajectory comparison'});
colors = lines(numel(files));
ax1 = nexttile(layout, 1); hold(ax1, 'on');
ax2 = nexttile(layout, 2); hold(ax2, 'on');
ax3 = nexttile(layout, 3); hold(ax3, 'on');
ax4 = nexttile(layout, 4); hold(ax4, 'on');
states = [];
prns = zeros(1,numel(files));
for k = 1:numel(files)
    data = readtable(fullfile(actualDir, files(k).name));
    reference = readtable(fullfile(referenceDir, files(k).name));
    prn = sscanf(files(k).name, 'prn_%d.csv'); prns(k) = prn;
    time = data.sample_time_s;
    valid = data.valid == 1;
    if any(valid)
        baseline = median(data.doppler_hz(valid));
        plot(ax1, time(valid), data.doppler_hz(valid)-baseline, 'Color', colors(k,:), ...
            'DisplayName', sprintf('PRN %d', prn));
    end
    quality = data.quality_updated == 1;
    plot(ax2, time(quality), data.cn0_db_hz(quality), 'Color', colors(k,:), ...
        'DisplayName', sprintf('PRN %d', prn));
    state = zeros(1,height(data));
    state(valid) = 1;
    state(strcmp(string(data.state),'lost')) = 2;
    states(k,:) = state; %#ok<AGROW>
    plot(ax4, time, data.carrier_hz-reference.carrier_hz, 'Color', colors(k,:));
end
title(ax1, 'Valid-channel Doppler relative to its median'); ylabel(ax1, 'Hz'); xlabel(ax1, 'Record time (s)');
legend(ax1, 'Location', 'best'); grid(ax1, 'on');
title(ax2, 'Moment-based narrowband C/N0 estimate'); ylabel(ax2, 'dB-Hz'); xlabel(ax2, 'Record time (s)');
yline(ax2,25,'--','Threshold','HandleVisibility','off'); legend(ax2, 'Location', 'best'); grid(ax2, 'on');
imagesc(ax3, [0 60], 1:numel(files), states); set(ax3, 'YDir', 'reverse', 'YTick', 1:numel(files), ...
    'YTickLabel', compose('PRN %d',prns));
colormap(ax3, [.72 .75 .78; .12 .65 .36; .85 .22 .20]); clim(ax3,[0 2]);
title(ax3, 'Tracking validity: gray=pull-in, green=valid, red=lost'); xlabel(ax3, 'Record time (s)');
title(ax4, 'Carrier NCO difference: C minus MATLAB'); ylabel(ax4, 'Hz'); xlabel(ax4, 'Record time (s)'); grid(ax4, 'on');
for ax = [ax1 ax2 ax3 ax4], xlim(ax,[0 60]); end
exportgraphics(fig, outputPng, 'Resolution', 150);
fprintf('Saved %s\n', outputPng);
end
