#!/usr/bin/env node

var jackConnector = require('../index.js');
jackConnector.openClientSync('JACK_connector_sin_out_example');
jackConnector.registerInPortSync('cap');
jackConnector.registerInPortSync('cap2');
jackConnector.registerInPortSync('cap3');
jackConnector.registerInPortSync('cap4');
jackConnector.registerInPortSync('cap5');
jackConnector.registerOutPortSync('playback_mono');

function audioProcess(nframes, capture) {
	console.log(nframes, capture);
	return;
}

jackConnector.bindProcessSync(audioProcess);
jackConnector.activateSync();

(function mainLoop() {
	console.log('main loop');
	setTimeout(mainLoop, 1000);
	/*jackConnector.registerInPortSync('cap11');
	jackConnector.unregisterPortSync('cap11');
	setTimeout(mainLoop, 1);*/
})();
