var timer;
function showStatus() {
 document.getElementById('si').setAttribute('class', 'hdn');
 document.getElementById('st').removeAttribute('class');
 setTimeout(refresh, 1500);
}

document.addEventListener('DOMContentLoaded', (event) => {
 let elm = document.getElementById('ar');
 elm.checked = false;
 refresh()
});

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

function selectClk() {
 document.getElementById('st').setAttribute('class', 'hdn');
 document.getElementById('si').setAttribute('class', 'img');
 let ni = document.getElementById('newImg');
 for (let a in ni.options) { ni.options.remove(0); }
 loadFns();
}
function cancelClicked() {
 showStatus();
}
function loadClicked() {
 let si = document.getElementById('newImg');
 fetch('image?' +  new URLSearchParams({ imageName: si.value}))
 .then(r => r.json())
 .then(s => { 
  if (s.status != 'ok') alert('Select failed.');});
 showStatus();
}

var imgs=[];
function loadFns() {
 fetch('filenames')
 .then(response => response.json())
 .then(fns => {
  if (fns.status == 'wait') {setTimeout(loadFns, 50);}
  else if (fns.status == 'overflow') {loadImgs();}
  else { writeFn(document.getElementById('newImg'), fns);}}); 
}
function loadImgs() {
 fetch('nextImage')
 .then(response => response.json())
 .then(image => {
  if (image.status == 'wait') {setTimeout(loadImgs, 50);}
  else if (image.status == 'done') { loadImages(document.getElementById('newImg'));}
  else {imgs.push(image); loadImgs();}});
}
 function writeFn(ni, fns) {
  for (let fn of fns.filenames) {
   ni.add(new Option(fn));
  }
}
function loadImages(ni) {
 for (let i in imgs) {
  ni.add(new Option(imgs[i].filename));
 }
}
