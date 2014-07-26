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

#define VERSION "0.1.2"

#include <node.h>
#include <jack/jack.h>
#include <errno.h>
#include <uv.h>

#define ERR_MSG_NEED_TO_OPEN_JACK_CLIENT "JACK-client is not opened, need to open JACK-client"
#define THROW_ERR(Message) \
        { \
            ThrowException(Exception::Error(String::New(Message))); \
            return scope.Close(Undefined()); \
        }
#define STR_SIZE 256
#define MAX_PORTS 64
#define NEED_JACK_CLIENT_OPENED() \
        { \
        if (client == 0 && !closing) \
            THROW_ERR(ERR_MSG_NEED_TO_OPEN_JACK_CLIENT); \
        }

using namespace v8;

jack_client_t *client = 0;
short client_active = 0;
char client_name[STR_SIZE];

char **own_in_ports;
char **own_in_ports_short_names;
uint8_t own_in_ports_size = 0;
char **own_out_ports;
char **own_out_ports_short_names;
uint8_t own_out_ports_size = 0;

jack_port_t *capture_ports[MAX_PORTS];
jack_port_t *playback_ports[MAX_PORTS];
jack_default_audio_sample_t *capture_buf[MAX_PORTS];
jack_default_audio_sample_t *playback_buf[MAX_PORTS];

Handle<Array> get_ports(bool withOwn, unsigned long flags);
int check_port_connection(const char *src_port_name, const char *dst_port_name);
bool check_port_exists(char *check_port_name, unsigned long flags);
void get_own_ports();
void reset_own_ports_list();
int jack_process(jack_nframes_t nframes, void *arg);

Persistent<Function> processCallback;
Persistent<Function> closeCallback;
bool hasProcessCallback = false; // TODO unbind process callback and check for memory leak
bool hasCloseCallback = false;
bool process = false;
bool closing = false;
uv_work_t *baton;
uv_work_t *close_baton;
static uv_sem_t semaphore;

Handle<Value> deactivateSync(const Arguments &args);
void uv_work_plug(uv_work_t* task) {}

/**
 * Get version of this module
 *
 * @public
 * @returns {v8::String} version
 * @example
 *   var jackConnector = require('jack-connector');
 *   console.log(jackConnector.getVersion());
 *     // string of version, see VERSION macros
 */
Handle<Value> getVersion(const Arguments &args) // {{{1
{
    HandleScope scope;
    return scope.Close(String::New(VERSION));
} // getVersion() }}}1

/**
 * Check JACK-client for opened status
 *
 * @public
 * @returns {v8::Boolean} result True - JACK-client is opened, false - JACK-client is closed
 * @example
 *   var jackConnector = require('jack-connector');
 *   console.log(jackConnector.checkClientOpenedSync());
 *     // true if client opened or false if closed
 */
Handle<Value> checkClientOpenedSync(const Arguments &args) // {{{1
{
    HandleScope scope;
    return scope.Close(Boolean::New(client != 0 && !closing));
} // checkClientOpenedSync() }}}1

/**
 * Open JACK-client
 *
 * @public
 * @param {v8::String} client_name JACK-client name
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 */
Handle<Value> openClientSync(const Arguments &args) // {{{1
{
    HandleScope scope;

    if (client != 0 || closing)
        THROW_ERR("You need close old JACK-client before open new");

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
        ::client_name[0] = '\0';
        THROW_ERR("Couldn't create JACK-client");
    }

    jack_set_process_callback(client, jack_process, 0);
    process = true;

    return scope.Close(Undefined());
} // openClientSync() }}}1

