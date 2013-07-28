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
jackConnector.activateSync();
jackConnector.registerOutPortSync('output');
jackConnector.registerInPortSync('input');
function mainLoop() {
    process.nextTick(function () { mainLoop(); });
}
```
