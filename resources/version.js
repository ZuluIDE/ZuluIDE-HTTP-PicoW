document.addEventListener('DOMContentLoaded', (event) => {
    load_version();
});

function load_version()
{
    fetch('version')
    .then(response => response.json())
    .then(version => updateVersion(version));
}

function updateVersion(version) {
    let elm = document.getElementById('cav');
    elm.innerHTML = version.clientAPIVersion;
    elm = document.getElementById('sav');
    if (version.serverAPIVersion)
     elm.innerHTML = version.serverAPIVersion;
    if (version.message)
    {
     elm = document.getElementById('avm');
     elm.innerHTML = 'Message: ' + version.message; 
    }
}