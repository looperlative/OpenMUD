/* NewCircMUD Web OLC — frontend JavaScript */
'use strict';

/* ====================================================================
 * Constants matching structs.h / constants.c
 * ==================================================================== */

const ROOM_FLAGS = [
  [0,'DARK'], [1,'DEATH'], [2,'NOMOB'], [3,'INDOORS'], [4,'PEACEFUL'],
  [5,'SOUNDPROOF'], [6,'NOTRACK'], [7,'NOMAGIC'], [8,'TUNNEL'],
  [9,'PRIVATE'], [10,'GODROOM'], [11,'HOUSE'], [12,'HOUSE_CRASH'],
  [13,'ATRIUM'], [14,'OLC'], [15,'BFS_MARK']
];

const SECTOR_TYPES = [
  [0,'Inside'], [1,'City'], [2,'Field'], [3,'Forest'], [4,'Hills'],
  [5,'Mountain'], [6,'Water (swim)'], [7,'Water (no swim)'],
  [8,'Flying'], [9,'Underwater']
];

const EXIT_FLAGS = [
  [0,'ISDOOR'], [1,'CLOSED'], [2,'LOCKED'], [3,'PICKPROOF']
];

const DIR_NAMES = ['North','East','South','West','Up','Down'];

const MOB_FLAGS = [
  [0,'SPEC'], [1,'SENTINEL'], [2,'SCAVENGER'], [3,'ISNPC'],
  [4,'AWARE'], [5,'AGGR'], [6,'STAY_ZONE'], [7,'WIMPY'],
  [8,'AGGR_EVIL'], [9,'AGGR_GOOD'], [10,'AGGR_NEUTRAL'],
  [11,'MEMORY'], [12,'HELPER'], [13,'NOCHARM'], [14,'NOSUMMON'],
  [15,'NOSLEEP'], [16,'NOBASH'], [17,'NOBLIND'], [18,'NOKILL']
];

const AFF_FLAGS = [
  [0,'BLIND'], [1,'INVISIBLE'], [2,'DETECT_ALIGN'], [3,'DETECT_INVIS'],
  [4,'DETECT_MAGIC'], [5,'SENSE_LIFE'], [6,'WATERWALK'], [7,'SANCTUARY'],
  [8,'GROUP'], [9,'CURSE'], [10,'INFRAVISION'], [11,'POISON'],
  [12,'PROTECT_EVIL'], [13,'PROTECT_GOOD'], [14,'SLEEP'],
  [15,'NOTRACK'], [16,'UNUSED'], [17,'UNUSED2'], [18,'SNEAK'],
  [19,'HIDE'], [20,'UNUSED3'], [21,'CHARM']
];

const POSITIONS = [
  [0,'Dead'], [1,'Mortally wounded'], [2,'Incapacitated'], [3,'Stunned'],
  [4,'Sleeping'], [5,'Resting'], [6,'Sitting'], [7,'Fighting'], [8,'Standing']
];

const SEX_TYPES = [[0,'Neutral'], [1,'Male'], [2,'Female']];

const ATTACK_TYPES = [
  [0,'Hit'], [1,'Sting'], [2,'Whip'], [3,'Slash'], [4,'Bite'],
  [5,'Bludgeon'], [6,'Crush'], [7,'Pound'], [8,'Claw'], [9,'Maul'],
  [10,'Thrash'], [11,'Pierce'], [12,'Blast'], [13,'Punch'], [14,'Stab']
];

const ITEM_TYPES = [
  [0,'UNDEFINED'], [1,'LIGHT'], [2,'SCROLL'], [3,'WAND'], [4,'STAFF'],
  [5,'WEAPON'], [6,'FIREWEAPON'], [7,'MISSILE'], [8,'TREASURE'],
  [9,'ARMOR'], [10,'POTION'], [11,'WORN'], [12,'OTHER'],
  [13,'TRASH'], [14,'TRAP'], [15,'CONTAINER'], [16,'NOTE'],
  [17,'DRINKCON'], [18,'KEY'], [19,'FOOD'], [20,'MONEY'],
  [21,'PEN'], [22,'BOAT'], [23,'FOUNTAIN']
];

const EXTRA_FLAGS = [
  [0,'GLOW'], [1,'HUM'], [2,'NORENT'], [3,'NODONATE'], [4,'NOINVIS'],
  [5,'INVISIBLE'], [6,'MAGIC'], [7,'NODROP'], [8,'BLESS'],
  [9,'ANTI_GOOD'], [10,'ANTI_EVIL'], [11,'ANTI_NEUTRAL'],
  [12,'ANTI_MAGE'], [13,'ANTI_CLERIC'], [14,'ANTI_THIEF'],
  [15,'ANTI_WARRIOR'], [16,'NOSELL']
];

