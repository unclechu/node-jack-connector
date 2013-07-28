/**
 * JACK Connector
 * Bindings JACK-Audio-Connection-Kit for Node.JS
 *
 * @author Viacheslav Lotsmanov (unclechu) <lotsmanov89@gmail.com>
 * @license MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013 Viacheslav Lotsmanov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define VERSION "0.1"

#include <node.h>
#include <jack/jack.h>
#include <errno.h>

#define ERR_MSG_NEED_TO_OPEN_JACK_CLIENT "JACK-client is not opened, need to open JACK-client"
#define THROW_ERR(Message) \
        { \
            ThrowException(Exception::Error(String::New(Message))); \
            return scope.Close(Undefined()); \
        }
#define STR_SIZE 256

using namespace v8;

jack_client_t *client = 0;
short client_active = 0;
char client_name[STR_SIZE];

Handle<Array> get_ports(bool withOwn, unsigned long flags);
int check_port_connection(const char *src_port_name, const char *dst_port_name);
bool check_port_exists(char *check_port_name, unsigned long flags);

/**
 * Get version of this module
 *
 * @public
 * @returns {v8::String} version
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   console.log(jackConnector.getVersion());
 *     // string of version, see VERSION macros
 */
Handle<Value> getVersion(const Arguments &args)
{
    HandleScope scope;
    return scope.Close(String::New(VERSION));
}

/**
 * Check JACK-client for opened status
 *
 * @public
 * @returns {v8::Boolean} result True - JACK-client is opened, false - JACK-client is closed
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   console.log(jackConnector.checkClientOpenedSync());
 *     // true if client opened or false if closed
 */
Handle<Value> checkClientOpenedSync(const Arguments &args)
{
    HandleScope scope;
    return scope.Close(Boolean::New(client != 0));
}

/**
 * Open JACK-client
 *
 * @public
 * @param {v8::String} client_name JACK-client name
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   jackConnector.openClientSync('JACK_connector');
 */
Handle<Value> openClientSync(const Arguments &args)
{
    HandleScope scope;

    if (client != 0) THROW_ERR("You need close old JACK-client before open new");

    String::AsciiValue arg_client_name(args[0]->ToString());
    char *client_name = *arg_client_name;

    for (unsigned int i=0; ; i++) {
        if (client_name[i] == '\0' || i>=STR_SIZE-1) {
            if (i==0) {
                client_name[0] = '\0';
                THROW_ERR("Empty JACK-client name");
            }
            client_name[i] = '\0';
            break;
        }

        ::client_name[i] = client_name[i];
    }

    client = jack_client_open(client_name, JackNullOption, 0);
    if (client == 0) {
        client_name[0] = '\0';
        THROW_ERR("Couldn't create JACK-client");
    }

    return scope.Close(Undefined());
}

/**
 * Close JACK-client
 *
 * @public
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   jackConnector.closeClientSync();
 */
Handle<Value> closeClientSync(const Arguments &args)
{
    HandleScope scope;
    if (client == 0) THROW_ERR("JACK-client already closed");

    if (jack_client_close(client) != 0) THROW_ERR("Couldn't close JACK-client");

    client = 0;

    return scope.Close(Undefined());
}

/**
 * Register new port for this client
 *
 * @public
 * @param {v8::String} port_name Full port name
 * @param {v8::Integer} port_type See: enum jack_flags
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   jackConnector.registerInPortSync('in_1');
 *   jackConnector.registerInPortSync('in_2');
 */
Handle<Value> registerInPortSync(const Arguments &args)
{
    HandleScope scope;
    if (client == 0) THROW_ERR(ERR_MSG_NEED_TO_OPEN_JACK_CLIENT);

    String::AsciiValue port_name(args[0]->ToString());

    jack_port_register( client,
                        *port_name,
                        JACK_DEFAULT_AUDIO_TYPE,
                        JackPortIsInput,
                        0 );

    return scope.Close(Undefined());
}

/**
 * Register new port for this client
 *
 * @public
 * @param {v8::String} port_name Full port name
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   jackConnector.registerOutPortSync('out_1');
 *   jackConnector.registerOutPortSync('out_2');
 */
