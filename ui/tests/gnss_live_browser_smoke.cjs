/* Synthetic display fixture only. Does not connect to or validate RF hardware.
 * Start ui/tests/test_gnss_live.py --serve NEW_DIR PORT, then pass its JSONL
 * path below. Outputs screenshots to build/playwright/gnss-live/. */
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const [base,file]=process.argv.slice(2),output=path.resolve('build/playwright/gnss-live');
assert.ok(file&&file.includes('test-only'),'Use only the isolated test-only fixture');fs.mkdirSync(output,{recursive:true});
const original=fs.readFileSync(file,'utf8').trim().split('\n').map(JSON.parse),runtime=original.at(-1);
function emit(kind,data,age=0){fs.appendFileSync(file,JSON.stringify({schema:1,run_id:'test-only-display',time_ms:Date.now()-age,kind,data})+'\n');}
function tick(extra={},age=0){emit('runtime',{...runtime.data,...extra},age);}
let browser,timer;
(async()=>{
  browser=await chromium.launch({headless:true,...(process.env.BROWSER_PATH?{executablePath:process.env.BROWSER_PATH}:{})});
  const page=await browser.newPage({viewport:{width:1536,height:1080}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));tick();timer=setInterval(()=>tick(),400);
  await page.goto(base);await page.waitForFunction(()=>document.querySelector('#g-used').textContent==='4');
  assert.match(await page.locator('#g-notice').innerText(),/B210 实采/);
  assert.equal(await page.locator('.gnss-replay').isVisible(),false);
  assert.match(await page.locator('#g-context').innerText(),/test-only-mock/);
  await page.screenshot({path:path.join(output,'summary-test-only.png'),fullPage:true});
  await page.locator('[data-page="trace"]').click();await page.waitForSelector('#g-trace-page:visible');
  await page.screenshot({path:path.join(output,'trace-test-only.png'),fullPage:true});
  await page.locator('[data-page="results"]').click();await page.waitForFunction(()=>document.querySelector('#g-latitude').textContent.includes('40.'));
  assert.match(await page.locator('#g-position-status').innerText(),/在线因果解/);
  await page.locator('#g-freeze').click();clearInterval(timer);
  tick({running:false,stage:'stopped'});
  await page.waitForTimeout(1100);assert.match(await page.locator('#g-latitude').innerText(),/40./);
  assert.match(await page.locator('#connection-label').innerText(),/冻结/);
  await page.locator('#g-freeze').click();await page.waitForFunction(()=>document.querySelector('#g-latitude').textContent==='—');
  tick({lag_s:3});await page.waitForFunction(()=>document.querySelector('#connection-label').textContent.includes('滞后'));
  assert.equal(await page.locator('#g-latitude').innerText(),'—');
  tick({},5000);await page.waitForFunction(()=>document.querySelector('#connection-label').textContent.includes('过期'));
  tick();timer=setInterval(()=>tick(),400);
  await page.waitForFunction(()=>document.querySelector('#g-latitude').textContent.includes('40.'));
  await page.locator('[data-gresult="navigation"]').click();await page.waitForFunction(()=>document.querySelector('#g-frame-page').textContent.includes('25 个'));
  await page.locator('#g-frame-next').click();await page.waitForFunction(()=>document.querySelector('#g-frame-page').textContent.includes('第 2 页'));
  await page.waitForTimeout(1100);assert.match(await page.locator('#g-frame-page').innerText(),/第 2 页/);
  await page.locator('#g-prn').selectOption('25');await page.waitForFunction(()=>document.querySelector('#g-frame-page').textContent.includes('C25')&&document.querySelector('#g-frame-page').textContent.includes('第 1 页'));
  await page.locator('#g-frame-next').click();await page.waitForFunction(()=>document.querySelector('#g-frame-page').textContent.includes('第 2 页'));
  await page.locator('#g-freeze').click();assert.equal(await page.locator('#g-frame-prev').isDisabled(),true);
  await page.locator('#g-freeze').click();
  emit('gap',{time_s:20,reason:'test_only_gap'});
  await page.locator('[data-gresult="position"]').click();await page.waitForFunction(()=>document.querySelector('#g-latitude').textContent==='—');
  await page.screenshot({path:path.join(output,'gap-test-only.png'),fullPage:true});
  await page.locator('#g-offline-mode').click();await page.waitForSelector('.gnss-replay:visible');
  await page.locator('#signal-5g').click();await page.waitForSelector('#nr-shell:visible');
  await page.locator('#signal-gnss').click();await page.locator('#g-live-mode').click();
  await page.waitForFunction(()=>document.querySelector('#g-source-badge').textContent==='B210 实采');
  await page.setViewportSize({width:390,height:844});
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
  await page.screenshot({path:path.join(output,'mobile-test-only.png'),fullPage:true});
  clearInterval(timer);
  const nextConfig=structuredClone(original[0]);nextConfig.run_id=nextConfig.data.run_id='next-test-only';nextConfig.data.prns=[37];
  const nextRuntime=structuredClone(runtime);nextRuntime.run_id='next-test-only';nextRuntime.time_ms=Date.now();nextRuntime.data.valid_fixes='0';nextRuntime.data.valid_frames='0';
  fs.appendFileSync(file,JSON.stringify(nextConfig)+'\n'+JSON.stringify(nextRuntime)+'\n');
  await page.waitForFunction(()=>document.querySelector('#g-prn').value==='37');
  await page.route('**/api/gnss/live?**',route=>route.fulfill({json:{waiting:true,live:{}}}));
  await page.reload();await page.waitForSelector('#g-empty:visible');
  assert.match(await page.locator('#g-empty h2').innerText(),/等待北斗 B210/);
  assert.deepEqual(errors,[]);console.log('PASS: synthetic live display, stale/lagged/stop/gap blanking, freeze, frame paging, source switching and mobile/empty views');
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(async()=>{clearInterval(timer);if(browser)await browser.close();});
