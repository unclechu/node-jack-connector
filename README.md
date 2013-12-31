# Node.JS JACK-connector

Bindings JACK-Audio-Connection-Kit for Node.JS

# Install
```
npm install jack-connector
```

# Build requirements
libjack2, libjack2-devel

# How to use
```javascript
var jackConnector = require('jack-connector');
jackConnector.openClientSync('JACK_connector_client_name');
jackConnector.registerOutPortSync('output');
jackConnector.registerInPortSync('input');
jackConnector.activateSync();
(function mainLoop() {
    setTimeout(function () { mainLoop(); }, 10);
})();
```