Handle<Value> registerOutPortSync(const Arguments &args)
{
    HandleScope scope;
    if (client == 0) THROW_ERR(ERR_MSG_NEED_TO_OPEN_JACK_CLIENT);

    String::AsciiValue port_name(args[0]->ToString());

    jack_port_register( client,
                        *port_name,
                        JACK_DEFAULT_AUDIO_TYPE,
                        JackPortIsOutput,
                        0 );

    return scope.Close(Undefined());
}

/**
 * Unregister port for this client
 *
 * @public
 * @param {v8::String} port_name Full port name
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   jackConnector.unregisterPortSync('out_1', jackConnector.IsOutput);
 *   jackConnector.unregisterPortSync('out_2', jackConnector.IsOutput);
 */
Handle<Value> unregisterPortSync(const Arguments &args)
{
    HandleScope scope;
    if (client == 0) THROW_ERR(ERR_MSG_NEED_TO_OPEN_JACK_CLIENT);

    String::AsciiValue arg_port_name(args[0]->ToString());
    char full_port_name[STR_SIZE];
    char *port_name = *arg_port_name;

    for (int i=0, n=0, m=0; ; i++, m++) {
        if (n == 0) {
            if (::client_name[m] == '\0') {
                full_port_name[i] = ':';
                m = -1;
                n = 1;
            } else {
                full_port_name[i] = ::client_name[m];
            }
        } else {
            if (port_name[m] == '\0') {
                full_port_name[i] = '\0';
                break;
            } else {
                full_port_name[i] = port_name[m];
            }
        }
    }

    jack_port_t *port = jack_port_by_name(client, full_port_name);

    if (jack_port_unregister(client, port) != 0) THROW_ERR("Couldn't unregister JACK-port");

    return scope.Close(Undefined());
}

/**
 * Check JACK-client for active
 *
 * @public
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   if (jackConnector.checkActiveSync())
 *     console.log('JACK-client is active');
 *   else
 *     console.log('JACK-client is not active');
 * @returns {v8::Boolean} result True - client is active, false - client is not active
 */
Handle<Value> checkActiveSync(const Arguments &args)
{
    HandleScope scope;
    if (client == 0) THROW_ERR(ERR_MSG_NEED_TO_OPEN_JACK_CLIENT);

    if (::client_active > 0) {
        return scope.Close(Boolean::New(true));
    } else {
        return scope.Close(Boolean::New(false));
    }
}

/**
 * Activate JACK-client
 *
 * @public
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   jackConnector.activateSync();
 */
Handle<Value> activateSync(const Arguments &args)
{
    HandleScope scope;
    if (client == 0) THROW_ERR(ERR_MSG_NEED_TO_OPEN_JACK_CLIENT);

    if (client_active) THROW_ERR("JACK-client already activated");

    if (jack_activate(client) != 0) THROW_ERR("Couldn't activate JACK-client");

    client_active = 1;

    return scope.Close(Undefined());
}

/**
 * Deactivate JACK-client
 *
 * @public
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   jackConnector.deactivateSync();
 */
Handle<Value> deactivateSync(const Arguments &args)
{
    HandleScope scope;
    if (client == 0) THROW_ERR(ERR_MSG_NEED_TO_OPEN_JACK_CLIENT);

    if (! client_active) THROW_ERR("JACK-client is not active");

    if (jack_deactivate(client) != 0) THROW_ERR("Couldn't deactivate JACK-client");

    client_active = 0;

    return scope.Close(Undefined());
}

/**
 * Connect port to port
 *
 * @public
 * @param {v8::String} sourcePort Full name of source port
 * @param {v8::String} destinationPort Full name of destination port
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   jackConnector.connectPortSync('system:capture_1', 'system:playback_1');
 */
Handle<Value> connectPortSync(const Arguments &args)
{
    HandleScope scope;
    if (client == 0) THROW_ERR(ERR_MSG_NEED_TO_OPEN_JACK_CLIENT);

    if (! client_active) THROW_ERR("JACK-client is not active");

    String::AsciiValue src_port_name(args[0]->ToString());
    jack_port_t *src_port = jack_port_by_name(client, *src_port_name);
    if (!src_port) THROW_ERR("Non existing source port");

    String::AsciiValue dst_port_name(args[1]->ToString());
    jack_port_t *dst_port = jack_port_by_name(client, *dst_port_name);
    if (!dst_port) THROW_ERR("Non existing destination port");

    if (! client_active
    && (jack_port_is_mine(client, src_port) || jack_port_is_mine(client, dst_port))) {
        THROW_ERR("Jack client must be activated to connect own ports");
    }

    int error = jack_connect(client, *src_port_name, *dst_port_name);
    if (error != 0 && error != EEXIST) THROW_ERR("Failed to connect ports");

    return scope.Close(Undefined());
}

