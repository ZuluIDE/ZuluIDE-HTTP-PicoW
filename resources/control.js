var timer;
function showStatus() {
 document.getElementById('si').setAttribute('class', 'hdn');
 document.getElementById('st').removeAttribute('class');
 setTimeout(refresh, 1500);
}
function onload() {
 let elm = document.getElementById('ar');
 elm.checked = false;
 setTimeout(refresh, 1500);
}
function ejectClk() {
 fetch('eject')
 .then(r => r.json())
 .then(s => { if (s.status != 'ok') alert('Eject failed.'); else setTimeout(refresh, 1500); });
}
function refresh() {
 fetch('status')
 .then(response => response.json())
 .then(status => updateStatus(status));
}
function updateStatus(status) {
 let elm = document.getElementById('dt');
 elm.innerHTML = (status.isPrimary ? 'Primary' : 'Secondary') + ' CD-ROM';
 elm = document.getElementById('img');
 elm.innerHTML = status.image ? status.image.filename : '';
}
function autoRefresh() {
 let elm = document.getElementById('ar');
 if (elm.checked) {
  timer = setInterval(refresh, 45000);
 } else if (timer) {
  clearTimeout(timer);
 }
}