const WEAR_FLAGS = [
  [0,'TAKE'], [1,'FINGER'], [2,'NECK'], [3,'BODY'], [4,'HEAD'],
  [5,'LEGS'], [6,'FEET'], [7,'HANDS'], [8,'ARMS'], [9,'SHIELD'],
  [10,'ABOUT'], [11,'WAIST'], [12,'WRIST'], [13,'WIELD'], [14,'HOLD']
];

const APPLY_TYPES = [
  [0,'NONE'], [1,'STR'], [2,'DEX'], [3,'INT'], [4,'WIS'], [5,'CON'],
  [6,'CHA'], [7,'CLASS'], [8,'LEVEL'], [9,'AGE'], [10,'CHAR_WEIGHT'],
  [11,'CHAR_HEIGHT'], [12,'MAXMANA'], [13,'MAXHIT'], [14,'MAXMOVE'],
  [15,'GOLD'], [16,'EXP'], [17,'ARMOR'], [18,'HITROLL'], [19,'DAMROLL'],
  [20,'SAVING_PARA'], [21,'SAVING_ROD'], [22,'SAVING_PETRI'],
  [23,'SAVING_BREATH'], [24,'SAVING_SPELL']
];

const ZCMD_TYPES = ['M','O','G','E','P','D'];

/* ====================================================================
 * State
 * ==================================================================== */

let token = sessionStorage.getItem('olc_token');
let userName = sessionStorage.getItem('olc_name');
let userLevel = parseInt(sessionStorage.getItem('olc_level') || '0', 10);
let currentZone = null;

/* ====================================================================
 * API helpers
 * ==================================================================== */

async function api(method, path, body) {
  const opts = {
    method,
    headers: { 'Content-Type': 'application/json' }
  };
  if (token) opts.headers['Authorization'] = 'Bearer ' + token;
  if (body !== undefined) opts.body = JSON.stringify(body);
  const r = await fetch(path, opts);
  if (r.status === 401) { logout(); return null; }
  const ct = r.headers.get('Content-Type') || '';
  if (ct.includes('json')) return r.json();
  return null;
}

async function apiGet(path)       { return api('GET', path); }
async function apiPost(path, body){ return api('POST', path, body); }

/* ====================================================================
 * Auth
 * ==================================================================== */

function logout() {
  if (token) apiPost('/olc/logout');
  token = null; userName = null; userLevel = 0;
  sessionStorage.removeItem('olc_token');
  sessionStorage.removeItem('olc_name');
  sessionStorage.removeItem('olc_level');
  showLogin();
}

function showLogin() {
  document.getElementById('app').style.display = 'none';
  document.getElementById('login-screen').style.display = 'block';
  document.getElementById('user-info').textContent = '';
  document.getElementById('login-user').value = '';
  document.getElementById('login-pass').value = '';
  document.getElementById('login-err').textContent = '';
  document.getElementById('btn-logout').style.display = 'none';
}

function showApp() {
  document.getElementById('login-screen').style.display = 'none';
  document.getElementById('app').style.display = 'flex';
  document.getElementById('user-info').textContent = userName + ' [' + userLevel + ']';
  document.getElementById('btn-logout').style.display = 'inline-block';
  loadZones();
}

async function doLogin() {
  const name = document.getElementById('login-user').value.trim();
  const pass = document.getElementById('login-pass').value;
  if (!name || !pass) { document.getElementById('login-err').textContent = 'Enter name and password.'; return; }
  document.getElementById('login-err').textContent = '';
  try {
    const r = await apiPost('/olc/login', { name, password: pass });
    if (!r || r.error) {
      document.getElementById('login-err').textContent = r ? r.error : 'Login failed.';
      return;
    }
    token    = r.token;
    userName = r.name;
    userLevel= r.level;
    sessionStorage.setItem('olc_token', token);
    sessionStorage.setItem('olc_name',  userName);
    sessionStorage.setItem('olc_level', userLevel);
    showApp();
  } catch(e) {
    document.getElementById('login-err').textContent = 'Server error: ' + e.message;
  }
}

/* ====================================================================
 * Zone list
 * ==================================================================== */

async function loadZones() {
  const zones = await apiGet('/olc/zones');
  if (!zones) return;
  const ul = document.getElementById('zone-list');
  ul.innerHTML = '';
  zones.forEach(z => {
    const li = document.createElement('li');
    const num  = document.createElement('span');
    const name = document.createElement('span');
    num.className  = 'zone-num';
    name.className = 'zone-name';
    num.textContent  = z.number;
    name.textContent = z.name;
    li.appendChild(num);
    li.appendChild(name);
    if (z.can_edit) {
      const tag = document.createElement('span');
      tag.className = 'zone-mine';
      tag.textContent = '✓';
      li.appendChild(tag);
    } else if (z.closed) {
      const tag = document.createElement('span');
      tag.className = 'zone-locked';
      tag.textContent = '🔒';
      li.appendChild(tag);
    }
    li.addEventListener('click', () => selectZone(z, li));
    ul.appendChild(li);
  });
}