/**
 * Disconnect ports
 *
 * @public
 * @param {v8::String} sourcePort Full name of source port
 * @param {v8::String} destinationPort Full name of destination port
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   jackConnector.disconnectPortSync('system:capture_1', 'system:playback_1');
 */
Handle<Value> disconnectPortSync(const Arguments &args)
{
    HandleScope scope;
    if (client == 0) THROW_ERR(ERR_MSG_NEED_TO_OPEN_JACK_CLIENT);

    if (! client_active) THROW_ERR("JACK-client is not active");

    String::AsciiValue src_port_name(args[0]->ToString());
    jack_port_t *src_port = jack_port_by_name(client, *src_port_name);
    if (!src_port) THROW_ERR("Non existing source port");

    String::AsciiValue dst_port_name(args[1]->ToString());
    jack_port_t *dst_port = jack_port_by_name(client, *dst_port_name);
    if (!dst_port) THROW_ERR("Non existing destination port");

    if (check_port_connection(*src_port_name, *dst_port_name)) {
        if (jack_disconnect(client, *src_port_name, *dst_port_name))
            THROW_ERR("Failed to disconnect ports");
    }

    return scope.Close(Undefined());
}

/**
 * Get all JACK-ports list
 *
 * @public
 * @param {v8::Boolean} [withOwn] Default: true
 * @returns {v8::Array} allPortsList Array of full ports names strings
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   console.log(jackConnector.getAllPortsSync());
 *     // prints: [ "system:playback_1", "system:playback_2",
 *     //           "system:capture_1", "system:capture_2" ]
 */
Handle<Value> getAllPortsSync(const Arguments &args)
{
    HandleScope scope;
    if (client == 0) THROW_ERR(ERR_MSG_NEED_TO_OPEN_JACK_CLIENT);

    bool withOwn = true;
    if (args.Length() > 0 && (args[0]->IsBoolean() || args[0]->IsNumber())) {
        withOwn = args[0]->ToBoolean()->BooleanValue();
    }

    Handle<Array> allPortsList = get_ports(withOwn, 0);

    return scope.Close(allPortsList);
}

/**
 * Get output JACK-ports list
 *
 * @public
 * @param {v8::Boolean} [withOwn] Default: true
 * @returns {v8::Array} outPortsList Array of full ports names strings
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   console.log(jackConnector.getOutPortsSync());
 *     // prints: [ "system:capture_1", "system:capture_2" ]
 */
Handle<Value> getOutPortsSync(const Arguments &args)
{
    HandleScope scope;
    if (client == 0) THROW_ERR(ERR_MSG_NEED_TO_OPEN_JACK_CLIENT);

    bool withOwn = true;
    if (args.Length() > 0 && (args[0]->IsBoolean() || args[0]->IsNumber())) {
        withOwn = args[0]->ToBoolean()->BooleanValue();
    }

    Handle<Array> outPortsList = get_ports(withOwn, JackPortIsOutput);

    return scope.Close(outPortsList);
}

/**
 * Get input JACK-ports list
 *
 * @public
 * @param {v8::Boolean} [withOwn] Default: true
 * @returns {v8::Array} inPortsList Array of full ports names strings
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   console.log(jackConnector.getInPortsSync());
 *     // prints: [ "system:playback_1", "system:playback_2" ]
 */
Handle<Value> getInPortsSync(const Arguments &args)
{
    HandleScope scope;
    if (client == 0) THROW_ERR(ERR_MSG_NEED_TO_OPEN_JACK_CLIENT);

    bool withOwn = true;
    if (args.Length() > 0 && (args[0]->IsBoolean() || args[0]->IsNumber())) {
        withOwn = args[0]->ToBoolean()->BooleanValue();
    }

    Handle<Array> inPortsList = get_ports(withOwn, JackPortIsInput);

    return scope.Close(inPortsList);
}

/**
 * Check port for exists by full port name
 *
 * @public
 * @param {v8::String} checkPortName Full port name to check for exists
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   console.log(jackConnector.portExistsSync('system:playback_1'));
 *     // true
 *   console.log(jackConnector.portExistsSync('nowhere:never'));
 *     // false
 * @returns {v8::Boolean} portExists
 */
