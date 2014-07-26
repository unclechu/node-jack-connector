#!/usr/bin/env node

/**
 * Sine to output
 *
 * @author Viacheslav Lotsmanov
 */

var jackConnector = require('../index.js');
var jackClientName = 'JACK connector - sine';

console.log('Opening JACK client...');
jackConnector.openClientSync(jackClientName);

console.log('Registering JACK ports...');
jackConnector.registerOutPortSync('out');

function audioProcess(nframes) {
	var ret = [];
	for (var i=0; i<nframes; i++) ret.push((Math.random() * 2) - 1);
	return { out: ret };
}

console.log('Binding audio-process callback...');
jackConnector.bindProcessSync(audioProcess);

console.log('Activating JACK client...');
jackConnector.activateSync();

console.log('Auto-connecting to hardware ports...');
jackConnector.connectPortSync(jackClientName + ':out', 'system:playback_1');
jackConnector.connectPortSync(jackClientName + ':out', 'system:playback_2');

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
