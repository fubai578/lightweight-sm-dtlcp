'use strict';
const $ = id => document.getElementById(id);
let cursor = 0, runId = null, allEvents = [], state = null, paused = false, filter = 'all';
let connected = false, busy = false, toastTimer;
let settings = {peer: '', udp_port: 4433, count: 12};
const modes = {full:'FULL',resume:'RESUME',psk:'PSK',early:'PSK · 0-RTT'};
const names = {start:'实验启动',ready:'网关就绪',handshake:'握手完成',wire:'UDP 数据报',telemetry:'接收遥测',ack_sent:'ACK 已发送',complete:'网关完成',finished:'实验完成',error:'运行异常',stop:'实验停止'};
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
      e.bytes == null ? '—' : number(e.bytes),
      e.text || (e.type === 'wire' ? 'UDP 数据报元信息（不显示原始载荷）' : e.type === 'handshake' ? `握手完成 · SM2 ${e.sm2_ops} 次 · ${(e.elapsed_us/1000).toFixed(2)} ms` : '—')];
    values.forEach((value,i) => {
      const td = document.createElement('td');
      if (i === 1) {
        const tag = document.createElement('span');
        tag.className = 'event-tag' + (e.type === 'telemetry' || e.type === 'handshake' ? ' verified' : e.type === 'error' ? ' failed' : '');
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
  const ack = latest && allEvents.find(e => e.id > latest.id && e.type === 'ack_sent');
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
  $('handshakeValue').textContent = hs ? (hs.elapsed_us/1000).toFixed(2) : '—';
  $('handshakeDetail').textContent = hs ? `本机握手耗时 · SM2 运算 ${hs.sm2_ops} 次` : '等待一次完整握手';
  $('receivedValue').textContent = number(m.received);
  $('receivedDetail').textContent = m.app_bytes == null ? '网关解保护后的业务消息' : `业务载荷累计 ${number(m.app_bytes)} B`;
  $('ackValue').textContent = number(m.acks);
  $('wireValue').textContent = m.wire_rx == null && m.wire_tx == null ? '—' : number((m.wire_rx || 0)+(m.wire_tx || 0));
  $('wireDetail').textContent = m.wire_rx == null && m.wire_tx == null ? '含握手与记录层开销' : `接收 ${number(m.wire_rx || 0)} B / 发送 ${number(m.wire_tx || 0)} B`;
  $('telemetryEmpty').hidden = !!latest;
  $('telemetryLive').hidden = !latest;
  if (latest) {
    $('payload').textContent = latest.text;
    $('payloadBytes').textContent = `${number(latest.bytes)} B`;
    $('payloadTime').textContent = timeLabel(latest.timestamp);
    $('payloadTimestamp').textContent = timeLabel(latest.timestamp);
    $('payloadAck').textContent = ack ? 'ACK 已发送' : '等待网关回执';
  } else $('payloadTime').textContent = '尚无消息';
  step('stepReady',ready,ready ? '本机网关启动成功' : '等待启动本机网关');
  step('stepHandshake',!!hs,hs ? '收到握手完成事件' : '等待与终端建立会话');
  step('stepTelemetry',!!latest,latest ? '记录解保护成功' : '收到消息后执行解保护');
  step('stepAck',!!m.acks,m.acks ? '网关发送成功' : '等待网关发送 ACK');
  $('sessionNote').textContent = state.error || (state.config.mode === 'early' ? '0-RTT 早期数据存在重放风险；发送成功不代表对端已收到。' : '只有网关实际观测到的事件会更新状态。');
  renderRecords();
}
function setConnection(ok) {
  connected = ok;
  $('serviceStatus').classList.toggle('online',ok);
  $('serviceStatus').replaceChildren();
  $('serviceStatus').append(document.createElement('i'),document.createTextNode(ok ? '本机服务在线' : '本机服务断开'));
  $('observerState').textContent = ok ? '观测就绪' : '服务离线';
  $('nodeObserver').classList.toggle('active',ok);
  if (!ok) {
    $('startButton').disabled = true; $('stopButton').disabled = true;
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
  $('settingsDialog').close();toast('连接设置已保存，尚未启动网关。');
});
$('guideButton').addEventListener('click',() => $('guideDialog').showModal());
document.querySelectorAll('[data-close]').forEach(button => button.addEventListener('click',() => $(button.dataset.close).close()));
$('mode').addEventListener('change',() => { if (!state || !activeStates.has(state.status)) $('sessionMode').textContent=modes[$('mode').value]; });
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
clock();setInterval(clock,1000);poll();