function selectZone(z, li) {
  document.querySelectorAll('#zone-list li').forEach(e => e.classList.remove('active'));
  li.classList.add('active');
  currentZone = z;
  showZoneBrowser(z);
}

function showZoneBrowser(z) {
  const ed = document.getElementById('editor');
  ed.innerHTML = '';

  const title = document.createElement('h2');
  title.textContent = 'Zone ' + z.number + ': ' + z.name +
      ' (rooms ' + z.bot + '–' + z.top + ')';
  title.style.cssText = 'color:#7ec8e3;margin-bottom:10px;font-size:14px;';
  ed.appendChild(title);

  const sec = document.createElement('div');
  sec.className = 'editor-section';
  sec.innerHTML = '<h3>Zone Commands</h3>';
  const zcmdBtn = document.createElement('button');
  zcmdBtn.className = 'btn btn-primary btn-small';
  zcmdBtn.textContent = 'Edit Zone Commands';
  zcmdBtn.onclick = () => loadZoneCmds(z.number);
  sec.appendChild(zcmdBtn);
  ed.appendChild(sec);

  /* rooms */
  const rsec = makeItemSection('Rooms', z.bot, z.top,
    n => { const c=document.createElement('span');c.className='item-chip item-room';c.textContent=n;c.onclick=()=>loadRoom(n);return c; });
  ed.appendChild(rsec);

  /* mobs */
  const msec = makeItemSection('Mobiles', z.bot, z.top,
    n => { const c=document.createElement('span');c.className='item-chip item-mob';c.textContent='M'+n;c.onclick=()=>loadMob(n);return c; });
  ed.appendChild(msec);

  /* objs */
  const osec = makeItemSection('Objects', z.bot, z.top,
    n => { const c=document.createElement('span');c.className='item-chip item-obj';c.textContent='O'+n;c.onclick=()=>loadObj(n);return c; });
  ed.appendChild(osec);
}

function makeItemSection(title, bot, top, makeChip) {
  const sec = document.createElement('div');
  sec.className = 'editor-section';
  sec.innerHTML = '<h3>' + title + ' (' + bot + '–' + top + ')</h3>';
  const list = document.createElement('div');
  list.className = 'zone-items-list';
  for (let n = bot; n <= top; n++) list.appendChild(makeChip(n));
  sec.appendChild(list);
  return sec;
}

function showEditorError(subject, msg) {
  const ed = document.getElementById('editor');
  ed.innerHTML = '';
  const div = document.createElement('div');
  div.style.cssText = 'color:#e06c75;margin-top:20px;padding:12px;border:1px solid #e06c75;';
  div.textContent = subject + ': ' + msg;
  ed.appendChild(div);
}

/* ====================================================================
 * Bit-flag helpers
 * ==================================================================== */

function makeFlagsGrid(defs, currentBits, idPrefix) {
  const grid = document.createElement('div');
  grid.className = 'flags-grid';
  defs.forEach(([bit, name]) => {
    const label = document.createElement('label');
    label.className = 'flag-item';
    const cb = document.createElement('input');
    cb.type = 'checkbox';
    cb.id = idPrefix + '_' + bit;
    cb.checked = !!(currentBits & (1 << bit));
    cb.dataset.bit = bit;
    label.appendChild(cb);
    label.appendChild(document.createTextNode(name));
    grid.appendChild(label);
  });
  return grid;
}

function readFlagsGrid(idPrefix, defs) {
  let bits = 0;
  defs.forEach(([bit]) => {
    const cb = document.getElementById(idPrefix + '_' + bit);
    if (cb && cb.checked) bits |= (1 << bit);
  });
  return bits;
}

function makeSelect(defs, selectedVal) {
  const sel = document.createElement('select');
  defs.forEach(([val, name]) => {
    const opt = document.createElement('option');
    opt.value = val;
    opt.textContent = name;
    if (val === selectedVal) opt.selected = true;
    sel.appendChild(opt);
  });
  return sel;
}

function fieldRow(label, widget) {
  const row = document.createElement('div');
  row.className = 'field-row';
  const lbl = document.createElement('span');
  lbl.className = 'field-label';
  lbl.textContent = label;
  row.appendChild(lbl);
  row.appendChild(widget);
  return row;
}

function textInput(val, size) {
  const inp = document.createElement('input');
  inp.type = 'text';
  inp.value = val || '';
  if (size) inp.style.width = size;
  return inp;
}