void uv_close_task(uv_work_t* task, int status) // {{{1
{
    HandleScope scope;

    if (baton) {
        scope.Close(Undefined());
        delete task;
        close_baton = NULL;
        close_baton = new uv_work_t();
        uv_queue_work(uv_default_loop(), close_baton, uv_work_plug, uv_close_task);
        return;
    }

    // deactivate first if client activated
    if (client_active) {
        if (jack_deactivate(client) != 0)
            ThrowException(Exception::Error(String::New(
                "Couldn't deactivate JACK-client")));

        client_active = 0;
    }

    if (jack_client_close(client) != 0)
        ThrowException(Exception::Error(String::New(
            "Couldn't close JACK-client")));

    client = 0;

    if (hasCloseCallback) {
        closeCallback->Call(Context::GetCurrent()->Global(), 0, NULL);
        //closeCallback - TODO set to undefined
        hasCloseCallback = false;
    }

    if (hasProcessCallback) {
        //processCallback - TODO set to undefined
        hasProcessCallback = false;
    }

    // TODO cleanup stuff

    closing = false;

    scope.Close(Undefined());
    delete task;
    close_baton = NULL;
} // uv_close_task() }}}1

/**
 * Close JACK-client
 *
 * @public
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   jackConnector.closeClient(function () {
 *     console.log('client closed');
 *   });
 * @async
 * @TODO free jack ports
 */
Handle<Value> closeClient(const Arguments &args) // {{{1
{
    HandleScope scope;

    if (closing) {
        THROW_ERR("Already started closing JACK-client");
    } else {
        closing = true;
    }

    if (client == 0) THROW_ERR("JACK-client already closed");

    process = false;

    if (args[0]->IsFunction()) {
        Local<Function> callback = Local<Function>::Cast( args[0] );
        closeCallback = Persistent<Function>::New( callback );
        hasCloseCallback = true;
    }

    close_baton = new uv_work_t();
    uv_queue_work(uv_default_loop(), close_baton, uv_work_plug, uv_close_task);

    return scope.Close(Undefined());
} // closeClient() }}}1

/**
 * Register new port for this client
 *
 * @public
 * @param {v8::String} port_name Full port name
 * @param {v8::Integer} port_type See: enum jack_flags
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   jackConnector.registerInPortSync('in_1');
 *   jackConnector.registerInPortSync('in_2');
 */
Handle<Value> registerInPortSync(const Arguments &args) // {{{1
{
    HandleScope scope;
    NEED_JACK_CLIENT_OPENED();

    String::AsciiValue port_name(args[0]->ToString());

    capture_ports[own_in_ports_size] = jack_port_register(
        client,
        *port_name,
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsInput,
        0
    );

    reset_own_ports_list();

    return scope.Close(Undefined());
} // registerInPortSync() }}}1

/**
 * Register new port for this client
 *
 * @public
 * @param {v8::String} port_name Full port name
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   jackConnector.registerOutPortSync('out_1');
 *   jackConnector.registerOutPortSync('out_2');
 */
Handle<Value> registerOutPortSync(const Arguments &args) // {{{1
{
    HandleScope scope;
    NEED_JACK_CLIENT_OPENED();

    String::AsciiValue port_name(args[0]->ToString());

    playback_ports[own_out_ports_size] = jack_port_register(
        client,
        *port_name,
        JACK_DEFAULT_AUDIO_TYPE,
        JackPortIsOutput,
        0
    );

    reset_own_ports_list();

    return scope.Close(Undefined());
} // registerOutPortSync() }}}1

/**
 * Unregister port for this client
 *
 * @public
 * @param {v8::String} port_name Full port name
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   jackConnector.registerOutPortSync('out_1');
 *   jackConnector.registerOutPortSync('out_2');
 *   jackConnector.unregisterPortSync('out_1');
 *   jackConnector.unregisterPortSync('out_2');
 * @TODO deactivating (for stop processing before update ports list)
 * @TODO remove port from ports list
 */
Handle<Value> unregisterPortSync(const Arguments &args) // {{{1
{
    HandleScope scope;
    NEED_JACK_CLIENT_OPENED();

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

    if (jack_port_unregister(client, port) != 0)
        THROW_ERR("Couldn't unregister JACK-port");

    // .....

    reset_own_ports_list();

    return scope.Close(Undefined());
} // unregisterPortSync() }}}1

