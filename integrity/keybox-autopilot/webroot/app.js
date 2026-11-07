// Keybox Autopilot WebUI. Talks to scripts/webui.sh through the KSU bridge.
// @Dev_DcS
const BIN = '/data/adb/modules/keybox_autopilot/scripts/webui.sh';

// KernelSU / FinaSU WebUI exec bridge -> Promise<string stdout>
function exec(cmd) {
  return new Promise((resolve, reject) => {
    if (typeof ksu === 'undefined' || !ksu.exec) {
      reject(new Error('no-bridge'));
      return;
    }
    const cb = 'kbcb_' + Math.random().toString(36).slice(2);
    window[cb] = (errno, stdout) => { delete window[cb]; resolve(stdout || ''); };
    try { ksu.exec(cmd, '{}', cb); } catch (e) { delete window[cb]; reject(e); }
  });
}

const call = (action, ...args) =>
  exec([`sh`, BIN, action, ...args.map(a => `'${String(a).replace(/'/g, "'\\''")}'`)].join(' '));

function toast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg; t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 2200);
}

function renderTargets(csv) {
  const ul = document.getElementById('target-list');
  ul.innerHTML = '';
  (csv ? csv.split(',') : []).filter(Boolean).forEach(pkg => {
    const li = document.createElement('li');
    li.innerHTML = `<span>${pkg}</span>`;
    const del = document.createElement('button');
    del.textContent = '✕'; del.className = 'del';
    del.onclick = async () => { await call('deltarget', pkg); toast('removed'); refresh(); };
    li.appendChild(del); ul.appendChild(li);
  });
  if (!ul.children.length) ul.innerHTML = '<li class="empty">no custom apps yet</li>';
}

async function refresh() {
  try {
    const raw = await call('state');
    const s = JSON.parse(raw.trim() || '{}');
    document.getElementById('st-keybox').textContent = s.keybox === 'yes' ? 'yes' : 'no';
    document.getElementById('st-backend').textContent = s.backend === 'yes' ? 'found' : 'missing';
    document.getElementById('st-last').textContent =
      s.last ? new Date(parseInt(s.last, 10) * 1000).toLocaleString() : 'never';
    document.getElementById('in-source').value = s.source || '';
    document.getElementById('in-refresh').value = s.refresh || '0';
    document.getElementById('in-recommended').checked = s.recommended === '1';
    renderTargets(s.targets || '');
    document.getElementById('log').textContent = (await call('log')).trim() || '(empty)';
  } catch (e) {
    toast(e.message === 'no-bridge' ? 'Open from your root manager' : 'error: ' + e.message);
  }
}

document.getElementById('btn-sync').onclick = async () => {
  toast('syncing...'); await call('sync'); toast('done'); refresh();
};
document.getElementById('btn-save').onclick = async () => {
  await call('set', 'SOURCE_URL', document.getElementById('in-source').value.trim());
  await call('set', 'REFRESH_HOURS', document.getElementById('in-refresh').value.trim() || '0');
  await call('set', 'APPLY_RECOMMENDED', document.getElementById('in-recommended').checked ? '1' : '0');
  toast('saved'); refresh();
};
document.getElementById('btn-add').onclick = async () => {
  const pkg = document.getElementById('in-pkg').value.trim();
  if (!pkg) return;
  await call('addtarget', pkg);
  document.getElementById('in-pkg').value = '';
  toast('added'); refresh();
};

refresh();