function numInput(val, min, max) {
  const inp = document.createElement('input');
  inp.type = 'number';
  inp.value = val;
  if (min !== undefined) inp.min = min;
  if (max !== undefined) inp.max = max;
  inp.style.width = '80px';
  return inp;
}

function textArea(val) {
  const ta = document.createElement('textarea');
  ta.value = val || '';
  return ta;
}

function saveBar(onSave) {
  const bar = document.createElement('div');
  bar.className = 'save-bar';
  const btn = document.createElement('button');
  btn.className = 'btn btn-primary';
  btn.textContent = 'Save';
  const status = document.createElement('span');
  btn.onclick = () => {
    status.className = '';
    status.textContent = 'Saving...';
    onSave().then(ok => {
      if (ok) { status.className = 'save-status'; status.textContent = 'Saved.'; }
      else    { status.className = 'save-error';  status.textContent = 'Save failed.'; }
    });
  };
  bar.appendChild(btn);
  bar.appendChild(status);
  return bar;
}

/* ====================================================================
 * Room editor
 * ==================================================================== */

async function loadRoom(vnum) {
  const data = await apiGet('/olc/room/' + vnum);
  if (!data || data.error) { showEditorError('Room ' + vnum, data ? data.error : 'Failed to load'); return; }
  renderRoomEditor(data);
}

function renderRoomEditor(r) {
  const ed = document.getElementById('editor');
  ed.innerHTML = '';

  const title = document.createElement('h2');
  title.textContent = 'Room Editor — #' + r.vnum;
  title.style.cssText = 'color:#7ec8e3;margin-bottom:10px;font-size:14px;';
  ed.appendChild(title);

  /* Basic fields */
  const baseSec = document.createElement('div');
  baseSec.className = 'editor-section';
  baseSec.innerHTML = '<h3>Basic</h3>';

  const nameInp = textInput(r.name);
  const descTa  = textArea(r.description);
  baseSec.appendChild(fieldRow('Name', nameInp));
  baseSec.appendChild(fieldRow('Description', descTa));

  const sectorSel = makeSelect(SECTOR_TYPES, r.sector_type);
  baseSec.appendChild(fieldRow('Sector', sectorSel));
  ed.appendChild(baseSec);

  /* Room flags */
  const flagSec = document.createElement('div');
  flagSec.className = 'editor-section';
  flagSec.innerHTML = '<h3>Room Flags</h3>';
  const flagsGrid = makeFlagsGrid(ROOM_FLAGS, r.room_flags, 'rf');
  flagSec.appendChild(flagsGrid);
  ed.appendChild(flagSec);

  /* Exits */
  const exitSec = document.createElement('div');
  exitSec.className = 'editor-section';
  exitSec.innerHTML = '<h3>Exits</h3>';
  const exitWidgets = [];
  r.exits.forEach((ex, d) => {
    const det = document.createElement('details');
    det.className = 'exit-section';
    const sum = document.createElement('summary');
    sum.textContent = DIR_NAMES[d] + (ex ? ' → ' + ex.to_room : ' (none)');
    det.appendChild(sum);

    const toRoomInp = numInput(ex ? ex.to_room : -1, -1);
    const keyInp    = numInput(ex ? ex.key    : -1, -1);
    const gdescTa   = textArea(ex ? ex.general_description : '');
    const kwInp     = textInput(ex ? ex.keyword : '');
    const exitFlags = makeFlagsGrid(EXIT_FLAGS, ex ? ex.exit_info : 0, 'ef' + d);

    det.appendChild(fieldRow('To room vnum', toRoomInp));
    det.appendChild(fieldRow('Key obj vnum', keyInp));
    det.appendChild(fieldRow('Description', gdescTa));
    det.appendChild(fieldRow('Keywords', kwInp));
    const efRow = document.createElement('div');
    efRow.className = 'field-row';
    const efl = document.createElement('span'); efl.className = 'field-label'; efl.textContent = 'Exit flags';
    efRow.appendChild(efl); efRow.appendChild(exitFlags);
    det.appendChild(efRow);

    exitWidgets.push({ toRoomInp, keyInp, gdescTa, kwInp });
    exitSec.appendChild(det);
  });
  ed.appendChild(exitSec);

  /* Extra descriptions */
  const edSec = document.createElement('div');
  edSec.className = 'editor-section';
  edSec.innerHTML = '<h3>Extra Descriptions</h3>';
  const edList = document.createElement('div');
  edList.id = 'ed-list';
  r.extra_descs.forEach(e => edList.appendChild(makeExtDescRow(e.keyword, e.description)));
  edSec.appendChild(edList);
  const addEdBtn = document.createElement('button');
  addEdBtn.className = 'btn btn-primary btn-small';
  addEdBtn.textContent = '+ Add';
  addEdBtn.onclick = () => edList.appendChild(makeExtDescRow('', ''));
  edSec.appendChild(addEdBtn);
  ed.appendChild(edSec);

  /* Save */
  ed.appendChild(saveBar(async () => {
    const exits = r.exits.map((ex, d) => {
      const w = exitWidgets[d];
      const to_room = parseInt(w.toRoomInp.value, 10);
      if (isNaN(to_room) || to_room < 0) return null;
      return {
        to_room,
        key:       parseInt(w.keyInp.value, 10),
        exit_info: readFlagsGrid('ef' + d, EXIT_FLAGS),
        general_description: w.gdescTa.value,
        keyword:   w.kwInp.value
      };
    });
    const extra_descs = [...document.querySelectorAll('.extdesc-row')].map(row => ({
      keyword:     row.querySelector('.ed-kw').value,
      description: row.querySelector('.ed-desc').value
    })).filter(e => e.keyword);
    const payload = {
      name:        nameInp.value,
      description: descTa.value,
      room_flags:  readFlagsGrid('rf', ROOM_FLAGS),
      sector_type: parseInt(sectorSel.value, 10),
      exits,
      extra_descs
    };
    const res = await apiPost('/olc/room/' + r.vnum, payload);
    return res && res.ok;
  }));
}