/**
 * Check JACK-client for active
 *
 * @public
 * @example
 *   var jackConnector = require('jack-connector');
 *   if (jackConnector.checkActiveSync())
 *     console.log('JACK-client is active');
 *   else
 *     console.log('JACK-client is not active');
 * @returns {v8::Boolean} result True - client is active, false - client is not active
 */
Handle<Value> checkActiveSync(const Arguments &args) // {{{1
{
    HandleScope scope;
    NEED_JACK_CLIENT_OPENED();

    if (::client_active > 0) {
        return scope.Close(Boolean::New(true));
    } else {
        return scope.Close(Boolean::New(false));
    }
} // checkActiveSync() }}}1

/**
 * Activate JACK-client
 *
 * @public
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   jackConnector.activateSync();
 */
Handle<Value> activateSync(const Arguments &args) // {{{1
{
    HandleScope scope;
    NEED_JACK_CLIENT_OPENED();

    if (client_active) THROW_ERR("JACK-client already activated");

    if (jack_activate(client) != 0) THROW_ERR("Couldn't activate JACK-client");

    client_active = 1;

    return scope.Close(Undefined());
} // activateSync() }}}1

/**
 * Deactivate JACK-client
 *
 * @public
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   jackConnector.activateSync();
 *   jackConnector.deactivateSync();
 */
Handle<Value> deactivateSync(const Arguments &args) // {{{1
{
    HandleScope scope;
    NEED_JACK_CLIENT_OPENED();

    if (! client_active) THROW_ERR("JACK-client is not active");

    if (jack_deactivate(client) != 0) THROW_ERR("Couldn't deactivate JACK-client");

    client_active = 0;

    return scope.Close(Undefined());
} // deactivateSync() }}}1

/**
 * Connect port to port
 *
 * @public
 * @param {v8::String} sourcePort Full name of source port
 * @param {v8::String} destinationPort Full name of destination port
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   jackConnector.activateSync();
 *   jackConnector.connectPortSync('system:capture_1', 'system:playback_1');
 */
Handle<Value> connectPortSync(const Arguments &args) // {{{1
{
    HandleScope scope;
    NEED_JACK_CLIENT_OPENED();

    if (! client_active) THROW_ERR("JACK-client is not active");

    String::AsciiValue src_port_name(args[0]->ToString());
    jack_port_t *src_port = jack_port_by_name(client, *src_port_name);
    if (! src_port) THROW_ERR("Non existing source port");

    String::AsciiValue dst_port_name(args[1]->ToString());
    jack_port_t *dst_port = jack_port_by_name(client, *dst_port_name);
    if (! dst_port) THROW_ERR("Non existing destination port");

    if (! client_active
    && (jack_port_is_mine(client, src_port) || jack_port_is_mine(client, dst_port))) {
        THROW_ERR("Jack client must be activated to connect own ports");
    }

    int error = jack_connect(client, *src_port_name, *dst_port_name);
    if (error != 0 && error != EEXIST) THROW_ERR("Failed to connect ports");

    return scope.Close(Undefined());
} // connectPortSync() }}}1

/**
 * Disconnect ports
 *
 * @public
 * @param {v8::String} sourcePort Full name of source port
 * @param {v8::String} destinationPort Full name of destination port
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   jackConnector.activateSync();
 *   jackConnector.disconnectPortSync('system:capture_1', 'system:playback_1');
 */
