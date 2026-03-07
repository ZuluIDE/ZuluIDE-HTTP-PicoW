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
    if (elm) elm.innerHTML = version.clientAPIVersion;

    elm = document.getElementById('cfv');
    if (elm) elm.innerHTML = version.clientFWVersion;

    elm = document.getElementById('sav');
    if (elm && version.serverAPIVersion)
     elm.innerHTML = version.serverAPIVersion;

    if (version.message)
    {
     elm = document.getElementById('avm');
     if (elm) elm.innerHTML = 'Message: ' + version.message;
    }
}
