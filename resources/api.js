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