Handle<Value> disconnectPortSync(const Arguments &args) // {{{1
{
    HandleScope scope;
    NEED_JACK_CLIENT_OPENED();

    if (! client_active) THROW_ERR("JACK-client is not active");

    String::AsciiValue src_port_name(args[0]->ToString());
    jack_port_t *src_port = jack_port_by_name(client, *src_port_name);
    if (! src_port) THROW_ERR("Non existing source port");

    String::AsciiValue dst_port_name(args[1]->ToString());
    jack_port_t *dst_port = jack_port_by_name(client, *dst_port_name);
    if (! dst_port) THROW_ERR("Non existing destination port");

    if (check_port_connection(*src_port_name, *dst_port_name)) {
        if (jack_disconnect(client, *src_port_name, *dst_port_name))
            THROW_ERR("Failed to disconnect ports");
    }

    return scope.Close(Undefined());
} // disconnectPortSync() }}}1

/**
 * Get all JACK-ports list
 *
 * @public
 * @param {v8::Boolean} [withOwn] Default: true
 * @returns {v8::Array} allPortsList Array of full ports names strings
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   console.log(jackConnector.getAllPortsSync());
 *     // prints: [ "system:playback_1", "system:playback_2",
 *     //           "system:capture_1", "system:capture_2" ]
 */
Handle<Value> getAllPortsSync(const Arguments &args) // {{{1
{
    HandleScope scope;
    NEED_JACK_CLIENT_OPENED();

    bool withOwn = true;
    if (args.Length() > 0 && (args[0]->IsBoolean() || args[0]->IsNumber())) {
        withOwn = args[0]->ToBoolean()->BooleanValue();
    }

    Handle<Array> allPortsList = get_ports(withOwn, 0);

    return scope.Close(allPortsList);
} // getAllPortsSync() }}}1

/**
 * Get output JACK-ports list
 *
 * @public
 * @param {v8::Boolean} [withOwn] Default: true
 * @returns {v8::Array} outPortsList Array of full ports names strings
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   console.log(jackConnector.getOutPortsSync());
 *     // prints: [ "system:capture_1", "system:capture_2" ]
 */
Handle<Value> getOutPortsSync(const Arguments &args) // {{{1
{
    HandleScope scope;
    NEED_JACK_CLIENT_OPENED();

    bool withOwn = true;
    if (args.Length() > 0 && (args[0]->IsBoolean() || args[0]->IsNumber())) {
        withOwn = args[0]->ToBoolean()->BooleanValue();
    }

    Handle<Array> outPortsList = get_ports(withOwn, JackPortIsOutput);

    return scope.Close(outPortsList);
} // getOutPortsSync() }}}1

/**
 * Get input JACK-ports list
 *
 * @public
 * @param {v8::Boolean} [withOwn] Default: true
 * @returns {v8::Array} inPortsList Array of full ports names strings
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   console.log(jackConnector.getInPortsSync());
 *     // prints: [ "system:playback_1", "system:playback_2" ]
 */
Handle<Value> getInPortsSync(const Arguments &args) // {{{1
{
    HandleScope scope;
    NEED_JACK_CLIENT_OPENED();

    bool withOwn = true;
    if (args.Length() > 0 && (args[0]->IsBoolean() || args[0]->IsNumber())) {
        withOwn = args[0]->ToBoolean()->BooleanValue();
    }

    Handle<Array> inPortsList = get_ports(withOwn, JackPortIsInput);

    return scope.Close(inPortsList);
} // getInPortsSync() }}}1

/**
 * Check port for exists by full port name
 *
 * @public
 * @param {v8::String} checkPortName Full port name to check for exists
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   console.log(jackConnector.portExistsSync('system:playback_1'));
 *     // true
 *   console.log(jackConnector.portExistsSync('nowhere:never'));
 *     // false
 * @returns {v8::Boolean} portExists
 * @TODO check for jack client
 */
Handle<Value> portExistsSync(const Arguments &args) // {{{1
{
    HandleScope scope;

    String::AsciiValue checkPortName_arg(args[0]->ToString());
    char *checkPortName = *checkPortName_arg;

    return scope.Close(Boolean::New(check_port_exists(checkPortName, 0)));
} // portExistsSync() }}}1