function makeExtDescRow(kw, desc) {
  const row = document.createElement('div');
  row.className = 'extdesc-row';
  const kwInp = textInput(kw, '200px');
  kwInp.className = 'ed-kw';
  const descTa = textArea(desc);
  descTa.className = 'ed-desc';
  const delBtn = document.createElement('button');
  delBtn.className = 'btn btn-danger btn-small';
  delBtn.textContent = 'Remove';
  delBtn.style.cssText = 'float:right;margin-bottom:4px;';
  delBtn.onclick = () => row.remove();
  row.appendChild(delBtn);
  row.appendChild(fieldRow('Keywords', kwInp));
  row.appendChild(fieldRow('Description', descTa));
  return row;
}

/* ====================================================================
 * Mob editor
 * ==================================================================== */

async function loadMob(vnum) {
  const data = await apiGet('/olc/mob/' + vnum);
  if (!data || data.error) { showEditorError('Mobile ' + vnum, data ? data.error : 'Failed to load'); return; }
  renderMobEditor(data);
}

function renderMobEditor(m) {
  const ed = document.getElementById('editor');
  ed.innerHTML = '';

  const title = document.createElement('h2');
  title.textContent = 'Mob Editor — #' + m.vnum;
  title.style.cssText = 'color:#7ec8e3;margin-bottom:10px;font-size:14px;';
  ed.appendChild(title);

  const baseSec = document.createElement('div');
  baseSec.className = 'editor-section';
  baseSec.innerHTML = '<h3>Identity</h3>';
  const aliasesInp  = textInput(m.aliases);
  const shortDescInp= textInput(m.short_desc);
  const longDescTa  = textArea(m.long_desc);
  const descTa      = textArea(m.description);
  baseSec.appendChild(fieldRow('Aliases', aliasesInp));
  baseSec.appendChild(fieldRow('Short desc', shortDescInp));
  baseSec.appendChild(fieldRow('Long desc', longDescTa));
  baseSec.appendChild(fieldRow('Description', descTa));
  ed.appendChild(baseSec);

  const statSec = document.createElement('div');
  statSec.className = 'editor-section';
  statSec.innerHTML = '<h3>Stats</h3>';
  const levelInp   = numInput(m.level,   0, 100);
  const alignInp   = numInput(m.alignment, -1000, 1000);
  const hitrollInp = numInput(m.hitroll, -100, 100);
  const acInp      = numInput(m.ac, -100, 100);
  const goldInp    = numInput(m.gold, 0);
  const expInp     = numInput(m.exp,  0);
  statSec.appendChild(fieldRow('Level', levelInp));
  statSec.appendChild(fieldRow('Alignment', alignInp));
  statSec.appendChild(fieldRow('Hitroll', hitrollInp));
  statSec.appendChild(fieldRow('Armor Class', acInp));
  statSec.appendChild(fieldRow('Gold', goldInp));
  statSec.appendChild(fieldRow('Exp', expInp));
  ed.appendChild(statSec);

  const diceSec = document.createElement('div');
  diceSec.className = 'editor-section';
  diceSec.innerHTML = '<h3>Dice</h3>';
  const hpNdInp  = numInput(m.hp_nodice,   1, 100);
  const hpSdInp  = numInput(m.hp_sizedice, 1, 100);
  const hpExInp  = numInput(m.hp_extra,    0, 10000);
  const dmNdInp  = numInput(m.dam_nodice,  1, 100);
  const dmSdInp  = numInput(m.dam_sizedice,1, 100);
  diceSec.appendChild(fieldRow('HP dice (N)', hpNdInp));
  diceSec.appendChild(fieldRow('HP dice (S)', hpSdInp));
  diceSec.appendChild(fieldRow('HP extra', hpExInp));
  diceSec.appendChild(fieldRow('Dam dice (N)', dmNdInp));
  diceSec.appendChild(fieldRow('Dam dice (S)', dmSdInp));
  ed.appendChild(diceSec);

  const abilSec = document.createElement('div');
  abilSec.className = 'editor-section';
  abilSec.innerHTML = '<h3>Abilities</h3>';
  const strInp    = numInput(m.str,     3, 18);
  const strAddInp = numInput(m.str_add, 0, 100);
  const intInp    = numInput(m.intel,   3, 18);
  const wisInp    = numInput(m.wis,     3, 18);
  const dexInp    = numInput(m.dex,     3, 18);
  const conInp    = numInput(m.con,     3, 18);
  const chaInp    = numInput(m.cha,     3, 18);
  abilSec.appendChild(fieldRow('STR', strInp));
  abilSec.appendChild(fieldRow('STR add', strAddInp));
  abilSec.appendChild(fieldRow('INT', intInp));
  abilSec.appendChild(fieldRow('WIS', wisInp));
  abilSec.appendChild(fieldRow('DEX', dexInp));
  abilSec.appendChild(fieldRow('CON', conInp));
  abilSec.appendChild(fieldRow('CHA', chaInp));
  ed.appendChild(abilSec);

  const miscSec = document.createElement('div');
  miscSec.className = 'editor-section';
  miscSec.innerHTML = '<h3>Misc</h3>';
  const sexSel    = makeSelect(SEX_TYPES, m.sex);
  const posSel    = makeSelect(POSITIONS, m.position);
  const defPosSel = makeSelect(POSITIONS, m.default_pos);
  const atkSel    = makeSelect(ATTACK_TYPES, m.attack_type);
  miscSec.appendChild(fieldRow('Sex', sexSel));
  miscSec.appendChild(fieldRow('Load position', posSel));
  miscSec.appendChild(fieldRow('Default pos', defPosSel));
  miscSec.appendChild(fieldRow('Attack type', atkSel));
  ed.appendChild(miscSec);

  const actSec = document.createElement('div');
  actSec.className = 'editor-section';
  actSec.innerHTML = '<h3>Action Flags</h3>';
  actSec.appendChild(makeFlagsGrid(MOB_FLAGS, m.act_flags, 'mf'));
  ed.appendChild(actSec);

  const affSec = document.createElement('div');
  affSec.className = 'editor-section';
  affSec.innerHTML = '<h3>Affect Flags</h3>';
  affSec.appendChild(makeFlagsGrid(AFF_FLAGS, m.aff_flags, 'af'));
  ed.appendChild(affSec);

  ed.appendChild(saveBar(async () => {
    const payload = {
      aliases:      aliasesInp.value,
      short_desc:   shortDescInp.value,
      long_desc:    longDescTa.value,
      description:  descTa.value,
      act_flags:    readFlagsGrid('mf', MOB_FLAGS),
      aff_flags:    readFlagsGrid('af', AFF_FLAGS),
      alignment:    parseInt(alignInp.value, 10),
      level:        parseInt(levelInp.value, 10),
      hitroll:      parseInt(hitrollInp.value, 10),
      ac:           parseInt(acInp.value, 10),
      hp_nodice:    parseInt(hpNdInp.value, 10),
      hp_sizedice:  parseInt(hpSdInp.value, 10),
      hp_extra:     parseInt(hpExInp.value, 10),
      dam_nodice:   parseInt(dmNdInp.value, 10),
      dam_sizedice: parseInt(dmSdInp.value, 10),
      gold:         parseInt(goldInp.value, 10),
      exp:          parseInt(expInp.value, 10),
      position:     parseInt(posSel.value, 10),
      default_pos:  parseInt(defPosSel.value, 10),
      sex:          parseInt(sexSel.value, 10),
      attack_type:  parseInt(atkSel.value, 10),
      str:          parseInt(strInp.value, 10),
      str_add:      parseInt(strAddInp.value, 10),
      intel:        parseInt(intInp.value, 10),
      wis:          parseInt(wisInp.value, 10),
      dex:          parseInt(dexInp.value, 10),
      con:          parseInt(conInp.value, 10),
      cha:          parseInt(chaInp.value, 10)
    };
    const res = await apiPost('/olc/mob/' + m.vnum, payload);
    return res && res.ok;
  }));
}

