#!/usr/bin/env node

/**
 * Noize generator
 *
 * @author Viacheslav Lotsmanov
 */

var jackConnector = require('../index.js');
var jackClientName = 'JACK connector - client registration watcher';

console.log('Opening JACK client...');
jackConnector.openClientSync(jackClientName);

function clientRegistrationCallback(name, registered) {
    console.log('client='+name+' '+(registered? 'registered':'unregistered'));
}

console.log('Registering client-registration callback...');
jackConnector.registerClientRegistrationCallback(clientRegistrationCallback);

console.log('Activating JACK client...');
jackConnector.activateSync();

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
