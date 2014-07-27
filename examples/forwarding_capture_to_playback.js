#!/usr/bin/env node

/**
 * Forwarding capture to playback demonstration
 *
 * @author Viacheslav Lotsmanov
 */

var jackConnector = require('../index.js');
var jackClientName = 'JACK connector - forwarding capture to playback example';

console.log('Opening JACK client...');
jackConnector.openClientSync(jackClientName);

console.log('Registering JACK ports...');
jackConnector.registerInPortSync('in_l');
jackConnector.registerInPortSync('in_r');
jackConnector.registerOutPortSync('out_l');
jackConnector.registerOutPortSync('out_r');

function audioProcess(err, nframes, capture) {
	if (err) {
		console.error(err);
		process.exit(1);
	}

	return {
		out_l: capture.in_l,
		out_r: capture.in_r,
	};
}

console.log('Binding audio-process callback...');
jackConnector.bindProcessSync(audioProcess);

console.log('Activating JACK client...');
jackConnector.activateSync();

console.log('Auto-connecting to hardware ports...');
jackConnector.connectPortSync('system:capture_1', jackClientName + ':in_l');
jackConnector.connectPortSync('system:capture_2', jackClientName + ':in_r');
jackConnector.connectPortSync(jackClientName + ':out_l', 'system:playback_1');
jackConnector.connectPortSync(jackClientName + ':out_r', 'system:playback_2');

(function mainLoop() {
	console.log('Main loop is started.');
	setTimeout(mainLoop, 1000000000);
})();

process.on('SIGTERM', function () {
	console.log('Deactivating JACK client...');
	jackConnector.deactivateSync();
	console.log('Closing JACK client...');
	jackConnector.closeClient(function () {
		console.log('Exiting...');
		process.exit(0);
	});
});