/* ====================================================================
 * Object editor
 * ==================================================================== */

async function loadObj(vnum) {
  const data = await apiGet('/olc/obj/' + vnum);
  if (!data || data.error) { showEditorError('Object ' + vnum, data ? data.error : 'Failed to load'); return; }
  renderObjEditor(data);
}

function renderObjEditor(o) {
  const ed = document.getElementById('editor');
  ed.innerHTML = '';

  const title = document.createElement('h2');
  title.textContent = 'Object Editor — #' + o.vnum;
  title.style.cssText = 'color:#7ec8e3;margin-bottom:10px;font-size:14px;';
  ed.appendChild(title);

  const baseSec = document.createElement('div');
  baseSec.className = 'editor-section';
  baseSec.innerHTML = '<h3>Identity</h3>';
  const aliasesInp = textInput(o.aliases);
  const rdescInp   = textInput(o.room_desc);
  const sdescInp   = textInput(o.short_desc);
  const adescTa    = textArea(o.action_desc);
  baseSec.appendChild(fieldRow('Aliases', aliasesInp));
  baseSec.appendChild(fieldRow('Room desc', rdescInp));
  baseSec.appendChild(fieldRow('Short desc', sdescInp));
  baseSec.appendChild(fieldRow('Action desc', adescTa));
  ed.appendChild(baseSec);

  const typeSec = document.createElement('div');
  typeSec.className = 'editor-section';
  typeSec.innerHTML = '<h3>Type & Values</h3>';
  const typeSel = makeSelect(ITEM_TYPES, o.type);
  typeSec.appendChild(fieldRow('Type', typeSel));
  const v0 = numInput(o.val0); const v1 = numInput(o.val1);
  const v2 = numInput(o.val2); const v3 = numInput(o.val3);
  typeSec.appendChild(fieldRow('Value 0', v0));
  typeSec.appendChild(fieldRow('Value 1', v1));
  typeSec.appendChild(fieldRow('Value 2', v2));
  typeSec.appendChild(fieldRow('Value 3', v3));
  const weightInp = numInput(o.weight, 0);
  const costInp   = numInput(o.cost, 0);
  const rentInp   = numInput(o.rent, 0);
  typeSec.appendChild(fieldRow('Weight', weightInp));
  typeSec.appendChild(fieldRow('Cost', costInp));
  typeSec.appendChild(fieldRow('Rent/day', rentInp));
  ed.appendChild(typeSec);

  const efSec = document.createElement('div');
  efSec.className = 'editor-section';
  efSec.innerHTML = '<h3>Extra Flags</h3>';
  efSec.appendChild(makeFlagsGrid(EXTRA_FLAGS, o.extra_flags, 'oef'));
  ed.appendChild(efSec);

  const wfSec = document.createElement('div');
  wfSec.className = 'editor-section';
  wfSec.innerHTML = '<h3>Wear Flags</h3>';
  wfSec.appendChild(makeFlagsGrid(WEAR_FLAGS, o.wear_flags, 'owf'));
  ed.appendChild(wfSec);

  /* Affects */
  const affSec = document.createElement('div');
  affSec.className = 'editor-section';
  affSec.innerHTML = '<h3>Affects</h3>';
  const affWidgets = o.affects.map((a, i) => {
    const row = document.createElement('div');
    row.className = 'field-row';
    row.innerHTML = '<span class="field-label">Affect ' + (i+1) + '</span>';
    const locSel = makeSelect(APPLY_TYPES, a.location);
    locSel.style.width = '160px';
    const modInp = numInput(a.modifier, -100, 100);
    row.appendChild(locSel);
    row.appendChild(modInp);
    affSec.appendChild(row);
    return { locSel, modInp };
  });
  ed.appendChild(affSec);

  /* Extra descs */
  const edSec = document.createElement('div');
  edSec.className = 'editor-section';
  edSec.innerHTML = '<h3>Extra Descriptions</h3>';
  const edList = document.createElement('div');
  edList.id = 'oed-list';
  o.extra_descs.forEach(e => edList.appendChild(makeExtDescRow(e.keyword, e.description)));
  edSec.appendChild(edList);
  const addEdBtn = document.createElement('button');
  addEdBtn.className = 'btn btn-primary btn-small';
  addEdBtn.textContent = '+ Add';
  addEdBtn.onclick = () => edList.appendChild(makeExtDescRow('', ''));
  edSec.appendChild(addEdBtn);
  ed.appendChild(edSec);

  ed.appendChild(saveBar(async () => {
    const extra_descs = [...document.querySelectorAll('#oed-list .extdesc-row')].map(row => ({
      keyword:     row.querySelector('.ed-kw').value,
      description: row.querySelector('.ed-desc').value
    })).filter(e => e.keyword);
    const payload = {
      aliases:     aliasesInp.value,
      room_desc:   rdescInp.value,
      short_desc:  sdescInp.value,
      action_desc: adescTa.value,
      type:        parseInt(typeSel.value, 10),
      extra_flags: readFlagsGrid('oef', EXTRA_FLAGS),
      wear_flags:  readFlagsGrid('owf', WEAR_FLAGS),
      weight:      parseInt(weightInp.value, 10),
      cost:        parseInt(costInp.value, 10),
      rent:        parseInt(rentInp.value, 10),
      val0:        parseInt(v0.value, 10),
      val1:        parseInt(v1.value, 10),
      val2:        parseInt(v2.value, 10),
      val3:        parseInt(v3.value, 10),
      affects:     affWidgets.map(w => ({
        location: parseInt(w.locSel.value, 10),
        modifier: parseInt(w.modInp.value, 10)
      })),
      extra_descs
    };
    const res = await apiPost('/olc/obj/' + o.vnum, payload);
    return res && res.ok;
  }));
}

