"use strict";
(() => {
  const g={on:false,page:"summary",tab:"position",runs:[],run:"",prn:0,time:0,window:30,
    data:null,playing:false,frozen:false,timer:null,debounce:null,version:0,frameOffset:0,framePage:null,frameKey:null,reference:null,
    live:document.body.dataset.bdsSource==="live",liveTimer:null};
  const stateNames={pull_in:"正在拉入",locked:"跟踪锁定",lost:"已失锁"};
  const reasons={below_elevation_mask:"低于仰角门限",tracking_unavailable:"跟踪不可用",no_continuous_anchor:"等待新连续帧",
    health_or_integrity:"健康 / 完整性未通过",stale_ephemeris:"星历或钟差过期",unsupported_geo:"GEO 暂不支持",invalid_time_model:"时间模型无效",invalid_satellite_model:"卫星模型无效"};
  const eventNames={pull_in:"环路拉入",locked:"锁定确认",lost:"失锁",generation_reset:"新跟踪代次",sample_gap:"采样 / 历元间断",reacquire_ok:"重捕获成功",reacquire_failed:"重捕获失败"};
  const seconds=v=>finite(v)?num(v,3)+" s":"—";
  const cell=(v,d=2)=>finite(v)?num(v,d):"—";
  const badge=(text,ok=false)=>`<span class="badge ${ok?"":"neutral"}">${esc(text)}</span>`;
  const prnName=p=>"C"+String(p).padStart(2,"0");
  const panel=(title,body,tag="")=>`<article class="panel"><div class="panel-heading"><h2>${title}</h2>${tag}</div>${body}</article>`;
  const chart=(id,title,units,legend="")=>`<article class="panel chart-panel"><h3>${title}<small>${units}</small></h3><canvas id="g-chart-${id}" aria-label="${title}"></canvas>${legend?`<div class="legend">${legend}</div>`:""}</article>`;
  const fact=(label,value)=>`<div class="config-item"><span>${label}</span><strong>${esc(value)}</strong></div>`;
  const metric=(id,label,small)=>`<article class="metric"><span>${label}</span><strong id="g-${id}">—</strong><small>${small}</small></article>`;
  $("gnss-shell").innerHTML=`
    <section class="toolbar gnss-toolbar"><button id="g-live-mode">实时监看</button><button id="g-offline-mode">离线回看</button>
      <label>实验 <select id="g-run" aria-label="北斗实验"><option value="">暂无导入实验</option></select></label>
      <label>卫星 <select id="g-prn" aria-label="北斗卫星"><option value="">—</option></select></label>
      <button id="g-freeze">Ⅱ 冻结画面</button><button id="g-reload" class="subtle">↻ 刷新实验</button>
    </section>
    <section class="replay-bar gnss-replay"><div class="replay-controls"><button id="g-start" title="记录起点">|‹</button><button id="g-prev" title="上一显示历元">‹</button><button id="g-play">▶ 播放</button><button id="g-next" title="下一显示历元">›</button><button id="g-end" title="记录终点">›|</button><select id="g-speed" aria-label="北斗回看速度"><option value="1">1×</option><option value="2">2×</option><option value="5">5×</option></select></div>
      <label>记录时间 <input id="g-time-number" type="number" min="0" step="0.01" value="0" aria-label="北斗记录时间"> s</label>
      <input id="g-time" type="range" min="0" max="1" step="0.01" value="0" aria-label="北斗时间轴"><span id="g-time-label">选择一个实验开始回看</span>
    </section>
    <div id="g-notice" class="notice">选择已导入的北斗实验。回看读取处理结果，不重新计算 IQ。</div>
    <main>
    <section id="g-summary-page">
      <div class="metrics">${metric("captured","捕获结果","整次捕获处理摘要")}${metric("tracked","当前跟踪有效","按所选记录时刻判断")}${metric("nav-ready","完整电文就绪","同一连续段内已收齐并校验")}${metric("used","定位用星","最近定位历元的实际用星数")}</div>
      <div class="summary-grid">${panel("处理状态",'<div id="g-pipeline" class="pipeline"></div><div class="health-grid" id="g-health"></div>','<span id="g-source-badge" class="badge neutral">离线记录</span>')}${panel("记录与时间",'<dl id="g-context"></dl><p id="g-time-model" class="fine"></p>')}</div>
      ${panel("各星载噪比 C/N₀",'<div id="g-cn0-bars" class="cn0-bars"></div><p class="fine">单位 dB-Hz；未形成有效估计时留空。颜色区分当前定位用星、未用星和失锁。</p>')}
      ${panel("卫星状态",'<div class="table-scroll"><table><thead><tr><th>卫星</th><th>捕获</th><th>跟踪</th><th>C/N₀ · dB-Hz</th><th>多普勒 · Hz</th><th>完整电文</th><th>参与定位 / 原因</th><th>代次</th></tr></thead><tbody id="g-satellite-table"></tbody></table></div>','<span class="muted">点击卫星进入单星跟踪</span>')}
      ${panel("处理参数",'<div class="config-grid" id="g-config"></div><details><summary>实验来源、输入校验摘要与处理设置</summary><pre id="g-metadata"></pre></details>')}
    </section>
    <section id="g-trace-page" class="hidden">
      <article class="panel trace-intro"><div><span class="eyebrow">B1C CHANNEL INSPECTOR</span><h2 id="g-trace-title">单星跟踪</h2><p class="muted" id="g-trace-caption">选择卫星查看真实相关器与环路输出</p></div><div class="trace-actions"><label>历史窗口 <select id="g-window"><option value="10">最近 10 秒</option><option value="30" selected>最近 30 秒</option><option value="120">最近 120 秒</option></select></label><button id="g-reset-zoom">重置图表缩放</button></div></article>
      <div class="fact-strip" id="g-trace-facts"></div>
      <div class="charts-grid">${chart("cn0","C/N₀","记录相对时间 · s / dB-Hz")}${chart("doppler","多普勒","记录相对时间 · s / Hz；虚线为跟踪事件")}${chart("dll","DLL 误差","记录相对时间 · s / 码环鉴别器输出")}${chart("pll","PLL 误差","记录相对时间 · s / cycles")}${chart("data","数据路 Prompt I/Q","记录相对时间 · s / 相关累加值","<i class=teal></i>I <i class=blue></i>Q")}${chart("pilot","导频路 Prompt I/Q","记录相对时间 · s / 相关累加值","<i class=teal></i>I <i class=blue></i>Q")}${chart("epl","当前 E/P/L 相关幅度","主码偏移 · chip / 复相关幅度；仅三个实际抽头","<i class=teal></i>数据路 <i class=blue></i>导频路")}${chart("residual-frequency","残余频率诊断","记录相对时间 · s / Hz")}</div>
      <p class="fine">滚轮缩放、拖动平移、双击恢复。间断与代次变化处断线。捕获完整相关曲面、原始 IQ 波形未记录时不补造。</p>
      ${panel("跟踪事件",'<div class="table-scroll"><table><thead><tr><th>记录时间</th><th>事件</th><th>代次</th><th>采样计数</th></tr></thead><tbody id="g-events"></tbody></table></div>')}
    </section>
    <section id="g-results-page" class="hidden">
      <div class="result-tabs"><button data-gresult="position" class="selected">定位结果</button><button data-gresult="observations">逐星观测</button><button data-gresult="navigation">电文与星历</button></div>
      <div id="g-position-result">
        <p id="g-position-status" class="result-caption"></p>
        <div class="metrics">${metric("latitude","纬度 · °","WGS84 椭球表达")}${metric("longitude","经度 · °","未做地图坐标偏移")}${metric("height","椭球高 · m","与海拔高度有区别")}${metric("pvt-state","解算状态","需结合用星数、残差和数据来源")}</div>
        <div id="g-position-facts" class="fact-strip"></div>
        <details class="panel reference-panel"><summary>显示参考点：默认首个有效定位解，可填写已知天线坐标</summary><div class="reference-inputs"><label>纬度 ° <input id="g-ref-lat" type="number" step="any" min="-90" max="90"></label><label>经度 ° <input id="g-ref-lon" type="number" step="any" min="-180" max="180"></label><label>椭球高 m <input id="g-ref-height" type="number" step="any"></label><button id="g-ref-apply">应用已知参考点</button><button id="g-ref-reset">恢复首个有效解</button></div><p id="g-reference-label" class="fine"></p></details>
        <div class="charts-grid">${chart("scatter","水平 ENU 散布","E / m，N / m；等比例坐标")}${chart("enu","ENU 随时间变化","记录相对时间 · s / m","<i class=teal></i>E <i class=blue></i>N <i class=amber></i>U")}${chart("height","椭球高随时间变化","记录相对时间 · s / m")}${chart("rms","后验伪距残差 RMS","记录相对时间 · s / m")}${chart("dop","几何精度因子","记录相对时间 · s / DOP","<i class=teal></i>PDOP <i class=blue></i>HDOP <i class=amber></i>VDOP")}<article class="panel chart-panel"><h3>卫星天空图<small>北向上，方位角顺时针；仅绘制本定位历元实际保存的角度</small></h3><canvas id="g-sky" aria-label="北斗卫星天空图"></canvas><div class="legend"><i class=teal></i>参与定位 <i class=amber></i>当前未用</div><p class="fine" id="g-sky-caption"></p></article></div>
      </div>
      <div id="g-observations-result" class="hidden">${panel("当前定位历元的逐星观测",'<p class="fine">原始码伪距包含接收机钟差；钟差修正后的码观测也不等同于无误差几何距离。</p><div class="table-scroll"><table><thead><tr><th>卫星</th><th>定位使用</th><th>仰角 °</th><th>原始伪距 m</th><th>卫星钟差改正后 m</th><th>接收机钟差改正后 m</th><th>后验残差 m</th><th>卫星钟差 μs</th></tr></thead><tbody id="g-observations"></tbody></table></div>')}${panel("排除原因",'<div id="g-exclusions" class="exclusion-list"></div>')}</div>
      <div id="g-navigation-result" class="hidden">${panel("当前卫星的已收齐电文",'<div class="table-scroll"><table><thead><tr><th>帧首时间</th><th>收齐时间</th><th>代次</th><th>BCH / CRC 状态</th><th>SF2 CRC</th><th>SF3 CRC</th><th>页号</th></tr></thead><tbody id="g-frames"></tbody></table></div><div class="frame-pager"><button id="g-frame-prev">上一页</button><span id="g-frame-page"></span><button id="g-frame-next">下一页</button></div>','<span class="muted">选帧查看星历；未来帧不提前展示</span>')}${panel("所选帧星历",'<div class="config-grid" id="g-ephemeris"></div><details><summary>完整星历参数与解码信息</summary><pre id="g-ephemeris-json"></pre></details>')}</div>
    </section>
    <section id="g-empty" class="panel hidden"><h2>还没有导入北斗实验</h2><p class="fine">在终端运行 ui/import_gnss.py 导入同一次实验的跟踪、电文和定位目录，然后点击“刷新实验”。仅有跟踪结果时也可导入，电文和定位区域会保持空白。</p><pre>python3 ui/import_gnss.py --tracking-dir log/gnss/tracking-01 \\\n  --navigation-dir log/gnss/navigation-01 --position-dir log/gnss/position-01 \\\n  --output-dir log/gnss-ui/experiment-01</pre></section>
    </main>`;
  const plotIds=["cn0","doppler","dll","pll","data","pilot","epl","residual-frequency","scatter","enu","height","rms","dop"];
  const gp=Object.fromEntries(plotIds.map(k=>[k,new Plot("g-chart-"+k)]));gp.scatter.equal=true;

  function stop(){clearTimeout(g.timer);g.playing=false;$("g-play").textContent="▶ 播放";}
  function emptyRows(id,n,message){$(id).innerHTML=`<tr><td class="empty-row" colspan="${n}">${esc(message)}</td></tr>`;}
  function error(e){stop();$("g-notice").textContent="北斗记录读取失败："+e.message;$("g-notice").className="notice warning";$("connection-label").textContent="记录不可用";}
  function controls(){
    const m=g.data?.run;
    for(const id of ["g-time","g-time-number","g-prn","g-window","g-start","g-prev","g-next","g-end","g-play"])$(id).disabled=!m||g.frozen;
    $("g-run").disabled=g.frozen;$("g-freeze").disabled=!m;
    $("g-run").closest("label").classList.toggle("hidden",g.live);
    document.querySelector(".gnss-replay").classList.toggle("hidden",g.live);
    $("g-live-mode").classList.toggle("selected",g.live);$("g-offline-mode").classList.toggle("selected",!g.live);
    $("g-freeze").textContent=g.frozen?"▶ 恢复画面":"Ⅱ 冻结画面";
    if(m){for(const id of ["g-time","g-time-number"]){$(id).min=m.start_s;$(id).max=m.end_s;$(id).value=String(g.time);}$("g-time-label").textContent=`${seconds(g.time)} / ${seconds(m.end_s)} · 记录相对采样时间`;}
  }
  function choosePage(page){
    g.page=page;const names={summary:["Summary","北斗概览"],trace:["Signal Trace","单星跟踪"],results:["Results","导航与定位"]};
    for(const k of Object.keys(names))$("g-"+k+"-page").classList.toggle("hidden",k!==page||!g.data);
    document.querySelectorAll(".nav").forEach(b=>b.classList.toggle("active",b.dataset.page===page));
    $("page-title").innerHTML=names[page][0]+" <span>"+names[page][1]+"</span>";
    render();if(page==="results"&&g.tab==="navigation")loadFrames().catch(error);
  }
  async function loadRuns(){
    if(g.live){await request(true);return;}
    const previous=g.run;
    const result=await api("/api/gnss/runs");g.runs=result.runs;
    $("g-run").innerHTML=g.runs.map(r=>`<option value="${esc(r.run_id)}">${esc(r.name)}</option>`).join("")||'<option value="">暂无导入实验</option>';
    g.run=g.runs.some(r=>r.run_id===g.run)?g.run:g.runs[0]?.run_id||"";$("g-run").value=g.run;
    if(previous!==g.run)g.prn=0;
    if(g.run)await request(!g.data||previous!==g.run);else{g.data=null;render();}
  }
  async function request(initial=false){
    if(!g.on||(!g.run&&!g.live)||g.frozen)return;
    const version=++g.version, query=new URLSearchParams({run:g.run,window:g.window});
    if(!initial&&!g.live)query.set("time",g.time);if(g.prn)query.set("prn",g.prn);
    let d;try{d=await api((g.live?"/api/gnss/live?":"/api/gnss/state?")+query);}catch(e){
      if(!g.live||!query.has("prn")||e.message!=="PRN not present in current session")throw e;
      query.delete("prn");d=await api("/api/gnss/live?"+query);
    }
    if(!g.on||version!==g.version||g.frozen)return;
    if(d.waiting){g.data=null;render();return;}
    if(g.live&&g.data?.run.run_id!==d.run.run_id){g.reference=null;g.frameKey=null;resetPlots();}
    const sameChannel=g.live&&g.data?.run.run_id===d.run.run_id&&g.data?.selected_prn===d.selected_prn;
    g.data=d;g.time=d.cursor_s;g.prn=d.selected_prn;if(!sameChannel){g.frameOffset=0;g.framePage=null;}
    $("g-prn").innerHTML=d.run.prns.map(p=>`<option value="${p}">${prnName(p)}</option>`).join("");$("g-prn").value=g.prn;
    render();if(g.page==="results"&&g.tab==="navigation")await loadFrames();
  }
  async function seek(value){stop();g.frameKey=null;g.time=Number(value);if(!finite(g.time))return;await request();}
  function resetPlots(){Object.values(gp).forEach(p=>p.reset());}
  async function activate(){
    g.on=true;g.frozen=false;stopPlaying();
    $("nr-shell").classList.add("hidden");$("gnss-shell").classList.remove("hidden");
    $("signal-5g").classList.remove("selected");$("signal-gnss").classList.add("selected");
    $("signal-eyebrow").textContent="BEIDOU B1C / OFFLINE OBSERVATION";$("brand-mark").innerHTML="BD<span>·</span>";
    document.querySelector('.nav[data-page="trace"]').innerHTML='<span>⌁</span>Signal Trace<small>单星跟踪</small>';
    choosePage(g.page);try{if(g.data)render();await loadRuns();}catch(e){error(e);}scheduleLive();
  }
  function deactivate(){
    stop();g.on=false;++g.version;clearTimeout(g.debounce);clearTimeout(g.liveTimer);
    $("gnss-shell").classList.add("hidden");$("nr-shell").classList.remove("hidden");
    $("signal-gnss").classList.remove("selected");$("signal-5g").classList.add("selected");
    $("signal-eyebrow").textContent="5G NR / SIGNAL INTELLIGENCE";$("brand-mark").innerHTML="NR<span>·</span>";
    document.querySelector('.nav[data-page="trace"]').innerHTML='<span>⌁</span>SSB Trace<small>阶段波形</small>';
    choosePageNR();
  }
  function choosePageNR(){choosePageOriginal();}
  // app.js owns the unchanged 5G state and its handlers.
  const choosePageOriginal=()=>window.GNSS.active()?null:choosePage5G(state.page);
  const choosePage5G=window.choosePage;

  function drawSky(satellites){
    const canvas=$("g-sky"),w=canvas.clientWidth,h=canvas.clientHeight;if(!w||!h)return;
    const ratio=devicePixelRatio||1;canvas.width=w*ratio;canvas.height=h*ratio;
    const ctx=canvas.getContext("2d");ctx.scale(ratio,ratio);const cx=w/2,cy=h/2+5,r=Math.min(w/2-35,h/2-25);
    ctx.font="11px system-ui";ctx.textAlign="center";
    for(const f of [1,2/3,1/3]){ctx.strokeStyle="#e2eaee";ctx.beginPath();ctx.arc(cx,cy,r*f,0,2*Math.PI);ctx.stroke();}
    ctx.strokeStyle="#e2eaee";ctx.beginPath();ctx.moveTo(cx-r,cy);ctx.lineTo(cx+r,cy);ctx.moveTo(cx,cy-r);ctx.lineTo(cx,cy+r);ctx.stroke();
    ctx.fillStyle="#8196a4";ctx.fillText("N",cx,cy-r-9);ctx.fillText("S",cx,cy+r+17);ctx.fillText("E",cx+r+16,cy+4);ctx.fillText("W",cx-r-17,cy+4);
    let shown=0;
    for(const s of satellites){const o=s.observation;if(!finite(o?.elevation_deg)||!finite(o?.azimuth_deg)||o.elevation_deg<0)continue;
      const rad=r*(90-Math.min(90,o.elevation_deg))/90,a=o.azimuth_deg*Math.PI/180,x=cx+rad*Math.sin(a),y=cy-rad*Math.cos(a);
      ctx.fillStyle=s.used?"#158b7d":"#c39343";ctx.beginPath();ctx.arc(x,y,6,0,Math.PI*2);ctx.fill();ctx.fillText(prnName(s.prn),x,y-11);++shown;}
    $("g-sky-caption").textContent=shown?`本定位历元保存了 ${shown} 颗卫星的角度；未保存角度的卫星不补画。`:"本时刻没有保存的卫星角度。";
  }
  new ResizeObserver(()=>{if(g.on&&g.data)drawSky(g.data.satellites);}).observe($("g-sky"));
  function reference(){return g.reference||g.data?.run.origin;}
  function enu(r,ref){
    if(!ref||!r.valid||![r.x_m,r.y_m,r.z_m].every(finite))return [null,null,null];
    const lat=ref.latitude_deg*Math.PI/180,lon=ref.longitude_deg*Math.PI/180,dx=r.x_m-ref.x_m,dy=r.y_m-ref.y_m,dz=r.z_m-ref.z_m;
    return [-Math.sin(lon)*dx+Math.cos(lon)*dy,-Math.sin(lat)*Math.cos(lon)*dx-Math.sin(lat)*Math.sin(lon)*dy+Math.cos(lat)*dz,
      Math.cos(lat)*Math.cos(lon)*dx+Math.cos(lat)*Math.sin(lon)*dy+Math.sin(lat)*dz];
  }
  function renderPosition(d){
    const p=d.position,ok=!!(d.position_current&&p?.valid),ref=reference();
    $("g-latitude").textContent=ok?cell(p.latitude_deg,8):"—";$("g-longitude").textContent=ok?cell(p.longitude_deg,8):"—";$("g-height").textContent=ok?cell(p.height_m,3):"—";
    $("g-pvt-state").textContent=ok?"有效解":p&&!p.valid?"无有效解":"本时刻无解";
    $("g-position-status").textContent=p?`定位历元 ${seconds(p.time_s)} · ${ok?(g.live?"在线因果解":"离线回溯解"):!d.position_current?"当前数据滞后或过期，坐标留空":p.status} · ${g.live?"处理可用时刻":"完整电文依赖收齐于"} ${seconds(p.available_s)}`:"尚无定位结果。跟踪锁定不等同于定位成功。";
    $("g-position-facts").innerHTML=[["用星",ok?p.satellites:"—"],["PDOP",ok?cell(p.pdop,3):"—"],["残差 RMS / m",ok?cell(p.residual_rms_m,3):"—"],["钟差修正 / m",ok?cell(p.clock_correction_m,3):"—"],
      ["ECEF X / m",ok?cell(p.x_m,3):"—"],["ECEF Y / m",ok?cell(p.y_m,3):"—"],["ECEF Z / m",ok?cell(p.z_m,3):"—"]].map(([k,v])=>`<span>${k}<b>${esc(v)}</b></span>`).join("");
    $("g-reference-label").textContent=g.reference?"相对用户填写的已知参考位置显示 ENU 偏差；请确保参考位置与数据来源一致。":"当前显示相对首个有效定位解的散布，不是绝对定位误差。初始化历元保留。";
    const history=d.position_history,xyz=history.map(r=>enu(r,ref)),t=history.map(r=>r.time_s),line=k=>({x:t,y:xyz.map(v=>v[k])});
    gp.scatter.set([{x:xyz.map(v=>v[0]),y:xyz.map(v=>v[1]),scatter:true}],"尚无有效定位轨迹");gp.enu.set([line(0),line(1),line(2)]);
    for(const [chart,field]of [[gp.height,"height_m"],[gp.rms,"residual_rms_m"]])chart.set([{x:t,y:history.map(r=>r.valid?r[field]:null)}]);
    gp.dop.set(["pdop","hdop","vdop"].map(field=>({x:t,y:history.map(r=>r.valid?r[field]:null)})));
    drawSky(d.satellites);
  }
  function renderFrames(){
    const d=g.data;if(!d)return;const list=g.framePage?.frames||d.frames;
    const total=g.framePage?.total??list.length;
    $("g-frame-prev").disabled=g.frozen||g.frameOffset===0;$("g-frame-next").disabled=g.frozen||g.frameOffset+20>=total;
    $("g-frame-page").textContent=`${prnName(g.prn)} · ${total} 个已收齐候选 · 第 ${Math.floor(g.frameOffset/20)+1} 页`;
    const key=f=>`${f.generation}:${f.first_epoch}`;let selected=list.find(f=>key(f)===g.frameKey)||list[0];g.frameKey=selected?key(selected):null;
    $("g-frames").innerHTML=list.map((f,i)=>`<tr class="g-frame-row ${key(f)===g.frameKey?"selected-row":""}" data-frame="${i}"><td>${seconds(f.time_s)}</td><td>${seconds(f.available_s)}</td><td>${esc(f.generation)}</td><td>${badge(f.status,!!f.valid)}</td><td>${f.crc2_ok?"通过":"未通过 / 未检查"}</td><td>${f.crc3_ok?"通过":"未通过 / 未检查"}</td><td>${esc(f.ephemeris?.PageID??"—")}</td></tr>`).join("");
    if(!list.length)emptyRows("g-frames",7,"本时刻该卫星还没有收齐的电文帧");
    document.querySelectorAll(".g-frame-row").forEach(row=>row.onclick=()=>{g.frameKey=key(list[Number(row.dataset.frame)]);renderFrames();});
    const e=selected?.ephemeris;
    const items=e?[["PRN",e.PRN],["BDT 周号",e.WN],["帧 TOW / s",e.TOW],["IODE / IODC",`${e.IODE} / ${e.IODC}`],["卫星类型",{1:"GEO",2:"IGSO",3:"MEO"}[e.SatType]||"未知"],
      ["HS / DIF / SIF / AIF",`${e.HS} / ${e.DIF} / ${e.SIF} / ${e.AIF}`],["toe / toc · s",`${e.t_oe} / ${e.t_oc}`],["a₀ / s",e.a_0]]:[];
    $("g-ephemeris").innerHTML=items.map(([k,v])=>fact(k,v)).join("")||'<p class="fine">尚无该帧的有效星历。</p>';
    $("g-ephemeris-json").textContent=selected?JSON.stringify(selected,null,2):"尚无完整电文";
  }
  async function loadFrames(){
    if(!g.on||!g.data||g.frozen)return;const version=g.version,query=new URLSearchParams({run:g.run,time:g.time,prn:g.prn,offset:g.frameOffset});
    const result=await api((g.live?"/api/gnss/live/frames?":"/api/gnss/frames?")+query);if(g.on&&version===g.version&&!g.frozen){g.framePage=result;renderFrames();}
  }
  function render(){
    if(!g.on)return;controls();const d=g.data;$("g-empty").classList.toggle("hidden",!!d);
    for(const p of ["summary","trace","results"])$("g-"+p+"-page").classList.toggle("hidden",!d||p!==g.page);
    $("connection-dot").classList.remove("online");$("connection-label").textContent=g.frozen?"北斗画面已冻结":"北斗离线回看";
    const empty=$("g-empty");
    empty.querySelector('h2').textContent=g.live?"等待北斗 B210 实采记录":"还没有导入北斗实验";
    empty.querySelector('p').textContent=g.live?"请在终端执行 sh run_BDS_only.sh。设备、外部参考和 PPS 检查完成后，采集和处理状态会在这里更新。没有信号时不显示锁定或坐标。":"在终端运行 ui/import_gnss.py 导入同一次实验的跟踪、电文和定位目录，然后点击刷新实验。";
    empty.querySelector('pre').classList.toggle('hidden',g.live);
    $("signal-eyebrow").textContent=g.live?"BEIDOU B1C / USRP LIVE RX":"BEIDOU B1C / OFFLINE OBSERVATION";
    if(!d){$("g-notice").textContent=g.live?"等待本次 B210 采样与处理记录。":"选择已导入的北斗实验。回看读取处理结果，不重新计算 IQ。";$("clock").textContent="—";$("session-label").textContent=g.live?"等待北斗实采":"北斗实验尚未导入";$("connection-label").textContent=g.live?"等待 B210":"北斗离线回看";return;}
    const m=d.run,c=d.counts,s=d.satellites.find(s=>s.prn===g.prn),e=s?.tracking;
    $("clock").textContent=seconds(d.cursor_s);$("session-label").textContent=m.name+(g.live?" · B210 实采":" · 离线后处理");
    const incomplete=[m.tracking,m.navigation,m.positioning].filter(Boolean).some(r=>r.complete===false||r.status_code);
    $("g-notice").textContent=(g.frozen?"画面已冻结。 ":"")+`离线回溯结果 · ${m.name} · 时间轴按记录相对采样时间。定位可能早于电文收齐时刻；无天线真值时轨迹仅表示散布。`+(incomplete?" 本实验存在未完整完成的处理阶段，请结合状态诊断。":"");
    $("g-notice").className="notice"+(incomplete?" warning":"");
    $("g-source-badge").textContent=g.live?"B210 实采":"离线记录";
    $("g-time-model").textContent=g.live?"采样时间来自本次硬件计数起点；外部 PPS 不自动提供 UTC/BDT 日期。在线定位只使用已经收齐的电文。":"定位为离线回溯结果；位置采样时刻可能早于电文完整接收时刻。";
    if(g.live){
      const r=d.live,stages={searching:"捕获 / 重捕获",tracking:"连续处理",stopped:"已停止",error:"设备 / 处理异常"};
      $("connection-label").textContent=g.frozen?"北斗画面已冻结":r.connected?(r.current?"B210 实采已连接":"处理结果滞后"):"实采已停止 / 记录过期";
      $("connection-dot").classList.toggle("online",!!r.connected&&!g.frozen);
      $("g-notice").textContent=(g.frozen?"画面已冻结；采样和处理继续。 ":"")+`B210 实采 · ${stages[r.stage]||r.stage||"等待采样"} · 处理延迟 ${seconds(r.lag_s)} · RX 间断 ${r.rx_gaps||0} · UHD 溢出 ${r.rx_overflows||0} · 队列丢弃样本 ${r.queue_dropped_samples||0}。`+(!r.current?" 当前结果滞后或过期，不显示当前锁定及定位坐标。":"")+(r.error?" "+r.error:"");
      $("g-notice").className="notice"+(r.current?"":" warning");
    }
    $("g-captured").textContent=c.captured??"—";$("g-tracked").textContent=`${c.tracked} / ${m.prns.length}`;$("g-nav-ready").textContent=`${c.navigation_ready} / ${m.prns.length}`;$("g-used").textContent=c.used;
    $("g-pipeline").innerHTML=[["捕获摘要",c.captured>0],["连续跟踪",c.tracked>0],["完整电文",c.navigation_ready>0],[g.live?"在线定位":"离线定位",c.used>=4]].map(([k,v],i)=>`<div class="stage ${v?"ok":""}"><b>${v?"✓":i+1}</b>${k}</div>`).join("");
    $("g-health").innerHTML=[["跟踪通道",m.prns.length],["已收齐有效帧",c.received_frames],["实验有效解",m.valid_fixes],["当前时刻",seconds(d.cursor_s)]].map(([k,v])=>`<div>${k}<strong>${esc(v)}</strong></div>`).join("");
    $("g-context").innerHTML=[["信号","北斗 B1C"],["数据来源","已导入的离线处理结果"],["采样率",`${cell(m.sample_rate_hz/1e6,3)} MS/s`],["中频",`${cell(m.tracking.if_hz)} Hz`],["位置时间模型","接收机钟差联合解算"],["在线输入","尚未接入"]].map(([k,v])=>`<dt>${k}</dt><dd>${esc(v)}</dd>`).join("");
    if(g.live){const dev=m.device,r=d.live;$("g-context").innerHTML=[["设备",dev.args],["RF / 天线",`${cell(dev.tuned_hz)} Hz / ${dev.antenna}`],["实际采样率",`${cell(m.sample_rate_hz/1e6,6)} MS/s`],["中频",`${cell(m.tracking.if_hz)} Hz`],["时钟来源",`${dev.clock_source} / ${dev.time_source}`],["10 MHz 锁定",r.reference_locked===true?"已锁定":r.reference_locked===false?"未锁定":"无传感器报告"],["PPS 输入",r.pps_detected?"已检测":"未检测"],["处理队列样本",r.queue_samples??"—"]].map(([k,v])=>`<dt>${k}</dt><dd>${esc(v)}</dd>`).join("");}
    $("g-cn0-bars").innerHTML=d.satellites.map(s=>{const n=s.tracking?.cn0_db_hz,valid=finite(n);return `<button class="cn0-item ${s.used?"used":s.tracking?.valid?"tracked":"unlocked"}" data-prn="${s.prn}" title="查看 ${prnName(s.prn)}"><span>${prnName(s.prn)}</span><div class="cn0-track"><i style="height:${valid?Math.max(0,Math.min(100,n/60*100)):0}%"></i></div><b>${valid?cell(n,1):"—"}</b><small>${s.used?"定位用星":s.tracking?.valid?"跟踪有效":"未锁定 / 无数据"}</small></button>`;}).join("");
    $("g-satellite-table").innerHTML=d.satellites.map(s=>`<tr class="g-sat-row" data-prn="${s.prn}"><td class="cell-id">${prnName(s.prn)}</td><td>${s.acquisition?s.acquisition.acquired?"成功":"未捕获":"无记录"}</td><td>${badge(s.tracking?stateNames[s.tracking.state]:"本时刻无数据",!!s.tracking?.valid)}</td><td>${cell(s.tracking?.cn0_db_hz,1)}</td><td>${cell(s.tracking?.doppler_hz)}</td><td>${s.navigation_ready?"已就绪":s.frame?"当前段未就绪":"等待收齐"}</td><td>${s.used?badge("参与定位",true):esc(reasons[s.exclusion]||s.exclusion||(d.position_current?"未参与":"本时刻无定位历元"))}</td><td>${esc(s.tracking?.generation??"—")}</td></tr>`).join("");
    document.querySelectorAll("#gnss-shell [data-prn]").forEach(el=>el.onclick=async()=>{g.prn=Number(el.dataset.prn);g.frameKey=null;resetPlots();choosePage("trace");try{await request();}catch(e){error(e);}});
    $("g-config").innerHTML=[["载波环带宽",`${m.tracking.pll_bandwidth_hz??"—"} Hz`],["码环带宽",`${m.tracking.dll_bandwidth_hz??"—"} Hz`],["E/P 偏移",`${m.tracking.early_late_offset_chips??"—"} chip`],["质量窗口",`${m.tracking.quality_epochs??"—"} 个历元`],["定位周期",`${m.positioning?.period_ms??"—"} ms`],["仰角门限",`${m.positioning?.elevation_mask_deg??"—"}°`],["对流层改正",m.positioning?m.positioning.troposphere?"固定气象模型":"关闭":"无定位记录"],["LDPC 纠错",m.navigation?.ldpc_error_correction?"已启用":"未启用"]].map(([k,v])=>fact(k,v)).join("");
    $("g-metadata").textContent=JSON.stringify(m,null,2);
    $("g-trace-title").textContent=prnName(g.prn)+" · 单星跟踪";$("g-trace-caption").textContent=e?`记录 ${seconds(e.time_s)} · ${stateNames[e.state]} · 代次 ${e.generation} · 64 位采样计数 ${e.absolute_sample}`:"本时刻没有该卫星的完整跟踪历元";
    $("g-trace-facts").innerHTML=[["捕获多普勒 / Hz",cell(s?.acquisition?.doppler_hz)],["捕获码相位 / sample",s?.acquisition?.code_phase_samples??"—"],["捕获峰值指标",cell(s?.acquisition?.peak_metric)],["码频率 / chip/s",cell(e?.code_rate_hz,3)],["当前 C/N₀ / dB-Hz",cell(e?.cn0_db_hz,1)]].map(([k,v])=>`<span>${k}<b>${esc(v)}</b></span>`).join("");
    if(g.page==="trace"){
      for(const [p,f]of [[gp.cn0,"cn0_db_hz"],[gp.doppler,"doppler_hz"],[gp.dll,"dll_error"],[gp.pll,"pll_error"],[gp["residual-frequency"],"residual_frequency_hz"]]){p.markers=d.events.map(e=>({x:e.time_s}));p.set([d.series[f]],"本窗口没有保存该项数据");}
      gp.data.set([d.series.data_i_p,d.series.data_q_p]);gp.pilot.set([d.series.pilot_i_p,d.series.pilot_q_p]);
      const spacing=m.tracking.early_late_offset_chips,mag=(side,component)=>e&&finite(e[`${component}_i_${side}`])&&finite(e[`${component}_q_${side}`])?Math.hypot(e[`${component}_i_${side}`],e[`${component}_q_${side}`]):null;
      gp.epl.set(finite(spacing)?["data","pilot"].map(k=>({x:[-spacing,0,spacing],y:["e","p","l"].map(side=>mag(side,k))})):[],"没有保存 E/P/L 抽头或间隔参数");
    }
    $("g-events").innerHTML=d.events.map(e=>`<tr><td>${seconds(e.time_s)}</td><td>${esc(eventNames[e.event]||e.event)}</td><td>${esc(e.generation??"—")}</td><td>${esc(e.absolute_sample??"—")}</td></tr>`).join("");if(!d.events.length)emptyRows("g-events",4,"本时刻之前没有跟踪事件");
    if(g.page==="results"){
      for(const tab of ["position","observations","navigation"])$("g-"+tab+"-result").classList.toggle("hidden",tab!==g.tab);
      if(g.tab==="position")renderPosition(d);
      const by=new Map(d.observations.map(r=>[r.prn,r]));
      $("g-observations").innerHTML=d.satellites.map(s=>{const o=by.get(s.prn);return `<tr><td class="cell-id">${prnName(s.prn)}</td><td>${s.used?"参与":esc(reasons[s.exclusion]||s.exclusion||"无观测")}</td><td>${cell(o?.elevation_deg,2)}</td><td>${cell(o?.raw_pseudorange_m,3)}</td><td>${cell(o?.clock_corrected_pseudorange_m,3)}</td><td>${cell(o?.receiver_corrected_pseudorange_m,3)}</td><td>${cell(o?.residual_m,3)}</td><td>${finite(o?.satellite_clock_s)?cell(o.satellite_clock_s*1e6,6):"—"}</td></tr>`;}).join("");
      $("g-exclusions").innerHTML=d.exclusions.map(e=>`<span>${prnName(e.prn)} · ${esc(reasons[e.reason]||e.reason)}</span>`).join("")||'<span class="muted">本定位历元没有保存排除记录。</span>';
      if(g.tab==="navigation")renderFrames();
    }
  }
  async function play(){
    if(!g.playing||!g.data||g.frozen)return;const start=performance.now(),m=g.data.run;
    if(g.time>=m.end_s){stop();return;}
    g.time=Math.min(m.end_s,g.time+.2*Number($("g-speed").value));
    try{await request();}catch(e){error(e);return;}
    if(g.playing)g.timer=setTimeout(play,Math.max(0,200-(performance.now()-start)));
  }
  $("signal-gnss").onclick=activate;$("signal-5g").onclick=deactivate;
  $("g-run").onchange=async()=>{stop();g.run=$("g-run").value;g.prn=0;g.reference=null;g.frameKey=null;resetPlots();try{await request(true);}catch(e){error(e);}};
  $("g-prn").onchange=async()=>{stop();g.prn=Number($("g-prn").value);g.frameKey=null;resetPlots();try{await request();}catch(e){error(e);}};
  $("g-window").onchange=async()=>{g.window=Number($("g-window").value);resetPlots();try{await request();}catch(e){error(e);}};
  $("g-reload").onclick=()=>{stop();loadRuns().catch(error);};
  $("g-time").oninput=()=>{stop();clearTimeout(g.debounce);const value=$("g-time").value;g.debounce=setTimeout(()=>seek(value).catch(error),120);};
  $("g-time-number").onchange=()=>seek($("g-time-number").value).catch(error);
  $("g-start").onclick=()=>seek(g.data.run.start_s).catch(error);$("g-end").onclick=()=>seek(g.data.run.end_s-.0000001).catch(error);
  for(const [id,sign]of [["g-prev",-1],["g-next",1]])$(id).onclick=()=>seek(g.time+sign*(g.data.run.positioning?.period_ms||200)/1000).catch(error);
  $("g-play").onclick=()=>{if(g.playing)stop();else{g.playing=true;$("g-play").textContent="Ⅱ 暂停";play();}};
  $("g-freeze").onclick=()=>{stop();g.frozen=!g.frozen;++g.version;if(g.data)g.time=g.data.cursor_s;render();};
  $("g-reset-zoom").onclick=resetPlots;
  document.querySelectorAll("[data-gresult]").forEach(b=>b.onclick=()=>{g.tab=b.dataset.gresult;document.querySelectorAll("[data-gresult]").forEach(v=>v.classList.toggle("selected",v===b));render();if(g.tab==="navigation")loadFrames().catch(error);});
  $("g-frame-prev").onclick=()=>{g.frameOffset=Math.max(0,g.frameOffset-20);loadFrames().catch(error);};$("g-frame-next").onclick=()=>{g.frameOffset+=20;loadFrames().catch(error);};
  $("g-ref-apply").onclick=()=>{
    const raw=["g-ref-lat","g-ref-lon","g-ref-height"].map(id=>$(id).value.trim()),[lat,lon,h]=raw.map(Number);
    if(raw.some(v=>!v)||![lat,lon,h].every(finite)||Math.abs(lat)>90||Math.abs(lon)>180||h< -10000||h>1e6){$("g-reference-label").textContent="请填写有效的参考纬度、经度和椭球高。";return;}
    const a=6378137,f=1/298.257223563,e2=f*(2-f),p=lat*Math.PI/180,l=lon*Math.PI/180,n=a/Math.sqrt(1-e2*Math.sin(p)**2);
    g.reference={latitude_deg:lat,longitude_deg:lon,height_m:h,x_m:(n+h)*Math.cos(p)*Math.cos(l),y_m:(n+h)*Math.cos(p)*Math.sin(l),z_m:(n*(1-e2)+h)*Math.sin(p)};resetPlots();render();
  };
  $("g-ref-reset").onclick=()=>{g.reference=null;resetPlots();render();};
  function scheduleLive(){clearTimeout(g.liveTimer);if(!g.on||!g.live)return;g.liveTimer=setTimeout(async()=>{try{if(!g.frozen)await request();}catch(e){error(e);}scheduleLive();},500);}
  async function setSource(live){stop();clearTimeout(g.liveTimer);++g.version;g.live=live;g.data=null;g.prn=0;g.reference=null;g.frozen=false;g.frameKey=null;resetPlots();render();try{await loadRuns();}catch(e){error(e);}scheduleLive();}
  $("g-live-mode").onclick=()=>setSource(true);$("g-offline-mode").onclick=()=>setSource(false);
  window.GNSS={active:()=>g.on,choosePage};
  if(location.hash==="#gnss"||(!location.hash&&document.body.dataset.defaultSignal==="bds"))activate();
})();
