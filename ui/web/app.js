"use strict";
const $ = id => document.getElementById(id);
const esc = value => String(value ?? "—").replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const finite = v => typeof v === "number" && Number.isFinite(v);
const num = (v, digits=2) => finite(v) ? v.toLocaleString("zh-CN", {maximumFractionDigits:digits,minimumFractionDigits:digits}) : "—";
const stamp = v => finite(v) ? new Date(v).toLocaleString("zh-CN", {hour12:false}) : "—";
const keyOf = e => `${Math.round(e.freq_hz || 0)}:${e.pci}:${e.ssb}`;
const colors = ["#158b7d", "#548ecb", "#c39343"];
const state = {mode:"live", page:"summary", result:"measure", selected:"", frozen:false, bootstrap:null, data:null, recordings:[], replay:null, replayIndex:0, offsets:[], playing:false, player:null, fetchPending:false, error:"", lastTracePaint:0};

class Plot {
  constructor(id) {
    this.canvas=$(id); this.lines=[]; this.zoom=[0,1]; this.hover=null; this.drag=null;
    const c=this.canvas;
    c.addEventListener("wheel", e => {e.preventDefault(); const rect=c.getBoundingClientRect(), p=Math.max(0,Math.min(1,(e.clientX-rect.left-48)/(rect.width-65))); const [a,b]=this.zoom, width=Math.min(1,Math.max(.015,(b-a)*Math.exp(e.deltaY*.0015))), anchor=a+p*(b-a); let left=Math.max(0,Math.min(1-width,anchor-p*width)); this.zoom=[left,left+width]; this.draw();}, {passive:false});
    c.addEventListener("pointerdown", e => {this.drag={x:e.clientX,zoom:[...this.zoom]}; c.setPointerCapture(e.pointerId);});
    c.addEventListener("pointerup", () => this.drag=null);
    c.addEventListener("pointermove", e => {const rect=c.getBoundingClientRect(); if(this.drag) {const [a,b]=this.drag.zoom, shift=(this.drag.x-e.clientX)/(rect.width-65)*(b-a), left=Math.max(0,Math.min(1-(b-a),a+shift)); this.zoom=[left,left+b-a];} this.hover=e.clientX-rect.left; this.draw();});
    c.addEventListener("pointerleave", () => {this.hover=null;this.draw();});
    c.addEventListener("dblclick", () => this.reset());
    new ResizeObserver(() => this.draw()).observe(c);
  }
  reset() {this.zoom=[0,1];this.draw();}
  set(lines, empty="该事件尚无此阶段数据") {this.lines=lines.filter(Boolean);this.empty=empty;this.draw();}
  draw() {
    const c=this.canvas, w=c.clientWidth, h=c.clientHeight;
    if(!w || !h)return;
    const ratio=window.devicePixelRatio || 1;
    if(c.width!==Math.round(w*ratio)||c.height!==Math.round(h*ratio)){c.width=Math.round(w*ratio);c.height=Math.round(h*ratio);}
    const ctx=c.getContext("2d");ctx.setTransform(ratio,0,0,ratio,0,0);ctx.clearRect(0,0,w,h);
    const left=49,right=w-14,top=16,bottom=h-29;
    let points=this.lines.flatMap(l=>l.x.map((x,i)=>[x,l.y[i]])).filter(([x,y])=>finite(x)&&finite(y));
    if(!points.length){ctx.fillStyle="#a1b0ba";ctx.font="12px system-ui";ctx.textAlign="center";ctx.fillText(this.empty||"暂无数据",w/2,h/2);return;}
    let xmin=Infinity,xmax=-Infinity,ymin=Infinity,ymax=-Infinity;
    for(const [x]of points){xmin=Math.min(xmin,x);xmax=Math.max(xmax,x);} if(xmin===xmax)xmax=xmin+1;
    const span=xmax-xmin, x0=xmin+this.zoom[0]*span,x1=xmin+this.zoom[1]*span;
    for(const [x,y]of points)if(x>=x0&&x<=x1){ymin=Math.min(ymin,y);ymax=Math.max(ymax,y);}
    if(!finite(ymin)){ymin=0;ymax=1;} if(ymin===ymax){ymin-=Math.max(1,Math.abs(ymin)*.1);ymax+=Math.max(1,Math.abs(ymax)*.1);}
    const pad=(ymax-ymin)*.12;ymin-=pad;ymax+=pad;
    const px=x=>left+(x-x0)/(x1-x0)*(right-left), py=y=>bottom-(y-ymin)/(ymax-ymin)*(bottom-top);
    const tick=v=>Math.abs(v)>=10000 || (Math.abs(v)>0&&Math.abs(v)<.001) ? v.toExponential(1) : String(Number(v.toPrecision(4)));
    ctx.font="9px system-ui";ctx.lineWidth=1;
    for(let i=0;i<=4;i++){const y=top+(bottom-top)*i/4;ctx.strokeStyle="#edf1f3";ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(right,y);ctx.stroke();ctx.textAlign="right";ctx.fillStyle="#94a5b0";ctx.fillText(tick(ymax-(ymax-ymin)*i/4),left-8,y+3);}
    for(let i=0;i<=4;i++){const x=left+(right-left)*i/4;ctx.textAlign="center";ctx.fillStyle="#94a5b0";ctx.fillText(tick(x0+(x1-x0)*i/4),x,bottom+17);}
    ctx.save();ctx.beginPath();ctx.rect(left,top,right-left,bottom-top);ctx.clip();
    this.lines.forEach((line,k)=>{ctx.strokeStyle=colors[k%colors.length];ctx.lineWidth=1.4;ctx.beginPath();let open=false;for(let i=0;i<line.x.length;i++){const x=line.x[i],y=line.y[i];if(!finite(x)||!finite(y)){open=false;continue;}if(!open){ctx.moveTo(px(x),py(y));open=true;}else ctx.lineTo(px(x),py(y));}ctx.stroke();if(line.x.length<8){ctx.fillStyle=colors[k%colors.length];for(let i=0;i<line.x.length;i++){if(!finite(line.y[i]))continue;ctx.beginPath();ctx.arc(px(line.x[i]),py(line.y[i]),3.2,0,Math.PI*2);ctx.fill();}}});
    ctx.restore();
    if(this.hover!==null&&this.hover>=left&&this.hover<=right){const target=x0+(this.hover-left)/(right-left)*(x1-x0);let nearest=null;for(const p of points)if(!nearest||Math.abs(p[0]-target)<Math.abs(nearest[0]-target))nearest=p;if(nearest){ctx.strokeStyle="#9bb4bd";ctx.setLineDash([3,3]);ctx.beginPath();ctx.moveTo(px(nearest[0]),top);ctx.lineTo(px(nearest[0]),bottom);ctx.stroke();ctx.setLineDash([]);ctx.textAlign="left";ctx.fillStyle="#496b7c";ctx.fillText(`x ${tick(nearest[0])}  ·  y ${tick(nearest[1])}`,left+6,10);}}
  }
}
const plots=Object.fromEntries(["iq","pss","channel","phase","cir","fraction","cfo","delay"].map(k=>[k,new Plot("chart-"+k)]));