/* ====================================================================
 * Zone commands editor
 * ==================================================================== */

async function loadZoneCmds(znum) {
  const data = await apiGet('/olc/zone/' + znum);
  if (!data || data.error) { alert(data ? data.error : 'Failed to load zone'); return; }
  renderZoneCmds(znum, data);
}

function renderZoneCmds(znum, z) {
  const ed = document.getElementById('editor');
  ed.innerHTML = '';

  const title = document.createElement('h2');
  title.textContent = 'Zone Commands — ' + z.name;
  title.style.cssText = 'color:#7ec8e3;margin-bottom:10px;font-size:14px;';
  ed.appendChild(title);

  const sec = document.createElement('div');
  sec.className = 'editor-section';
  const addBtn = document.createElement('button');
  addBtn.className = 'btn btn-primary btn-small';
  addBtn.textContent = '+ Add Command';
  addBtn.style.marginBottom = '8px';
  sec.appendChild(addBtn);

  const table = document.createElement('table');
  table.id = 'zcmd-table';
  table.innerHTML = '<thead><tr>' +
    '<th>Cmd</th><th>If</th><th>Arg1 (vnum)</th><th>Arg2 (max/pos)</th>' +
    '<th>Arg3 (room/container)</th><th>Del</th>' +
    '</tr></thead>';
  const tbody = document.createElement('tbody');
  table.appendChild(tbody);
  sec.appendChild(table);

  function addRow(cmd) {
    const tr = document.createElement('tr');
    const cmdSel = makeSelect(ZCMD_TYPES.map(c=>[c,c]), cmd ? cmd.command : 'M');
    cmdSel.style.width = '50px';
    const ifCb  = document.createElement('input'); ifCb.type='checkbox'; ifCb.checked = cmd ? !!cmd.if_flag : false;
    const a1Inp = numInput(cmd ? cmd.arg1 : 0, -1);
    const a2Inp = numInput(cmd ? cmd.arg2 : 1, 0);
    const a3Inp = numInput(cmd ? cmd.arg3 : -1, -1);
    const delBtn= document.createElement('button'); delBtn.className='btn btn-danger btn-small'; delBtn.textContent='✕';
    delBtn.onclick = () => tr.remove();
    [cmdSel, ifCb, a1Inp, a2Inp, a3Inp, delBtn].forEach(w => {
      const td = document.createElement('td');
      td.appendChild(w);
      tr.appendChild(td);
    });
    tbody.appendChild(tr);
    return tr;
  }

  (z.commands || []).forEach(c => addRow(c));
  addBtn.onclick = () => addRow(null);
  ed.appendChild(sec);

  ed.appendChild(saveBar(async () => {
    const commands = [...tbody.querySelectorAll('tr')].map(tr => {
      const cells = tr.querySelectorAll('td');
      return {
        command:  cells[0].querySelector('select').value,
        if_flag:  cells[1].querySelector('input').checked ? 1 : 0,
        arg1:     parseInt(cells[2].querySelector('input').value, 10),
        arg2:     parseInt(cells[3].querySelector('input').value, 10),
        arg3:     parseInt(cells[4].querySelector('input').value, 10)
      };
    });
    const res = await apiPost('/olc/zone/' + znum, { commands });
    return res && res.ok;
  }));
}

/* ====================================================================
 * Init
 * ==================================================================== */

document.addEventListener('DOMContentLoaded', () => {
  document.getElementById('btn-login').addEventListener('click', doLogin);
  document.getElementById('btn-logout').addEventListener('click', logout);
  document.getElementById('login-pass').addEventListener('keydown', e => {
    if (e.key === 'Enter') doLogin();
  });

  if (token) showApp();
  else       showLogin();
});