Handle<Value> portExistsSync(const Arguments &args)
{
    HandleScope scope;

    String::AsciiValue checkPortName_arg(args[0]->ToString());
    char *checkPortName = *checkPortName_arg;

    return scope.Close(Boolean::New(check_port_exists(checkPortName, 0)));
}

/**
 * Check output port for exists by full port name
 *
 * @public
 * @param {v8::String} checkPortName Full port name to check for exists
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   console.log(jackConnector.outPortExistsSync('system:playback_1'));
 *     // false
 *   console.log(jackConnector.outPortExistsSync('system:capture_1'));
 *     // true
 * @returns {v8::Boolean} outPortExists
 */
Handle<Value> outPortExistsSync(const Arguments &args)
{
    HandleScope scope;

    String::AsciiValue checkPortName_arg(args[0]->ToString());
    char *checkPortName = *checkPortName_arg;

    return scope.Close(Boolean::New(check_port_exists(checkPortName, JackPortIsOutput)));
}

/**
 * Check input port for exists by full port name
 *
 * @public
 * @param {v8::String} checkPortName Full port name to check for exists
 * @example
 *   var jackConnector = require('../build/Release/jack_connector');
 *   console.log(jackConnector.inPortExistsSync('system:playback_1'));
 *     // true
 *   console.log(jackConnector.inPortExistsSync('system:capture_1'));
 *     // false
 * @returns {v8::Boolean} inPortExists
 */
Handle<Value> inPortExistsSync(const Arguments &args)
{
    HandleScope scope;

    String::AsciiValue checkPortName_arg(args[0]->ToString());
    char *checkPortName = *checkPortName_arg;

    return scope.Close(Boolean::New(check_port_exists(checkPortName, JackPortIsInput)));
}

/* System functions */

/**
 * Get JACK-ports
 *
 * @private
 * @param {bool} withOwn Get ports of this client too
 * @param {unsigned long} flags Sum of ports filter
 * @returns {v8::Array} allPortsList Array of full ports names strings
 * @example Handle<Array> portsList = get_ports(true, 0);
 * @example Handle<Array> outPortsList = get_ports(false, JackPortIsOutput);
 */
Handle<Array> get_ports(bool withOwn, unsigned long flags)
{
    unsigned int ports_count = 0;
    const char** jack_ports_list;
    jack_ports_list = jack_get_ports(::client, NULL, NULL, flags);
    while (jack_ports_list[ports_count]) ports_count++;

    unsigned int parsed_ports_count = 0;
    if (withOwn) {
        parsed_ports_count = ports_count;
    } else {
        for (unsigned int i=0; i<ports_count; i++) {
            for (unsigned int n=0; ; n++) {
                if (n>=STR_SIZE-1) {
                    parsed_ports_count++;
                    break;
                }

                if (client_name[n] == '\0' && jack_ports_list[i][n] == ':') {
                    break;
                }

                if (client_name[n] != jack_ports_list[i][n]) {
                    parsed_ports_count++;
                    break;
                }
            }
        }
    }

    Local<Array> allPortsList;
    if (withOwn) {
        allPortsList = Array::New(ports_count);
        for (unsigned int i=0; i<ports_count; i++) {
            allPortsList->Set(i, String::NewSymbol(jack_ports_list[i]));
        }
    } else {
        allPortsList = Array::New(parsed_ports_count);
        for (unsigned int i=0; i<ports_count; i++) {
            for (unsigned int n=0; ; n++) {
                if (n>=STR_SIZE-1) {
                    allPortsList->Set(i, String::NewSymbol(jack_ports_list[i]));
                    break;
                }

                if (client_name[n] == '\0' && jack_ports_list[i][n] == ':') {
                    break;
                }

                if (client_name[n] != jack_ports_list[i][n]) {
                    allPortsList->Set(i, String::NewSymbol(jack_ports_list[i]));
                    break;
                }
            }
        }
    }

    delete jack_ports_list;
    return allPortsList;
}

/**
 * Check for port connection
 *
 * @private
 * @param {const char} src_port_name Source full port name
 * @param {const char} dst_port_name Destination full port name
 * @returns {int} result 0 - not connected, 1 - connected
 * @example int result = check_port_connection("system:capture_1", "system:playback_1");
 */
