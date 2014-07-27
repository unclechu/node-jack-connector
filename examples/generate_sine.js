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

//var freq = 440;
var freq = 432; // SIC!
var sr = jackConnector.getSampleRateSync();
var step = Math.round(sr / freq);
var last = -1;

function relativeVal(relSrc, relMin, relMax, min, max) {
	if (relSrc < relMin) relSrc = relMin;
	else if (relSrc > relMax) relSrc = relMax;
	return ((relSrc - relMin) * (max-min) / (relMax - relMin)) + min;
}

function audioProcess(err, nframes) {
	if (err) {
		console.error(err);
		process.exit(1);
	}

	var ret = [], deg = 0;
	for (var i=0; i<nframes; i++) {
		if (++last >= step) last = 0;
		deg = relativeVal(last, 0, step, 0, 360);
		ret.push(Math.sin( deg * (Math.PI / 180) ));
	}
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