/**
 * Check output port for exists by full port name
 *
 * @public
 * @param {v8::String} checkPortName Full port name to check for exists
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   console.log(jackConnector.outPortExistsSync('system:playback_1'));
 *     // false
 *   console.log(jackConnector.outPortExistsSync('system:capture_1'));
 *     // true
 * @returns {v8::Boolean} outPortExists
 * @TODO check for jack client
 */
Handle<Value> outPortExistsSync(const Arguments &args) // {{{1
{
    HandleScope scope;

    String::AsciiValue checkPortName_arg(args[0]->ToString());
    char *checkPortName = *checkPortName_arg;

    return scope.Close(Boolean::New(check_port_exists(checkPortName, JackPortIsOutput)));
} // outPortExistsSync() }}}1

/**
 * Check input port for exists by full port name
 *
 * @public
 * @param {v8::String} checkPortName Full port name to check for exists
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   console.log(jackConnector.inPortExistsSync('system:playback_1'));
 *     // true
 *   console.log(jackConnector.inPortExistsSync('system:capture_1'));
 *     // false
 * @returns {v8::Boolean} inPortExists
 * @TODO check for jack client
 */
Handle<Value> inPortExistsSync(const Arguments &args) // {{{1
{
    HandleScope scope;

    String::AsciiValue checkPortName_arg(args[0]->ToString());
    char *checkPortName = *checkPortName_arg;

    return scope.Close(Boolean::New(check_port_exists(checkPortName, JackPortIsInput)));
} // inPortExistsSync() }}}1

/**
 * Bind callback for JACK process
 *
 * @public
 * @param {v8::Function} callback
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('JACK_connector_client_name');
 *   jackConnector.registerOutPortSync('output');
 *   function process(nframes, playback, capture) {
 *     for (var i=0; i<nframes; i++) playback['output'].write(i, 0);
 *   }
 *   jackConnector.bindProcessSync(process);
 *   jackConnector.activateSync();
 * @returns {v8::Undefined}
 */
Handle<Value> bindProcessSync(const Arguments &args) // {{{1
{
    HandleScope scope;
    NEED_JACK_CLIENT_OPENED();

    if ( ! args[0]->IsFunction()) {
        ThrowException(Exception::TypeError(String::New("Callback argument must be a function")));
        return scope.Close(Undefined());
    }

    Local<Function> callback = Local<Function>::Cast( args[0] );
    processCallback = Persistent<Function>::New( callback );
    hasProcessCallback = true;

    return scope.Close(Undefined());
} // bindProcessSync() }}}1


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
Handle<Array> get_ports(bool withOwn, unsigned long flags) // {{{1
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
} // get_ports() }}}1

typedef struct get_own_ports_retval_t {
    char** names;
    char** own_names; // without client name
    uint8_t count;
};

char* get_port_name_without_client_name(char* port_name) // {{{1
{
    char* retval = new char[STR_SIZE];
    uint16_t i=0, n=0;
    for (i=0; i<STR_SIZE; i++) {
        if (port_name[i] == ':') {
            n = i+1; break;
        }
    }
    for (i=0; n<STR_SIZE; i++, n++) {
        retval[i] = port_name[n];
        if (retval[i] == '\0') break;
    }
    return retval;
} // get_port_name_without_client_name() }}}1

