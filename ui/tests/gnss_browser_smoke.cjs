/* Browser QA for imported real B1C results. Uses the repository's existing
 * plain Playwright harness style; no test framework/runtime dependency. */
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const base=process.argv[2]||'http://127.0.0.1:18765';
const output=path.resolve('build/playwright/gnss');fs.mkdirSync(output,{recursive:true});
let browser;
(async()=>{
  browser=await chromium.launch({headless:true,...(process.env.BROWSER_PATH?{executablePath:process.env.BROWSER_PATH}:{})});
  const page=await browser.newPage({viewport:{width:1536,height:1080}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(base+'/#gnss');
  await page.locator('#g-offline-mode').click();
  await page.waitForFunction(()=>document.querySelector('#g-used').textContent==='6');
  assert.equal(await page.locator('#g-captured').innerText(),'7');
  assert.equal(await page.locator('#g-tracked').innerText(),'7 / 7');
  assert.equal(await page.locator('#g-nav-ready').innerText(),'7 / 7');
  assert.match(await page.locator('#g-satellite-table').innerText(),/低于仰角门限/);
  assert.match(await page.locator('#g-notice').innerText(),/离线回溯/);
  await page.screenshot({path:path.join(output,'summary.png'),fullPage:true});
  await page.locator('[data-page="trace"]').click();
  await page.locator('#g-prn').selectOption('25');
  await page.waitForFunction(()=>document.querySelector('#g-trace-title').textContent.includes('C25'));
  const shortWindow=await page.locator('#g-chart-doppler').evaluate(c=>c.toDataURL());
  const [windowResponse]=await Promise.all([
    page.waitForResponse(r=>r.url().includes('/api/gnss/state?')&&r.url().includes('window=120')),
    page.locator('#g-window').selectOption('120')
  ]);
  assert.ok((await windowResponse.json()).series.doppler_hz.x[0]<.1,'Full record should include acquisition/reacquisition period');
  await page.waitForFunction(previous=>document.querySelector('#g-chart-doppler').toDataURL()!==previous,shortWindow);
  await page.waitForFunction(()=>document.querySelector('#g-events').textContent.includes('重捕获成功'));
  const prior=await page.locator('#g-chart-doppler').evaluate(c=>c.toDataURL());
  const b=await page.locator('#g-chart-doppler').boundingBox();
  await page.mouse.move(b.x+b.width/2,b.y+b.height/2);await page.mouse.wheel(0,-600);
  assert.notEqual(await page.locator('#g-chart-doppler').evaluate(c=>c.toDataURL()),prior);
  await page.locator('#g-reset-zoom').click();
  await page.screenshot({path:path.join(output,'trace-c25.png'),fullPage:true});
  await page.locator('[data-page="results"]').click();
  await page.waitForFunction(()=>document.querySelector('#g-latitude').textContent.includes('40.'));
  assert.match(await page.locator('#g-position-status').innerText(),/完整电文依赖收齐/);
  await page.screenshot({path:path.join(output,'position.png'),fullPage:true});
  await page.locator('.reference-panel summary').click();
  await page.locator('#g-ref-lat').fill('40.009973328');await page.locator('#g-ref-lon').fill('-105.244333287');await page.locator('#g-ref-height').fill('1616.116');
  await page.locator('#g-ref-apply').click();assert.match(await page.locator('#g-reference-label').innerText(),/用户填写/);
  await page.locator('#g-ref-reset').click();assert.match(await page.locator('#g-reference-label').innerText(),/不是绝对定位误差/);
  await page.locator('[data-gresult="observations"]').click();
  assert.match(await page.locator('#g-exclusions').innerText(),/C25.*低于仰角/);
  await page.screenshot({path:path.join(output,'observations.png'),fullPage:true});
  await page.locator('[data-gresult="navigation"]').click();
  await page.waitForFunction(()=>document.querySelector('#g-frame-page').textContent.includes('3 个'));
  assert.match(await page.locator('#g-ephemeris').innerText(),/832/);
  await page.screenshot({path:path.join(output,'navigation.png'),fullPage:true});
  await page.locator('#g-time-number').fill('10');await page.locator('#g-time-number').press('Tab');
  await page.waitForFunction(()=>document.querySelector('#g-nav-ready').textContent==='0 / 7');
  assert.match(await page.locator('#g-frames').innerText(),/还没有收齐/);
  await page.locator('[data-gresult="position"]').click();
  assert.match(await page.locator('#g-position-status').innerText(),/离线回溯解/);
  await page.locator('#g-play').click();
  await page.waitForFunction(()=>Number(document.querySelector('#g-time').value)>10.1);
  await page.locator('#g-play').click();
  await page.locator('#g-freeze').click();
  const frozen=await page.locator('#g-time').inputValue();
  assert.equal(await page.locator('#g-time').isDisabled(),true);
  await page.waitForTimeout(350);assert.equal(await page.locator('#g-time').inputValue(),frozen);
  await page.locator('#g-freeze').click();
  // An epoch with no solution must not show a fabricated origin/zero fix.
  await page.locator('#g-time-number').fill('0.5');await page.locator('#g-time-number').press('Tab');
  await page.waitForFunction(()=>document.querySelector('#g-latitude').textContent==='—');
  assert.match(await page.locator('#g-pvt-state').innerText(),/无解/);
  await page.locator('#signal-5g').click();
  await page.waitForSelector('#nr-shell:visible');
  assert.match(await page.locator('#page-title').innerText(),/Summary/);
  await page.locator('#signal-gnss').click();
  await page.waitForSelector('#gnss-shell:visible');
  await page.waitForFunction(()=>Number(document.querySelector('#g-time').value)===.5);
  await page.locator('#g-time-number').fill('59.75');await page.locator('#g-time-number').press('Tab');
  await page.waitForFunction(()=>document.querySelector('#g-used').textContent==='6');
  await page.setViewportSize({width:800,height:1000});
  await page.screenshot({path:path.join(output,'compact.png'),fullPage:true});
  await page.setViewportSize({width:390,height:844});
  assert.equal(await page.locator('#signal-5g').isVisible(),true);
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,'Mobile page should fit viewport');
  await page.screenshot({path:path.join(output,'mobile.png'),fullPage:true});
  // A fresh checkout has no imported local experiment. Verify the actual
  // empty-data view as well, without modifying any user recordings.
  await page.route('**/api/gnss/runs',route=>route.fulfill({json:{runs:[]}}));
  await page.reload();
  await page.locator('#g-offline-mode').click();
  await page.waitForSelector('#g-empty:visible');
  const instructions=await page.locator('#g-empty pre').innerText();
  assert.match(instructions,/python3 ui\/import_gnss.py/);
  assert.equal(instructions.includes('\n+'),false,'Import instructions should be copyable');
  assert.equal(await page.locator('#g-time').isDisabled(),true);
  await page.screenshot({path:path.join(output,'empty.png'),fullPage:true});
  assert.deepEqual(errors,[],'Browser page errors');
  console.log('PASS: real B1C summary, C25 reacquisition trace/zoom, SPP/ENU/reference, observations, frames/causality, replay, freeze, empty epochs/experiments, mode switching and responsive layout');
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(async()=>{if(browser)await browser.close();});
