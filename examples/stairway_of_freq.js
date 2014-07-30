#!/usr/bin/env node

/**
 * Stairway of frequencies
 *
 * @author Viacheslav Lotsmanov
 */

var jackConnector = require('../index.js');
var jackClientName = 'JACK connector - stairway of frequencies';

console.log('Opening JACK client...');
jackConnector.openClientSync(jackClientName);

console.log('Registering JACK ports...');
jackConnector.registerOutPortSync('out');

var sampleRate = jackConnector.getSampleRateSync();

// from 20Hz to 20kHz
var freqMin = 20;
var freqMax = 20000;
var freqCur;
var freqStep = 8;

var phaseInc; // phase increment
var phase = 0;

if (process.argv.length < 3) {
	console.error(
		'Please, choose OSC type by first argument:\n'+
		'  1 - sine\n'+
		'  2 - saw\n'+
		'  3 - square\n'+
		'  4 - triangle'
	);
	process.exit(1);
}

var oscType;
switch (process.argv[2]) {
	case '1': case '2': case '3': case '4':
		oscType = parseInt(process.argv[2], 10) - 1;
		break;
	default:
		throw new Error('Incorrect OSC type by first argument ("'+
			process.argv[2] +'")');
}

var twoPI = Math.PI * 2;

var upFreqCount = 0;

var upFreqSpeed = 5; // ms
// transform to samples
upFreqSpeed *= sampleRate;
upFreqSpeed /= 1000;

function updateIncrement() {
	phaseInc = ((freqCur * 2) * Math.PI) / sampleRate;
}

function setFrequency(freqVal) {
	freqCur = freqVal;
	updateIncrement();
}

function incFrequency() {
	if (freqCur + freqStep > freqMax) freqCur = freqMin;
	else setFrequency(freqCur + freqStep);
}

function osc(nframes, upFreqAt) { // {{{1
	var type = oscType % 4;
	var buf = new Array(nframes);
	switch (type) {
	case 0: // sine
		for (var i=0; i<nframes; i++) {
			if (upFreqAt > -1 && i == upFreqAt) incFrequency();
			buf[i] = Math.sin(phase);
			phase += phaseInc;
			while (phase >= twoPI) phase -= twoPI;
		}
		break;
	case 1: // saw
		for (var i=0; i<nframes; i++) {
			if (upFreqAt > -1 && i == upFreqAt) incFrequency();
			buf[i] = 1.0 - ((2.0 * phase) / twoPI);
			phase += phaseInc;
			while (phase >= twoPI) phase -= twoPI;
		}
		break;
	case 2: // square
		for (var i=0; i<nframes; i++) {
			if (upFreqAt > -1 && i == upFreqAt) incFrequency();
			if (phase <= Math.PI) buf[i] = 1.0;
			else buf[i] = -1.0;
			phase += phaseInc;
			while (phase >= twoPI) phase -= twoPI;
		}
		break;
	case 3: // triangle
		var val;
		for (var i=0; i<nframes; i++) {
			if (upFreqAt > -1 && i == upFreqAt) incFrequency();
			val = -1.0 + ((2.0 * phase) / twoPI);
			buf[i] = 2.0 * (Math.abs(val) - 0.5);
			phase += phaseInc;
			while (phase >= twoPI) phase -= twoPI;
		}
		break;
	}
	return buf;
} // osc() }}}1

setFrequency(freqMin);

function audioProcess(err, nframes) {
	if (err) {
		console.error(err);
		process.exit(1);
		return;
	}

	var upFreqAt = -1;
	upFreqCount += nframes;
	if (upFreqCount >= upFreqSpeed)
		upFreqAt = upFreqCount -= upFreqSpeed;
	return { out: osc(nframes, upFreqAt) }; // port name: buffer array
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
	jackConnector.closeClient(function (err) {
		if (err) {
			console.error(err);
			process.exit(1);
			return;
		}

		console.log('Exiting...');
		process.exit(0);
	});
});