get_own_ports_retval_t get_own_ports(unsigned long flags) // {{{1
{
    const char** jack_ports_list;

    char** ports_names;
    char** ports_own_names;
    char** ports_namesTmp = new char*[MAX_PORTS];

    jack_ports_list = jack_get_ports(::client, NULL, NULL, flags);

    uint16_t i=0, n=0, m=0;

    while (jack_ports_list[i]) {
        if (i >= MAX_PORTS) break;
        uint8_t found = 1;
        for (n=0; ; n++) {
            if (n>=STR_SIZE-1) { found = 0; break; }
            if (client_name[n] == '\0' && jack_ports_list[i][n] == ':') { break; }
            if (client_name[n] != jack_ports_list[i][n]) { found = 0; break; }
        }
        if (found == 1) {
            ports_namesTmp[m] = new char[STR_SIZE];
            for (n=0; n<STR_SIZE; n++) {
                ports_namesTmp[m][n] = jack_ports_list[i][n];
                if (jack_ports_list[i][n] == '\0') break;
            }
            m++;
        }
        i++;
    }
    delete [] jack_ports_list;

    ports_names = new char*[m];
    ports_own_names = new char*[m];
    for (i=0; i<m; i++) {
        ports_names[i] = new char[STR_SIZE];
        for (n=0; n<STR_SIZE; n++) {
            ports_names[i][n] = ports_namesTmp[i][n];
            if (ports_namesTmp[i][n] == '\0') break;
        }
        delete [] ports_namesTmp[i];
        ports_own_names[i] = get_port_name_without_client_name(ports_names[i]);
    }
    delete [] ports_namesTmp;

    get_own_ports_retval_t retval;
    retval.names = ports_names;
    retval.own_names = ports_own_names;
    retval.count = m;

    return retval;
} // get_own_ports() }}}1

void reset_own_ports_list() // {{{1
{
    get_own_ports_retval_t retval;
    uint8_t i=0;

    // in {{{2
    retval = get_own_ports(JackPortIsInput);
    for (i=0; i<own_in_ports_size; i++) {
        delete [] own_in_ports[i];
        delete [] own_in_ports_short_names[i];
    }
    delete [] own_in_ports;
    delete [] own_in_ports_short_names;
    own_in_ports = retval.names;
    own_in_ports_short_names = retval.own_names;
    own_in_ports_size = retval.count;
    // in }}}2

    // out {{{2
    retval = get_own_ports(JackPortIsOutput);
    for (i=0; i<own_out_ports_size; i++) {
        delete [] own_out_ports[i];
        delete [] own_out_ports_short_names[i];
    }
    delete [] own_out_ports;
    delete [] own_out_ports_short_names;
    own_out_ports = retval.names;
    own_out_ports_short_names = retval.own_names;
    own_out_ports_size = retval.count;
    // out }}}2
} // reset_own_ports_list() }}}1

/**
 * Check for port connection
 *
 * @private
 * @param {const char} src_port_name Source full port name
 * @param {const char} dst_port_name Destination full port name
 * @returns {int} result 0 - not connected, 1 - connected
 * @example int result = check_port_connection("system:capture_1", "system:playback_1");
 */
int check_port_connection(const char *src_port_name, const char *dst_port_name) // {{{1
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
} // check_port_connection() }}}1

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
bool check_port_exists(char *check_port_name, unsigned long flags) // {{{1
{
    Handle<Array> portsList = get_ports(true, flags);
    for (uint8_t i=0; i<portsList->Length(); i++) {
        String::AsciiValue port_name_arg(portsList->Get(i)->ToString());
        char *port_name = *port_name_arg;

        for (uint16_t n=0; ; n++) {
            if (port_name[n] == '\0' || check_port_name[n] == '\0' || n>=STR_SIZE-1) {
                if (port_name[n] == check_port_name[n]) {
                    return true;
                } else {
                    break;
                }
            } else if (port_name[n] != check_port_name[n]) {
                break;
            }
        }
    }
    return false;
} // check_port_exists() }}}1

/**
 * Get own output port index
 *
 * @param {char} short_port_name - Own port name without client name
 * @private
 * @returns {int16_t} port_index - Port index or -1 if not found
 */
