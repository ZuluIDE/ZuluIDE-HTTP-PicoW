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
