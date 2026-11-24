'use strict';
const $ = id => document.getElementById(id);
const emptyHandshakeTimeline = $('handshakeTimeline').innerHTML;
let cursor = 0, runId = null, allEvents = [], state = null, paused = false, filter = 'all';
let connected = false, busy = false, toastTimer;
let settings = {peer: '', udp_port: 4433, count: 12};
const modes = {full:'FULL',resume:'RESUME'};
const names = {security_result:'安全测试结果',ready_wait:'等待终端',start:'实验启动',ready:'网关就绪',handshake:'握手完成',wire:'UDP 数据报',telemetry:'接收遥测',ack_sent:'ACK 已发送',complete:'网关完成',finished:'实验完成',error:'运行异常',stop:'实验停止'};
const business = new Set(['telemetry','ack_sent']);
const activeStates = new Set(['starting','waiting','connected','stopping']);
function toast(text) {
  $('toast').textContent = text; $('toast').hidden = false;
  clearTimeout(toastTimer); toastTimer = setTimeout(() => $('toast').hidden = true, 5000);
}
function timeLabel(value, full=false) {
  if (!value) return '—';
  const date = new Date(value);
  return new Intl.DateTimeFormat('zh-CN', {timeZone:'Asia/Shanghai',hour12:false,
    ...(full ? {month:'2-digit',day:'2-digit'} : {}),hour:'2-digit',minute:'2-digit',second:'2-digit',fractionalSecondDigits:3}).format(date);
}
function number(value) { return value == null ? '—' : Number(value).toLocaleString('en-US'); }
function step(id, done, text) {
  $(id).classList.toggle('done',done);
  $(id+'Text').textContent = text;
}
function renderConfiguration() {
  $('configuredPeer').textContent = settings.peer || '未指定';
  $('configuredPort').replaceChildren(document.createTextNode(settings.udp_port+' '));
  const note = document.createElement('small'); note.textContent = '尚未监听'; $('configuredPort').append(note);
  $('configuredMode').textContent = modes[$('mode').value];
}
function renderRecords() {
  const query = $('search').value.trim().toLowerCase();
  const rows = allEvents.filter(e => (filter === 'all' || (filter === 'business' ? business.has(e.type) : e.type !== 'wire' && !business.has(e.type)))
    && (!query || [e.text,names[e.type],e.type,e.role,e.direction].join(' ').toLowerCase().includes(query)));
  $('recordCount').textContent = number(allEvents.length);
  $('recordsEmpty').hidden = rows.length > 0;
  $('recordsEmpty').querySelector('strong').textContent = allEvents.length ? '没有匹配的记录' : '暂无通信记录';
  $('recordsEmpty').querySelector('span').textContent = allEvents.length ? '调整筛选条件或搜索内容。' : '接入数据后，握手、遥测与 ACK 事件将按时间排列。';
  const frag = document.createDocumentFragment();
  for (const e of rows.slice(-300).reverse()) {
    const tr = document.createElement('tr');
    const values = [timeLabel(e.timestamp),names[e.type] || e.type,
      e.type === 'wire' ? (e.direction === 'rx' ? '终端 → 网关' : '网关 → 终端') : e.type === 'telemetry' ? '终端 → 网关' : e.type === 'ack_sent' ? '网关 → 终端' : '本机网关',
      e.message_id ?? '—',
      e.bytes == null ? '—' : number(e.bytes),
      (e.type==='security_result' ? `${e.test==='tamper'?'篡改副本':'重放原记录'} · rc=${e.code} · ${e.passed?'拒绝符合预期，接收状态未改变':'测试失败'}` : e.text) || (e.type === 'wire' ? 'UDP 数据报元信息（不显示原始载荷）' : e.type === 'handshake' ? `握手完成 · SM2 ${e.sm2_ops} 次 · ${(e.elapsed_us/1000).toFixed(2)} ms` : '—')];
    values.forEach((value,i) => {
      const td = document.createElement('td');
      if (i === 1) {
        const tag = document.createElement('span');
        tag.className = 'event-tag' + (e.type === 'telemetry' || e.type === 'handshake' ? ' verified' : e.type === 'error' || e.type === 'security_result' ? ' failed' : '');
        tag.textContent = value; td.appendChild(tag);
      } else td.textContent = value;
      tr.appendChild(td);
    });
    frag.appendChild(tr);
  }
  $('records').replaceChildren(frag);
  $('recordFooter').textContent = allEvents.length ? `${number(rows.length)} 条符合筛选 · 最新记录在上方` : '等待真实网关事件';
}
function render() {
  if (!state) return;
  const m = state.metrics, hs = state.handshake, status = state.status;
  const running = activeStates.has(status);
  const ready = allEvents.some(e => e.type === 'ready');
  const latest = [...allEvents].reverse().find(e => e.type === 'telemetry');
  const ack = latest && latest.message_id != null && allEvents.find(e => e.type === 'ack_sent' && e.message_id === latest.message_id);
  const statusLabels = {idle:'尚未接入设备',starting:'正在启动网关',waiting:'网关就绪 · 等待终端',connected:'安全会话已建立',complete:'本轮接收完成',error:'实验未完成',stopped:'网关已停止',stopping:'正在停止接收'};
  $('runStatus').textContent = statusLabels[status] || status;
  $('runHint').textContent = state.error || (status === 'idle' ? '数据就绪后将在此实时显示' : status === 'waiting' ? '等待真实终端发送握手请求' : status === 'complete' ? '本轮真实事件已保留，可导出记录' : status === 'connected' ? '正在接收终端业务与网关事件' : '业务数据由本机 C 网关提供');
  $('runDot').className = 'status-dot' + (status === 'error' ? ' error' : running || status === 'complete' ? ' active' : '');
  $('terminalState').textContent = hs ? (running ? '会话已建立' : '本轮会话已结束') : '未连接';
  $('gatewayState').textContent = ready ? (running ? '运行中' : '本轮已结束') : status === 'starting' ? '启动中' : '未启动';
  $('nodeTerminal').classList.toggle('active',!!hs && running);
  $('nodeGateway').classList.toggle('active',ready && running);
  $('sourceBadge').textContent = state.run_id ? '网关实时事件' : '等待接入';
  $('startButton').hidden = running;
  $('stopButton').hidden = !running;
  $('startButton').disabled = !connected || busy;
  $('stopButton').disabled = !connected || busy || status === 'stopping';
  $('mode').disabled = running;
  $('settingsButton').disabled = running;
  $('exportButton').disabled = allEvents.length === 0;
  $('sessionMode').textContent = modes[state.run_id ? state.config.mode : $('mode').value];
  $('handshakeValue').textContent = hs ? ((hs.active_elapsed_us ?? hs.elapsed_us)/1000).toFixed(2) : '—';
  $('handshakeDetail').textContent = hs ? `首包至握手完成 · 网关 SM2 ${hs.sm2_ops} 次` : '等待一次完整握手';
  $('receivedValue').textContent = number(m.received);
  $('receivedDetail').textContent = m.app_bytes == null ? '网关解保护后的业务消息' : `业务载荷累计 ${number(m.app_bytes)} B`;
  $('ackValue').textContent = number(m.acks);
  $('wireValue').textContent = m.wire_rx == null && m.wire_tx == null ? '—' : number((m.wire_rx || 0)+(m.wire_tx || 0));
  $('wireDetail').textContent = m.wire_rx == null && m.wire_tx == null ? '含握手与记录层开销' : `接收 ${number(m.wire_rx || 0)} B / 发送 ${number(m.wire_tx || 0)} B`;
  $('telemetryEmpty').hidden = !!latest;
  $('telemetryLive').hidden = !latest;
  if (latest) {
    $('payload').textContent = latest.text;
    $('payloadSequence').textContent = latest.message_id ?? '—';
    $('payloadBytes').textContent = `${number(latest.bytes)} B`;
    $('payloadTime').textContent = timeLabel(latest.timestamp);
    $('payloadTimestamp').textContent = timeLabel(latest.timestamp);
    $('payloadAck').textContent = ack ? 'ACK 已发送' : '等待网关回执';
  } else $('payloadTime').textContent = '尚无消息';
  step('stepReady',ready,ready ? '本机网关启动成功' : '等待启动本机网关');
  step('stepHandshake',!!hs,hs ? '收到握手完成事件' : '等待与终端建立会话');
  step('stepTelemetry',!!latest,latest ? '记录解保护成功' : '收到消息后执行解保护');
  step('stepAck',!!m.acks,m.acks ? '网关发送成功' : '等待网关发送 ACK');
  $('sessionNote').textContent = state.error || ($('mode').value==='resume' ? (state.resume_available ? '恢复缓存可用；请勿复位开发板，使用恢复运行命令触发。' : '先完成 FULL；恢复依赖同一终端和网关进程的会话缓存。') : '') || '只有网关实际观测到的事件会更新状态。';
  renderLiveDetails();
  renderRecords();
}
function renderLiveDetails() {
  const hs=state.handshake,m=state.metrics,running=activeStates.has(state.status);
  $('integrationStatus').textContent=state.run_id ? '真实开发板 · 网关事件' : '等待真实终端接入';
  $('detailGateway').textContent=state.status==='idle'?'未启动':running?'运行中':state.status==='complete'?'本轮完成':'已结束 / 异常';
  $('detailPeer').textContent=hs?.peer || '—';
  $('detailSuite').textContent=hs?modes[state.config.mode]+' / L_SM2_SM4_CTR_SM3':'—';
  $('detailSessionTime').textContent=hs?timeLabel(hs.timestamp)+(state.ended?' / '+timeLabel(state.ended):' / —'):'—';
  const wires=allEvents.filter(e=>e.type==='wire');
  $('detailLastSeen').textContent=wires.length?timeLabel(wires.at(-1).timestamp):'—';
  if(state.run_id) {
    $('configuredPeer').textContent=state.config.peer;
    $('configuredPort').textContent=state.config.udp_port+(running?' · 网关运行中':' · 本轮已结束');
    $('configuredMode').textContent=modes[state.config.mode];
  }
  $('handshakeStatus').textContent=hs?'握手成功':state.error?'未完成':'等待握手';
  $('handshakeResult').textContent=hs?'成功':state.error?'未完成':'尚未完成';
  $('handshakeSm2').textContent=hs?hs.sm2_ops+' 次':'—';
  const hsPackets=hs?allEvents.filter(e=>e.type==='wire'&&e.id<hs.id).length:null;
  $('handshakePackets').textContent=hs?hs.flights+' / '+hsPackets:'— / —';
  $('handshakeBytes').textContent=hs?number(hs.wire_sent+hs.wire_recv)+' B':'—';
  const timeline=allEvents.filter(e=>['ready','handshake','error'].includes(e.type));
  if(timeline.length) {
    const frag=document.createDocumentFragment();
    timeline.forEach(e=>{const li=document.createElement('li'),i=document.createElement('i'),div=document.createElement('div'),strong=document.createElement('strong'),p=document.createElement('p'),t=document.createElement('time');
      strong.textContent=names[e.type];p.textContent=e.type==='ready'?'网关已开始等待开发板握手':e.type==='handshake'?'协议握手完成；未采集内部各消息阶段耗时':e.text;
      t.textContent=timeLabel(e.timestamp);div.append(strong,p);li.append(i,div,t);frag.append(li);});
    $('handshakeTimeline').replaceChildren(frag);
  } else $('handshakeTimeline').innerHTML=emptyHandshakeTimeline;
  $('securityAccepted').textContent=m.received==null?'—':number(m.received)+' 条';
  const security=state.security_results || [],last=security.at(-1);
  $('securityStatus').textContent=state.security_pending?'等待协议返回':last?(last.passed?'拒绝验证通过':'测试失败'):m.received?'可执行本地注入测试':'尚无验证事件';
  $('tamperRejected').textContent=m.tamper_rejected == null?'—':m.tamper_rejected+' 次';
  $('replayRejected').textContent=m.replay_rejected == null?'—':m.replay_rejected+' 次';
  $('securityReason').textContent=last?`${last.code} · ${last.code===-5?'MAC 校验失败':last.code===-6?'重复序号':'非预期返回'}`:'—';
  $('replayButton').disabled=$('tamperButton').disabled=!connected || !state.security_available || !!state.security_pending;
  $('securityHint').textContent=state.security_pending?'已请求测试，等待 C 记录层返回；页面不预设结果。':state.security_available?'网关本地注入真实记录副本；完成后的测试窗口为 60 秒。拒绝结果来自实际协议返回码。':'需运行网关并收到真实终端记录；测试窗口结束后需重新运行。';
  $('summaryStatus').textContent=state.run_id?(running?'进行中':state.status==='complete'?'本轮完成':'本轮结束'):'尚无实验';
  $('summaryRun').textContent=state.run_id || '—';
  $('summaryDuration').textContent=state.started?((new Date(state.ended || Date.now())-new Date(state.started))/1000).toFixed(1)+' s':'—';
  $('summaryAppBytes').textContent=m.app_bytes==null?'— / — B':number(m.app_bytes)+' / '+number(m.app_sent || 0)+' B';
  $('summaryBusiness').textContent=m.received?`接收 ${m.received} 条 / ACK 已发送 ${m.acks || 0} 条`:'等待遥测与 ACK 事件';
  $('evidenceGateway').textContent=state.run_id?'F103 → 本机 C 网关':'未接入';
  $('evidenceHardware').textContent=state.run_id?'STM32F103C8T6 + W5500':'—';
  $('evidenceInput').textContent=state.run_id?'固件在线状态上报 / 无性能对照':'—';
  $('evidenceNote').textContent=state.run_id?'此处为开发板实际发送的在线状态，不包含虚构温湿度；未加载历史性能基准。':'当前未加载性能基准；业务来源将在接入后标注。';
}
function setConnection(ok) {
  connected = ok;
  $('serviceStatus').classList.toggle('online',ok);
  $('serviceStatus').replaceChildren();
  $('serviceStatus').append(document.createElement('i'),document.createTextNode(ok ? '本机服务在线' : '本机服务断开'));
  $('observerState').textContent = ok ? '观测就绪' : '服务离线';
  $('nodeObserver').classList.toggle('active',ok);
  if (!ok) {
    $('startButton').disabled = true; $('stopButton').disabled = true; $('replayButton').disabled=$('tamperButton').disabled=true;
    $('runHint').textContent = '无法连接本机服务，请确认平台服务仍在运行。';
  }
}
async function poll() {
  try {
    const response = await fetch('/api/state?after='+cursor,{cache:'no-store'});
    if (!response.ok) throw new Error('offline');
    const next = await response.json();
    if (next.run_id !== runId) { allEvents = []; runId = next.run_id; }
    allEvents.push(...next.events);
    allEvents = allEvents.slice(-2000);
    cursor = next.cursor;
    state = next;
    setConnection(true);
    if (!paused) render();
  } catch (_) {setConnection(false);}
  finally { setTimeout(poll,600); }
}
async function post(path, data={}) {
  const response = await fetch(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || '操作未完成');
  return result;
}
$('startButton').addEventListener('click',async () => {
  if (busy) return;
  busy = true; $('startButton').disabled = true;
  try {
    await post('/api/start',{...settings,mode:$('mode').value});
    if (paused) $('pauseButton').click();
    toast('已请求启动网关，等待真实终端连接。');
  } catch (error) {toast(error.message);}
  finally {busy = false; if (!paused) render();}
});
$('stopButton').addEventListener('click',async () => {
  busy = true; $('stopButton').disabled = true;
  try {await post('/api/stop'); toast('网关已停止，本轮记录保留。');}
  catch (error) {toast(error.message);}
  finally {busy = false;}
});
$('settingsButton').addEventListener('click',() => {
  $('peer').value = settings.peer; $('udpPort').value = settings.udp_port; $('messageCount').value = settings.count;
  $('settingsDialog').showModal();
});
$('settingsForm').addEventListener('submit',e => {
  e.preventDefault();
  const peer = $('peer').value.trim();
  if (peer && (!/^\d{1,3}(\.\d{1,3}){3}$/.test(peer) || peer.split('.').some(p => +p > 255))) {
    toast('请填写有效的开发板 IPv4 地址，或留空。');return;
  }
  settings = {peer,udp_port:Number($('udpPort').value),count:Number($('messageCount').value)};
  renderConfiguration();
  $('settingsDialog').close();toast('连接设置已保存，尚未启动网关。');
});
for (const [id,test] of [['replayButton','replay'],['tamperButton','tamper']]) {
  $(id).addEventListener('click',async()=>{
    $('replayButton').disabled=$('tamperButton').disabled=true;
    try {await post('/api/security-test',{test});toast('已提交本地测试请求，等待协议验证结果。');}
    catch(error){toast(error.message);}
  });
}
$('guideButton').addEventListener('click',() => $('guideDialog').showModal());
document.querySelectorAll('[data-close]').forEach(button => button.addEventListener('click',() => $(button.dataset.close).close()));
$('mode').addEventListener('change',() => { renderConfiguration(); if (!state || !activeStates.has(state.status)) $('sessionMode').textContent=modes[$('mode').value]; });
$('presentButton').addEventListener('click',() => {
  const focused = document.body.classList.toggle('focus-mode');
  $('presentButton').lastChild.textContent = focused ? '退出专注' : '专注展示';
});
$('fullscreenButton').addEventListener('click',async () => {
  try { if(document.fullscreenElement) await document.exitFullscreen();else await document.documentElement.requestFullscreen(); }
  catch (_) {toast('浏览器未允许全屏，可使用 F11。');}
});
$('pauseButton').addEventListener('click',() => {
  paused = !paused;
  $('pauseButton').classList.toggle('paused',paused);
  $('pauseButton').title = paused ? '继续画面更新' : '暂停画面更新';
  $('pauseButton').setAttribute('aria-label',$('pauseButton').title);
  $('pauseButton').setAttribute('aria-pressed',String(paused));
  if (!paused) render();
  toast(paused ? '画面已暂停，后台继续接收。' : '已恢复实时画面。');
});
document.querySelectorAll('[data-filter]').forEach(button => button.addEventListener('click',() => {
  filter=button.dataset.filter;
  document.querySelectorAll('[data-filter]').forEach(b => b.classList.toggle('active',b===button));
  renderRecords();
}));
$('search').addEventListener('input',renderRecords);
$('exportButton').addEventListener('click',async () => {
  try {
    const response=await fetch('/api/export');if(!response.ok)throw new Error('导出失败');
    const data=await response.json();
    const blob=new Blob([JSON.stringify(data,null,2)],{type:'application/json'});
    const url=URL.createObjectURL(blob),a=document.createElement('a');
    a.href=url;a.download=`dtlcp-events-${data.run_id || 'empty'}.json`;a.click();
    setTimeout(() => URL.revokeObjectURL(url),1000);
  } catch(error){toast(error.message);}
});
function clock(){ $('clock').textContent=new Intl.DateTimeFormat('zh-CN',{timeZone:'Asia/Shanghai',year:'numeric',month:'2-digit',day:'2-digit',hour12:false,hour:'2-digit',minute:'2-digit',second:'2-digit'}).format(new Date()); }
renderConfiguration();clock();setInterval(clock,1000);poll();