int16_t get_own_out_port_index(char* short_port_name) // {{{1
{
    for (uint8_t n=0; n<own_out_ports_size; n++) {
        for (uint16_t m=0; m<STR_SIZE; m++) {
            if (
                short_port_name[m] == '\0' ||
                own_out_ports_short_names[n][m] == '\0'
            ) {
                if (short_port_name[m] == own_out_ports_short_names[n][m]) {
                    return n; // index of port
                } else {
                    break; // go to next port
                }
            } else if (short_port_name[m] != own_out_ports_short_names[n][m]) {
                break; // go to next port
            }
        } // for (char of port name)
    } // for (ports)

    return -1; // port not found
} // check_own_out_port_exists() }}}1


// processing {{{1

void uv_process(uv_work_t* task, int status) // {{{2
{
    HandleScope scope;

    uint16_t nframes = *((uint16_t*)(&task->data));

    Local<Object> capture = Object::New();
    for (uint8_t i=0; i<own_in_ports_size; i++) {
        Local<Array> portBuf = Array::New(nframes);
        for (uint16_t n=0; n<nframes; n++) {
            Local<Number> sample = Number::New( capture_buf[i][n] );
            portBuf->Set(n, sample);
        }
        capture->Set(
            String::NewSymbol(own_in_ports_short_names[i]),
            portBuf
        );
    }

    // prepare args to callback
    const unsigned argc = 2;
    Local<Value> argv[argc] = {
        Local<Value>::New( Number::New( nframes ) ),
        Local<Value>::New( capture )
    };

    Local<Value> retval =
        processCallback->Call(Context::GetCurrent()->Global(), argc, argv);

    if (!retval->IsNull() && !retval->IsUndefined() && !retval->IsObject()) {
        ThrowException(Exception::TypeError(String::New(
            "Returned value of \"process\" callback must be an object"
            " of port{String}:buffer{Array.<Number|Float>} values"
            " or null or undefined")));
        return;
    }

    if (retval->IsObject()) {
        Local<Object> obj = retval.As<Object>();
        Local<Array> keys = obj->GetOwnPropertyNames();
        for (uint16_t i=0; i<keys->Length(); i++) {
            Local<Value> key = keys->Get(i);
            if (!key->IsString()) {
                ThrowException(Exception::TypeError(String::New(
                    "Incorrect key type in returned value of \"process\""
                    " callback, must be a string (own port name)")));
                return;
            }
            String::AsciiValue port_name(key->ToString());

            int16_t port_index = get_own_out_port_index(*port_name);
            if (port_index == -1) {
                char err[] = "Port \"%s\" not found";
                char err_msg[STR_SIZE + sizeof(err)];
                sprintf(err_msg, err, *port_name);
                ThrowException(Exception::Error(String::New(err_msg)));
                return;
            }

            Local<Value> val = obj->Get(key);
            if (!val->IsArray()) {
                ThrowException(Exception::Error(String::New(
                    "Incorrect buffer type of returned value of \"process\""
                    " callback, must be an Array<Float|Number>")));
                return;
            }
            Local<Array> buffer = val.As<Array>();

            if (buffer->Length() != nframes) {
                ThrowException(Exception::Error(String::New(
                    "Incorrect buffer size of returned value"
                    " of \"process\" callback")));
                return;
            }

            for (uint16_t sample_i=0; sample_i<nframes; sample_i++) {
                Local<Value> sample = buffer->Get(sample_i);
                if (!sample->IsNumber()) {
                    ThrowException(Exception::Error(String::New(
                        "Incorrect sample type of returned value"
                        " of \"process\" callback"
                        ", must be a {Number|Float}")));
                    return;
                }
                playback_buf[port_index][sample_i] = sample->ToNumber()->Value();
            }
        } // for (ports)
    } // if we has something to output from callback

    scope.Close(Undefined());
    delete task;
    baton = NULL;
    uv_sem_post(&semaphore);
} // uv_process() }}}2