async function api(path) {const response=await fetch(path,{cache:"no-store"});const result=await response.json();if(!response.ok)throw new Error(result.error||"读取失败");return result;}
function stale(e,d=state.data){return !e || !d || d.now_ms-e.time_ms>state.bootstrap.settings.stale_after_ms;}
function validity(e,d) {
  const cfg=d.config||{};
  if(stale(e,d))return {valid:false,reason:"数据过期 / 尚无数据"};
  if(!e.locked)return {valid:false,reason:"未保持 PBCH / MIB 有效锁定"};
  if(cfg.time_model!=="shared")return {valid:false,reason:"接收机含钟差，绝对距离不可用"};
  if(!cfg.absolute_toa_calibrated)return {valid:false,reason:"发射时刻 / 硬件延迟标定未确认"};
  const clock=e.clock_locked ?? (!stale(d.runtime,d)&&d.runtime?.clock_locked);
  if(!clock)return {valid:false,reason:"该事件时钟锁定未确认"};
  if(!e.absolute_candidate_valid || !finite(e.toa_candidate_ns)||e.toa_candidate_ns<0)return {valid:false,reason:"尚无有效发射时间与 TOA 测量"};
  return {valid:true,reason:"满足已声明的绝对 TOA 条件"};
}
function choosePage(page) {
  state.page=page;const names={summary:["Summary","概览与状态"],trace:["SSB Trace","阶段波形"],results:["Results","测量与结果"]};
  for(const k of Object.keys(names))$(k+"-page").classList.toggle("hidden",k!==page);
  document.querySelectorAll(".nav").forEach(b=>b.classList.toggle("active",b.dataset.page===page));
  $("page-title").innerHTML=names[page][0]+" <span>"+names[page][1]+"</span>"; render(true);
}
function render(force=false) {
  const d=state.data;if(!d||!state.bootstrap)return;
  $("clock").textContent=stamp(d.now_ms);
  const demo=!!d.config?.demo, replay=state.mode==="replay";
  $("connection-label").textContent=state.frozen?"画面已冻结":replay?"记录回看":d.connected?"数据已连接":"等待 / 数据已过期";
  $("connection-dot").classList.toggle("online",!!d.connected&&!replay&&!state.frozen&&!demo);
  let notice=state.error || (demo?"演示记录 · 非实测数据。仅用于验证界面和回看流程。":state.frozen?"画面已冻结；接收程序与实验记录继续运行。":replay?"离线浏览已记录的状态与阶段快照，不重新运行 IQ 算法。":!d.connected?"等待接收程序生成新记录；旧数据保留展示并标记过期，不代表当前仍在锁定。":"实时只读监看。切换页面、冻结画面均不控制接收程序。");
  if(d.invalid_lines)notice+=` · 跳过 ${d.invalid_lines} 条不完整格式记录`;
  if(d.reader_skipped_bytes)notice+=` · 实时读取跳过旧积压 ${num(d.reader_skipped_bytes/1048576,1)} MiB，完整保留段可离线回看`;
  if(d.reader_error)notice+=" · 记录读取异常："+d.reader_error;
  $("notice").textContent=notice;$("notice").className="notice"+(demo?" demo":state.error||!d.connected&&!replay?" warning":"");
  $("session-label").textContent=d.run_id||"尚未连接实验";
  const cells=d.cells||[], selected=cells.find(e=>e.cell_key===state.selected)||cells.find(e=>e.cell_key===d.selected)||cells[0];
  if(selected)state.selected=selected.cell_key;
  const options=cells.map(e=>`<option value="${esc(e.cell_key)}">PCI ${e.pci} / SSB ${e.ssb} · ${num(e.freq_hz/1e6,2)} MHz</option>`).join("")||'<option value="">暂无信号</option>';
  if($("cell-select").innerHTML!==options)$("cell-select").innerHTML=options;
  $("cell-select").value=state.selected;
  $("metric-cells").textContent=cells.length;
  $("metric-locked").innerHTML=cells.filter(e=>e.locked&&!stale(e,d)).length+` <em>/ ${cells.length}</em>`;
  $("metric-rate").textContent=finite(d.runtime?.sample_rate_hz)?num(d.runtime.sample_rate_hz/1e6,2)+" MS/s":"—";
  $("metric-dropped").textContent=d.runtime?.dropped??selected?.dropped??0;
  const states=["初始化","等待时钟","搜索同步","已锁定","测量中","重新同步","停止中"];
  $("state-badge").textContent=d.runtime&&!stale(d.runtime,d)?states[d.runtime.state]||"运行中":"无新运行状态";
  const stages=[["采样",!!d.runtime?.rx_reads&&!stale(d.runtime,d)],["时钟",!!d.runtime?.clock_locked&&!stale(d.runtime,d)],["PSS / SSS",!!selected?.pss_metric&&!stale(selected,d)],["PBCH / MIB",!!selected?.locked&&!stale(selected,d)],["时延测量",finite(selected?.relative_delay_ns)&&!stale(selected,d)]];
  $("pipeline").innerHTML=stages.map(([name,ok],i)=>`<div class="stage ${ok?"ok":""}"><b>${ok?"✓":String(i+1).padStart(2,"0")}</b>${name}</div>`).join("");
  $("health").innerHTML=[["接收块数",d.runtime?.rx_reads],["采样间断",d.runtime?.rx_gaps],["同步任务丢弃",d.runtime?.sync_dropped],["环形缓冲溢出",d.runtime?.ring_overruns]].map(([k,v])=>`<div>${k}<strong>${esc(v)}</strong></div>`).join("");
  const cfg=d.config||{},r=d.runtime||{};
  $("context").innerHTML=[["信号来源",cfg.source_mode==="commercial"?"运营商现网":cfg.source_mode?"自建可控锚点":"等待运行确认"],["时间模型",cfg.time_model==="receiver_bias"?"仅基站同步 / 接收机含钟差":cfg.time_model?"收发共同时间基准":"等待运行确认"],["参考 / PPS",`${r.ref_locked?"锁定":"未确认"} / ${r.pps_locked?"锁定":"未确认"}`],["绝对时延标定声明",cfg.absolute_toa_calibrated?"已声明完成":"未确认"],["三维定位","接口预留，暂未实现"]].map(([k,v])=>`<dt>${k}</dt><dd>${esc(v)}</dd>`).join("");
  $("cell-table").innerHTML=cells.map(e=>`<tr class="cell-row" data-cell="${esc(e.cell_key)}"><td class="cell-id">${e.pci} <span class="muted">/ ${e.ssb}</span></td><td>${num(e.freq_hz/1e6,3)} MHz</td><td><span class="badge ${stale(e,d)?"neutral":e.locked?"":"warn"}">${stale(e,d)?"数据过期":e.locked?"已锁定":"候选 / 未锁定"}</span></td><td>${num(e.cfo_hz)}</td><td>${num(e.snr_db)}</td><td>${num(e.pss_metric)}</td><td class="muted">${num(Math.max(0,d.now_ms-e.time_ms)/1000,1)} s 前</td></tr>`).join("")||'<tr><td colspan="7" class="empty-row">暂无信号记录。接收程序启动后会自动更新。</td></tr>';
  $("cell-table").querySelectorAll(".cell-row").forEach(row=>row.onclick=()=>{state.selected=row.dataset.cell;resetPlots();choosePage("trace");refresh();});
  const loaded=cfg.configured||{};
  $("config-view").innerHTML=[["配置中心频率",finite(loaded.center_freq_hz)?num(loaded.center_freq_hz/1e6,3)+" MHz":"—"],["配置采样率",finite(loaded.sample_rate_hz)?num(loaded.sample_rate_hz/1e6,2)+" MS/s":"—"],["配置接收增益",finite(loaded.rx_gain_db)?num(loaded.rx_gain_db,1)+" dB":"—"],["设备",loaded.sdr],["时钟源",loaded.clock_source],["时间源",loaded.time_source],["目标 PCI / SSB",loaded.target_pci===undefined?"—":`${loaded.target_pci} / ${loaded.target_ssb_index}`],["配置扫描目标数",loaded.scan_target_count]].map(([k,v])=>`<div class="config-item"><span>${k}</span><strong>${esc(v)}</strong></div>`).join("");
  $("disk-config").textContent=JSON.stringify(state.bootstrap.disk_config,null,2);
  const series=(d.series||[]).filter(e=>keyOf(e)===state.selected), delays=series.map(e=>e.relative_delay_ns).filter(finite);
  const mean=delays.length?delays.reduce((a,b)=>a+b,0)/delays.length:null;
  const jitter=delays.length?Math.sqrt(delays.reduce((a,b)=>a+(b-mean)**2,0)/delays.length):null;
  $("result-status").textContent=!selected?"尚无选中信号的测量":stale(selected,d)?"数据过期：以下保留最近报告值，不代表当前有效测量。":!selected.locked?"当前信号未保持有效锁定；频偏等值仅作候选诊断，TOA 与距离需单独检查有效性。":`PCI ${selected.pci} / SSB ${selected.ssb} · PBCH/MIB 已确认 · 最近测量 ${stamp(selected.time_ms)}；窗口相对时延不作为传播距离。`;
  $("result-cfo").textContent=num(selected?.cfo_hz)+" Hz";$("result-delay").textContent=num(selected?.relative_delay_ns)+" ns";
  $("result-mean").textContent=num(mean)+" ns";$("result-jitter").textContent=num(jitter)+" ns";
  $("toa-table").innerHTML=cells.map(e=>{const v=validity(e,d);return `<tr><td class="cell-id">${e.pci} / ${e.ssb}</td><td>${num(e.freq_hz/1e6,3)} MHz</td><td>${v.valid?num(e.toa_candidate_ns,3):"—"}</td><td>${v.valid?num(e.toa_candidate_ns*.299792458,3):"—"}</td><td><span class="badge ${v.valid?"":"warn"}">${esc(v.reason)}</span></td></tr>`;}).join("")||'<tr><td colspan="5" class="empty-row">尚无 TOA 记录</td></tr>';
  if(force||performance.now()-state.lastTracePaint>500){
    state.lastTracePaint=performance.now();const trace=d.trace&&keyOf(d.trace)===state.selected?d.trace:null,curves=trace?.curves||{};
    $("trace-caption").textContent=trace?`事件 ${trace.event_id} · ${stamp(trace.time_ms)} · 硬件采样计数 ${trace.hardware_sample}${stale(trace,d)?" · 快照已过期":""}`:"当前条目还没有阶段快照。未完成的阶段不会补造数据。";
    $("trace-facts").innerHTML=[["搜索步长",trace?.pss_step_samp?trace.pss_step_samp+" samples":"本事件未执行全局 PSS 搜索"],["粗定时",trace?.coarse_offset_samp],["跟踪累计移位",trace?.tracking_shift_samp],["CFO / Hz",num(trace?.cfo_hz)],["CIR 峰值索引",trace?.cir_peak_sample],["小数采样点",num(trace?.fractional_offset_samp,4)],["PBCH 失败阶段",trace?.fail_stage]].map(([k,v])=>`<span>${k}<b>${esc(v)}</b></span>`).join("");
    plots.iq.set([curves.iq_i,curves.iq_q]);plots.pss.set([curves.pss_0,curves.pss_1,curves.pss_2]);plots.channel.set([curves.channel_magnitude]);plots.phase.set([curves.channel_phase]);plots.cir.set([curves.cir_magnitude]);plots.fraction.set([curves.fractional_peak]);
    const t0=series[0]?.time_ms||0;
    for(const [plot,field]of [[plots.cfo,"cfo_hz"],[plots.delay,"relative_delay_ns"]])plot.set([{x:series.map(e=>(e.time_ms-t0)/1000),y:series.map(e=>e[field])}],"当前窗口尚无有效测量记录");
  }
}
function resetPlots(){Object.values(plots).forEach(p=>p.reset());}
async function refresh(){
  if(state.mode!=="live"||state.frozen||state.fetchPending||!state.bootstrap)return;
  state.fetchPending=true;
  try{const requested=state.selected;const data=await api("/api/state?cell="+encodeURIComponent(requested));if(state.mode!=="live"||state.frozen||requested!==state.selected)return;state.data=data;state.error="";render();}
  catch(e){state.error="服务连接失败："+e.message;$("connection-label").textContent="服务连接断开";$("connection-dot").classList.remove("online");$("notice").textContent=state.error;$("notice").className="notice warning";}
  finally{state.fetchPending=false;}
}
async function recordings(){const data=await api("/api/recordings");state.recordings=data.recordings;const previous=$("recording-select").value;$("recording-select").innerHTML='<option value="">选择记录文件</option>'+state.recordings.map(r=>`<option value="${esc(r.file)}">${r.config.demo?"[演示] ":""}${r.file} · ${num(r.size/1048576,2)} MiB</option>`).join("");$("recording-select").value=previous;}
function replaySnapshot(){
  const page=state.replay;if(!page)return;
  const events=page.events.slice(0,state.replayIndex+1), cells=new Map(), traces=new Map();let runtime=null;
  for(const e of events){if(e.kind==="runtime")runtime=e;if(e.kind==="sample"){cells.set(keyOf(e),e);if(Object.keys(e.curves||{}).length)traces.set(keyOf(e),e);}}
  const selected=cells.has(state.selected)?state.selected:cells.keys().next().value||"";
  state.selected=selected;state.data={schema:1,run_id:page.config.run_id,now_ms:events.at(-1)?.time_ms||page.config.time_ms,config:page.config,runtime,cells:Array.from(cells,([k,e])=>({...e,cell_key:k})),trace:traces.get(selected),series:events.filter(e=>e.kind==="sample"),selected,connected:false,invalid_lines:page.invalid_lines};
  $("timeline").max=Math.max(0,page.events.length-1);$("timeline").value=state.replayIndex;
  $("replay-position").textContent=`${page.events.length?state.replayIndex+1:0} / ${page.events.length} 条 · ${stamp(state.data.now_ms)} · 文件位置 ${num(page.next_offset/1048576,2)} MiB`;
  $("page-prev").disabled=!state.offsets.length;$("page-next").disabled=page.eof;render(true);
}
async function loadReplay(offset=0){
  const file=$("recording-select").value;if(!file)return;
  stopPlaying();const generation=offset&&state.replay?`&generation=${state.replay.config.time_ms}`:"";state.replay=await api(`/api/replay?file=${encodeURIComponent(file)}&offset=${offset}${generation}`);state.replayIndex=0;state.error="";resetPlots();replaySnapshot();
}
function stopPlaying(){clearTimeout(state.player);state.playing=false;$("play").textContent="▶ 播放";}
function playStep(){
  if(!state.playing||!state.replay)return;
  const events=state.replay.events,idx=state.replayIndex;
  if(idx>=events.length-1){stopPlaying();return;}
  const delay=Math.max(30,Math.min(10000,(events[idx+1].time_ms-events[idx].time_ms)/Number($("speed").value)));
  state.player=setTimeout(()=>{state.replayIndex++;replaySnapshot();playStep();},delay);
}
function setMode(mode){stopPlaying();state.mode=mode;state.frozen=false;$("freeze").textContent="Ⅱ 冻结画面";$("freeze").disabled=mode!=="live";$("live-mode").classList.toggle("selected",mode==="live");$("replay-mode").classList.toggle("selected",mode==="replay");$("replay-bar").classList.toggle("hidden",mode!=="replay");if(mode==="live")refresh();else {recordings().catch(showError);if(state.replay)replaySnapshot();else {state.data={now_ms:Date.now(),cells:[],series:[],config:null,connected:false};render(true);}}}
function showError(error){state.error=error.message;$("notice").textContent=state.error;$("notice").className="notice warning";}
document.querySelectorAll(".nav").forEach(b=>b.onclick=()=>choosePage(b.dataset.page));
document.querySelectorAll("[data-result]").forEach(b=>b.onclick=()=>{state.result=b.dataset.result;document.querySelectorAll("[data-result]").forEach(btn=>btn.classList.toggle("selected",btn===b));for(const k of ["measure","toa","position"])$(k+"-result").classList.toggle("hidden",k!==state.result);render(true);});
$("cell-select").onchange=()=>{state.selected=$("cell-select").value;resetPlots();if(state.mode==="live"){render(true);refresh();}else replaySnapshot();};
$("live-mode").onclick=()=>setMode("live");$("replay-mode").onclick=()=>setMode("replay");
$("freeze").onclick=()=>{state.frozen=!state.frozen;$("freeze").textContent=state.frozen?"▶ 恢复画面":"Ⅱ 冻结画面";if(state.frozen)render();else refresh();};
$("reload-recordings").onclick=()=>recordings().catch(showError);
$("recording-select").onchange=()=>{state.offsets=[];loadReplay().catch(showError);};
$("timeline").oninput=()=>{stopPlaying();state.replayIndex=Number($("timeline").value);replaySnapshot();};
$("step-prev").onclick=()=>{stopPlaying();if(state.replay){state.replayIndex=Math.max(0,state.replayIndex-1);replaySnapshot();}};
$("step-next").onclick=()=>{stopPlaying();if(state.replay){state.replayIndex=Math.min(state.replay.events.length-1,state.replayIndex+1);replaySnapshot();}};
$("page-next").onclick=()=>{if(!state.replay||state.replay.eof)return;state.offsets.push(state.replay.offset);loadReplay(state.replay.next_offset).catch(showError);};
$("page-prev").onclick=()=>{if(state.offsets.length)loadReplay(state.offsets.pop()).catch(showError);};
$("play").onclick=()=>{if(state.playing){stopPlaying();return;}if(!state.replay?.events.length)return;state.playing=true;$("play").textContent="Ⅱ 暂停";playStep();};
$("reset-zoom").onclick=resetPlots;
(async()=>{try{state.bootstrap=await api("/api/bootstrap");await recordings();await refresh();setInterval(refresh,state.bootstrap.settings.poll_interval_ms);}catch(e){showError(e);}})();