int check_port_connection(const char *src_port_name, const char *dst_port_name)
{
    jack_port_t *src_port = jack_port_by_name(client, src_port_name);
    const char **existing_connections = jack_port_get_all_connections(client, src_port);
    if (existing_connections) {
        for (int i=0; existing_connections[i]; i++) {
            for (int c=0; ; c++) {
                if (existing_connections[i][c] != dst_port_name[c]) {
                    break;
                }

                if (existing_connections[i][c] == '\0') {
                    delete existing_connections;
                    return 1; // true
                }
            }
        }
    }
    delete existing_connections;
    return 0; // false
}

/**
 * Check port for exists
 *
 * @param {char} port_name Full port name to check for exists
 * @param {unsigned long} flags Filter flags sum
 * @private
 * @returns {bool} port_exists
 * @example bool result = check_port_exists("system:playback_1", 0); // true
 * @example bool result = check_port_exists("system:playback_1", JackPortIsOutput); // false
 */
bool check_port_exists(char *check_port_name, unsigned long flags)
{
    Handle<Array> portsList = get_ports(true, flags);
    for (unsigned int i=0; i<portsList->Length(); i++) {
        String::AsciiValue port_name_arg(portsList->Get(i)->ToString());
        char *port_name = *port_name_arg;

        for (unsigned int n=0; ; n++) {
            if (port_name[n] == '\0' || check_port_name[n] == '\0' || n>=STR_SIZE-1) {
                if (port_name[n] == check_port_name[n]) {
                    return true;
                }
                break;
            }

            if (port_name[n] != check_port_name[n]) {
                break;
            }
        }
    }
    return false;
}

void init(Handle<Object> target)
{

    target->Set( String::NewSymbol("getVersion"),
                 FunctionTemplate::New(getVersion)->GetFunction() );


    target->Set( String::NewSymbol("checkClientOpenedSync"),
                 FunctionTemplate::New(checkClientOpenedSync)->GetFunction() );

    target->Set( String::NewSymbol("openClientSync"),
                 FunctionTemplate::New(openClientSync)->GetFunction() );

    target->Set( String::NewSymbol("closeClientSync"),
                 FunctionTemplate::New(closeClientSync)->GetFunction() );


    target->Set( String::NewSymbol("registerInPortSync"),
                 FunctionTemplate::New(registerInPortSync)->GetFunction() );

    target->Set( String::NewSymbol("registerOutPortSync"),
                 FunctionTemplate::New(registerOutPortSync)->GetFunction() );

    target->Set( String::NewSymbol("unregisterPortSync"),
                 FunctionTemplate::New(unregisterPortSync)->GetFunction() );


    target->Set( String::NewSymbol("checkActiveSync"),
                 FunctionTemplate::New(checkActiveSync)->GetFunction() );

    target->Set( String::NewSymbol("activateSync"),
                 FunctionTemplate::New(activateSync)->GetFunction() );

    target->Set( String::NewSymbol("deactivateSync"),
                 FunctionTemplate::New(deactivateSync)->GetFunction() );


    target->Set( String::NewSymbol("connectPortSync"),
                 FunctionTemplate::New(connectPortSync)->GetFunction() );

    target->Set( String::NewSymbol("disconnectPortSync"),
                 FunctionTemplate::New(disconnectPortSync)->GetFunction() );


    target->Set( String::NewSymbol("getAllPortsSync"),
                 FunctionTemplate::New(getAllPortsSync)->GetFunction() );

    target->Set( String::NewSymbol("getOutPortsSync"),
                 FunctionTemplate::New(getOutPortsSync)->GetFunction() );

    target->Set( String::NewSymbol("getInPortsSync"),
                 FunctionTemplate::New(getInPortsSync)->GetFunction() );


    target->Set( String::NewSymbol("portExistsSync"),
                 FunctionTemplate::New(portExistsSync)->GetFunction() );

    target->Set( String::NewSymbol("outPortExistsSync"),
                 FunctionTemplate::New(outPortExistsSync)->GetFunction() );

    target->Set( String::NewSymbol("inPortExistsSync"),
                 FunctionTemplate::New(inPortExistsSync)->GetFunction() );

}

NODE_MODULE(jack_connector, init);

// vim:set ts=4 sw=4 expandtab: 