int jack_process(jack_nframes_t nframes, void *arg) // {{{2
{
    if (!process) return 0;
    if (!hasProcessCallback) return 0;

    if (baton) {
        uv_sem_wait(&semaphore);
        uv_sem_destroy(&semaphore);
    }

    baton = new uv_work_t();

    if (uv_sem_init(&semaphore, 0) < 0) { perror("uv_sem_init"); return 1; }

    for (uint8_t i=0; i<own_in_ports_size; i++) {
        capture_buf[i] = (jack_default_audio_sample_t *)
            jack_port_get_buffer(capture_ports[i], nframes);
    }

    for (uint8_t i=0; i<own_out_ports_size; i++) {
        playback_buf[i] = (jack_default_audio_sample_t *)
            jack_port_get_buffer(playback_ports[i], nframes);
    }

    baton->data = (void*)(uint16_t)nframes;
    uv_queue_work(uv_default_loop(), baton, uv_work_plug, uv_process);
    uv_sem_wait(&semaphore);
    uv_sem_destroy(&semaphore);

    return 0;
} // jack_process() }}}2

// processing }}}1

void init(Handle<Object> target) // {{{1
{

    target->Set( String::NewSymbol("getVersion"),
                 FunctionTemplate::New(getVersion)->GetFunction() );

    // client init

    target->Set( String::NewSymbol("checkClientOpenedSync"),
                 FunctionTemplate::New(checkClientOpenedSync)->GetFunction() );

    target->Set( String::NewSymbol("openClientSync"),
                 FunctionTemplate::New(openClientSync)->GetFunction() );

    target->Set( String::NewSymbol("closeClient"),
                 FunctionTemplate::New(closeClient)->GetFunction() );

    // registering ports

    target->Set( String::NewSymbol("registerInPortSync"),
                 FunctionTemplate::New(registerInPortSync)->GetFunction() );

    target->Set( String::NewSymbol("registerOutPortSync"),
                 FunctionTemplate::New(registerOutPortSync)->GetFunction() );

    target->Set( String::NewSymbol("unregisterPortSync"),
                 FunctionTemplate::New(unregisterPortSync)->GetFunction() );

    // port connections

    target->Set( String::NewSymbol("connectPortSync"),
                 FunctionTemplate::New(connectPortSync)->GetFunction() );

    target->Set( String::NewSymbol("disconnectPortSync"),
                 FunctionTemplate::New(disconnectPortSync)->GetFunction() );

    // get ports

    target->Set( String::NewSymbol("getAllPortsSync"),
                 FunctionTemplate::New(getAllPortsSync)->GetFunction() );

    target->Set( String::NewSymbol("getOutPortsSync"),
                 FunctionTemplate::New(getOutPortsSync)->GetFunction() );

    target->Set( String::NewSymbol("getInPortsSync"),
                 FunctionTemplate::New(getInPortsSync)->GetFunction() );

    // port exists

    target->Set( String::NewSymbol("portExistsSync"),
                 FunctionTemplate::New(portExistsSync)->GetFunction() );

    target->Set( String::NewSymbol("outPortExistsSync"),
                 FunctionTemplate::New(outPortExistsSync)->GetFunction() );

    target->Set( String::NewSymbol("inPortExistsSync"),
                 FunctionTemplate::New(inPortExistsSync)->GetFunction() );

    // sound process

    target->Set( String::NewSymbol("bindProcessSync"),
                 FunctionTemplate::New(bindProcessSync)->GetFunction() );

    // activating client

    target->Set( String::NewSymbol("checkActiveSync"),
                 FunctionTemplate::New(checkActiveSync)->GetFunction() );

    target->Set( String::NewSymbol("activateSync"),
                 FunctionTemplate::New(activateSync)->GetFunction() );

    target->Set( String::NewSymbol("deactivateSync"),
                 FunctionTemplate::New(deactivateSync)->GetFunction() );

} // init() }}}1

NODE_MODULE(jack_connector, init);

// vim:set ts=4 sts=4 sw=4 et:
