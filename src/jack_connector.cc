/**
 * JACK Connector
 * Bindings JACK-Audio-Connection-Kit for Node.JS
 *
 * @author Viacheslav Lotsmanov (unclechu) <lotsmanov89@gmail.com>
 * @license MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2014 Viacheslav Lotsmanov
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

#define VERSION "0.1.4"

#include <node_api.h>
#include <jack/jack.h>
#include <errno.h>
#include <uv.h>

#define ERR_MSG_NEED_TO_OPEN_JACK_CLIENT "JACK-client is not opened, need to open JACK-client"
#define THROW_ERR(Message)                \
  {                                       \
    napi_throw_error(env, NULL, Message); \
    napi_value undefined;                 \
    napi_get_undefined(env, &undefined);  \
    return undefined;                     \
  }
#define STR_SIZE 256
#define MAX_PORTS 64
#define NEED_JACK_CLIENT_OPENED()                  \
  {                                                \
    if (client == 0 && !closing)                   \
      THROW_ERR(ERR_MSG_NEED_TO_OPEN_JACK_CLIENT); \
  }

using namespace Napi;

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

napi_value getVersion(napi_env env, napi_callback_info info) // {{{1
{
  napi_status status;
  napi_value result;
  status = napi_create_string_utf8(env, VERSION, NAPI_AUTO_LENGTH, &result);
  return result;
} // getVersion() }}}1

napi_value checkClientOpenedSync(napi_env env, napi_callback_info info) // {{{1
{
  napi_status status;
  napi_value result;
  status = napi_get_boolean(env, client != 0 && !closing, &result);
  return result;
} // checkClientOpenedSync() }}}1

napi_value openClientSync(napi_env env, napi_callback_info info) // {{{1
{
  napi_status status;

  if (client != 0 || closing)
    THROW_ERR("You need to close the old JACK-client before opening a new one");

  napi_value argv[1];
  size_t argc = 1;
  status = napi_get_cb_info(env, info, &argc, argv, NULL, NULL);

  napi_value n_api_client_name = argv[0];
  size_t client_name_len;
  status = napi_get_value_string_utf8(env, n_api_client_name, NULL, 0, &client_name_len);
  char client_name[client_name_len + 1];
  status = napi_get_value_string_utf8(env, n_api_client_name, client_name, client_name_len + 1, NULL);
  client_name[client_name_len] = '\0';

  for (unsigned int i = 0;; i++)
  {
    if (client_name[i] == '\0' || i >= STR_SIZE - 1)
    {
      if (i == 0)
      {
        client_name[0] = '\0';
        THROW_ERR("Empty JACK-client name");
      }
      client_name[i] = '\0';
      break;
    }

    ::client_name[i] = client_name[i];
  }

  client = jack_client_open(client_name, JackNullOption, 0);
  if (client == 0)
  {
    client_name[0] = '\0';
    ::client_name[0] = '\0';
    THROW_ERR("Couldn't create JACK-client");
  }

  jack_set_process_callback(client, jack_process, 0);
  process = true;

  napi_value undefined;
  status = napi_get_undefined(env, &undefined);
  return undefined;
} // openClientSync() }}}1

#define UV_CLOSE_TASK_CLEANUP()                   \
  {                                               \
    status = napi_get_undefined(env, &undefined); \
    delete task;                                  \
    close_baton = NULL;                           \
  }

void uv_close_task(napi_env env, uv_work_t *task, int status)
{
  napi_handle_scope scope;
  napi_open_handle_scope(env, &scope);

  if (baton)
  {
    UV_CLOSE_TASK_CLEANUP();
    close_baton = new uv_work_t();
    uv_queue_work(uv_default_loop(), close_baton, uv_work_plug, uv_close_task);
    return;
  }

  if (client_active)
  {
    if (jack_deactivate(client) != 0)
      UV_CLOSE_TASK_EXCEPTION("Couldn't deactivate JACK-client");

    client_active = 0;
  }

  if (jack_client_close(client) != 0)
    UV_CLOSE_TASK_EXCEPTION("Couldn't close JACK-client");

  client = 0;

  closing = false;

  napi_value undefined;
  delete task;
  close_baton = NULL;
}

napi_value closeClient(napi_env env, napi_callback_info info) // {{{1
{
  napi_status status;

  if (closing)
  {
    THROW_ERR("Already started closing JACK-client");
  }
  else
  {
    closing = true;
  }

  if (client == 0)
    THROW_ERR("JACK-client already closed");

  process = false;

  size_t argc = 1;
  napi_value argv[1];
  status = napi_get_cb_info(env, info, &argc, argv, NULL, NULL);

  if (napi_is_function(env, argv[0]))
  {
    closeCallback = Persistent<Function>::New(napi_ref(argv[0]));
    hasCloseCallback = true;
  }

  close_baton = new uv_work_t();
  uv_queue_work(uv_default_loop(), close_baton, uv_work_plug, uv_close_task);

  napi_value undefined;
  status = napi_get_undefined(env, &undefined);
  return undefined;
} // closeClient() }}}1

Napi::Value registerInPortSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  std::string port_name = info[0].As<Napi::String>();

  capture_ports[own_in_ports_size] = jack_port_register(
      client,
      port_name.c_str(),
      JACK_DEFAULT_AUDIO_TYPE,
      JackPortIsInput,
      0);

  reset_own_ports_list();

  return env.Undefined();
}

Napi::Value registerOutPortSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  std::string port_name = info[0].As<Napi::String>();

  playback_ports[own_out_ports_size] = jack_port_register(
      client,
      port_name.c_str(),
      JACK_DEFAULT_AUDIO_TYPE,
      JackPortIsOutput,
      0);

  reset_own_ports_list();

  return env.Undefined();
}

Napi::Value unregisterPortSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  std::string arg_port_name = info[0].As<Napi::String>();
  char full_port_name[STR_SIZE];
  char *port_name = *arg_port_name;

  for (int i = 0, n = 0, m = 0;; i++, m++)
  {
    if (n == 0)
    {
      if (::client_name[m] == '\0')
      {
        full_port_name[i] = ':';
        m = -1;
        n = 1;
      }
      else
      {
        full_port_name[i] = ::client_name[m];
      }
    }
    else
    {
      if (port_name[m] == '\0')
      {
        full_port_name[i] = '\0';
        break;
      }
      else
      {
        full_port_name[i] = port_name[m];
      }
    }
  }

  jack_port_t *port = jack_port_by_name(client, full_port_name);

  if (jack_port_unregister(client, port) != 0)
    THROW_ERR("Couldn't unregister JACK-port");

  reset_own_ports_list();

  return env.Undefined();
}

Napi::Value checkActiveSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  return Napi::Boolean::New(env, client_active > 0);
}

Napi::Value activateSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  if (client_active)
    THROW_ERR(env, "JACK-client already activated");

  if (jack_activate(client) != 0)
    THROW_ERR(env, "Couldn't activate JACK-client");

  client_active = 1;

  return env.Undefined();
}

Napi::Value deactivateSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  if (!client_active)
    THROW_ERR(env, "JACK-client is not active");

  if (jack_deactivate(client) != 0)
    THROW_ERR(env, "Couldn't deactivate JACK-client");

  client_active = 0;

  return env.Undefined();
}

Napi::Value connectPortSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  if (!client_active)
    THROW_ERR("JACK-client is not active");

  String::AsciiValue src_port_name(args[0]->ToString());
  jack_port_t *src_port = jack_port_by_name(client, *src_port_name);
  if (!src_port)
    THROW_ERR("Non existing source port");

  String::AsciiValue dst_port_name(args[1]->ToString());
  jack_port_t *dst_port = jack_port_by_name(client, *dst_port_name);
  if (!dst_port)
    THROW_ERR("Non existing destination port");

  if (!client_active && (jack_port_is_mine(client, src_port) || jack_port_is_mine(client, dst_port)))
  {
    THROW_ERR("Jack client must be activated to connect own ports");
  }

  int error = jack_connect(client, *src_port_name, *dst_port_name);
  if (error != 0 && error != EEXIST)
    THROW_ERR("Failed to connect ports");

  return env.Undefined();
}

Napi::Value disconnectPortSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  if (!client_active)
    THROW_ERR("JACK-client is not active");

  String::AsciiValue src_port_name(args[0]->ToString());
  jack_port_t *src_port = jack_port_by_name(client, *src_port_name);
  if (!src_port)
    THROW_ERR("Non existing source port");

  String::AsciiValue dst_port_name(args[1]->ToString());
  jack_port_t *dst_port = jack_port_by_name(client, *dst_port_name);
  if (!dst_port)
    THROW_ERR("Non existing destination port");

  if (check_port_connection(*src_port_name, *dst_port_name))
  {
    if (jack_disconnect(client, *src_port_name, *dst_port_name))
      THROW_ERR("Failed to disconnect ports");
  }

  return env.Undefined();
}

Napi::Value getAllPortsSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  bool withOwn = true;
  if (info.Length() > 0 && (info[0].IsBoolean() || info[0].IsNumber()))
  {
    withOwn = info[0].ToBoolean();
  }

  Napi::Array allPortsList = get_ports(env, withOwn, 0);

  return allPortsList;
}

Napi::Value getOutPortsSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  bool withOwn = true;
  if (info.Length() > 0 && (info[0].IsBoolean() || info[0].IsNumber()))
  {
    withOwn = info[0].ToBoolean();
  }

  Napi::Array outPortsList = get_ports(env, withOwn, JackPortIsOutput);

  return outPortsList;
}

Napi::Value getInPortsSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  bool withOwn = true;
  if (info.Length() > 0 && (info[0].IsBoolean() || info[0].IsNumber()))
  {
    withOwn = info[0].ToBoolean();
  }

  Napi::Array inPortsList = get_ports(env, withOwn, JackPortIsInput);

  return inPortsList;
}

Napi::Value portExistsSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  std::string checkPortName = info[0].As<Napi::String>();

  return Napi::Boolean::New(env, check_port_exists(checkPortName.c_str(), 0));
}

Napi::Value outPortExistsSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  std::string checkPortName = info[0].As<Napi::String>();

  return Napi::Boolean::New(env, check_port_exists(checkPortName.c_str(), JackPortIsOutput));
}

Napi::Value inPortExistsSync(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  NEED_JACK_CLIENT_OPENED();

  std::string checkPortName = info[0].As<Napi::String>();

  return Napi::Boolean::New(env, check_port_exists(checkPortName.c_str(), JackPortIsInput));
}

napi_value bindProcessSync(napi_env env, napi_callback_info info)
{
  NEED_JACK_CLIENT_OPENED();

  size_t argc = 1;
  napi_value args[1];
  napi_value jsthis;

  napi_get_cb_info(env, info, &argc, args, &jsthis, NULL); // Get the argument values

  bool isFunction;
  napi_is_function(env, args[0], &isFunction);

  if (!isFunction)
  {
    napi_throw_type_error(env, NULL, "Callback argument must be a function");
    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
  }

  processCallback = Napi::Persistent(args[0], env);
  hasProcessCallback = true;

  napi_value undefined;
  napi_get_undefined(env, &undefined);
  return undefined;
}

napi_value get_ports(napi_env env, bool withOwn, unsigned long flags)
{
  unsigned int ports_count = 0;
  const char **jack_ports_list;
  jack_ports_list = jack_get_ports(::client, NULL, NULL, flags);
  while (jack_ports_list[ports_count])
    ports_count++;

  unsigned int parsed_ports_count = 0;
  if (withOwn)
  {
    parsed_ports_count = ports_count;
  }
  else
  {
    for (unsigned int i = 0; i < ports_count; i++)
    {
      for (unsigned int n = 0;; n++)
      {
        if (n >= STR_SIZE - 1)
        {
          parsed_ports_count++;
          break;
        }

        if (client_name[n] == '\0' && jack_ports_list[i][n] == ':')
        {
          break;
        }

        if (client_name[n] != jack_ports_list[i][n])
        {
          parsed_ports_count++;
          break;
        }
      }
    }
  }

  Napi::Array allPortsList;
  if (withOwn)
  {
    allPortsList = Napi::Array::New(env, ports_count);
    for (unsigned int i = 0; i < ports_count; i++)
    {
      allPortsList.Set(i, Napi::String::New(env, jack_ports_list[i]));
    }
  }
  else
  {
    allPortsList = Napi::Array::New(env, parsed_ports_count);
    for (unsigned int i = 0; i < ports_count; i++)
    {
      for (unsigned int n = 0;; n++)
      {
        if (n >= STR_SIZE - 1)
        {
          allPortsList.Set(i, Napi::String::New(env, jack_ports_list[i]));
          break;
        }

        if (client_name[n] == '\0' && jack_ports_list[i][n] == ':')
        {
          break;
        }

        if (client_name[n] != jack_ports_list[i][n])
        {
          allPortsList.Set(i, Napi::String::New(env, jack_ports_list[i]));
          break;
        }
      }
    }
  }

  delete jack_ports_list;
  return allPortsList;
}

struct GetOwnPortsRetVal
{
  char **names;
  char **own_names; // without client name
  uint8_t count;
};

char *GetPortNameWithoutClientName(char *port_name)
{
  char *retval = new char[STR_SIZE];
  uint16_t i = 0, n = 0;
  for (i = 0; i < STR_SIZE; i++)
  {
    if (port_name[i] == ':')
    {
      n = i + 1;
      break;
    }
  }
  for (i = 0; n < STR_SIZE; i++, n++)
  {
    retval[i] = port_name[n];
    if (retval[i] == '\0')
      break;
  }
  return retval;
}

GetOwnPortsRetVal GetOwnPorts(unsigned long flags)
{
  const char **jack_ports_list;

  char **ports_names;
  char **ports_own_names;
  char **ports_namesTmp = new char *[MAX_PORTS];

  jack_ports_list = jack_get_ports(::client, NULL, NULL, flags);

  uint16_t i = 0, n = 0, m = 0;

  while (jack_ports_list[i])
  {
    if (i >= MAX_PORTS)
      break;
    uint8_t found = 1;
    for (n = 0;; n++)
    {
      if (n >= STR_SIZE - 1)
      {
        found = 0;
        break;
      }
      if (client_name[n] == '\0' && jack_ports_list[i][n] == ':')
      {
        break;
      }
      if (client_name[n] != jack_ports_list[i][n])
      {
        found = 0;
        break;
      }
    }
    if (found == 1)
    {
      ports_namesTmp[m] = new char[STR_SIZE];
      for (n = 0; n < STR_SIZE; n++)
      {
        ports_namesTmp[m][n] = jack_ports_list[i][n];
        if (jack_ports_list[i][n] == '\0')
          break;
      }
      m++;
    }
    i++;
  }
  delete[] jack_ports_list;

  ports_names = new char *[m];
  ports_own_names = new char *[m];
  for (i = 0; i < m; i++)
  {
    ports_names[i] = new char[STR_SIZE];
    for (n = 0; n < STR_SIZE; n++)
    {
      ports_names[i][n] = ports_namesTmp[i][n];
      if (ports_namesTmp[i][n] == '\0')
        break;
    }
    delete[] ports_namesTmp[i];
    ports_own_names[i] = GetPortNameWithoutClientName(ports_names[i]);
  }
  delete[] ports_namesTmp;

  GetOwnPortsRetVal retval;
  retval.names = ports_names;
  retval.own_names = ports_own_names;
  retval.count = m;

  return retval;
}

void ResetOwnPortsList()
{
  GetOwnPortsRetVal retval;
  uint8_t i = 0;

  // in
  retval = GetOwnPorts(JackPortIsInput);
  for (i = 0; i < own_in_ports_size; i++)
  {
    delete[] own_in_ports[i];
    delete[] own_in_ports_short_names[i];
  }
  delete[] own_in_ports;
  delete[] own_in_ports_short_names;
  own_in_ports = retval.names;
  own_in_ports_short_names = retval.own_names;
  own_in_ports_size = retval.count;

  // out
  retval = GetOwnPorts(JackPortIsOutput);
  for (i = 0; i < own_out_ports_size; i++)
  {
    delete[] own_out_ports[i];
    delete[] own_out_ports_short_names[i];
  }
  delete[] own_out_ports;
  delete[] own_out_ports_short_names;
  own_out_ports = retval.names;
  own_out_ports_short_names = retval.own_names;
  own_out_ports_size = retval.count;
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
int check_port_connection(const char *src_port_name, const char *dst_port_name) // {{{1
{
  jack_port_t *src_port = jack_port_by_name(client, src_port_name);
  const char **existing_connections = jack_port_get_all_connections(client, src_port);
  if (existing_connections)
  {
    for (int i = 0; existing_connections[i]; i++)
    {
      for (int c = 0;; c++)
      {
        if (existing_connections[i][c] != dst_port_name[c])
        {
          break;
        }

        if (existing_connections[i][c] == '\0')
        {
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
  for (uint8_t i = 0; i < portsList->Length(); i++)
  {
    String::AsciiValue port_name_arg(portsList->Get(i)->ToString());
    char *port_name = *port_name_arg;

    for (uint16_t n = 0;; n++)
    {
      if (port_name[n] == '\0' || check_port_name[n] == '\0' || n >= STR_SIZE - 1)
      {
        if (port_name[n] == check_port_name[n])
        {
          return true;
        }
        else
        {
          break;
        }
      }
      else if (port_name[n] != check_port_name[n])
      {
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
int16_t get_own_out_port_index(char *short_port_name) // {{{1
{
  for (uint8_t n = 0; n < own_out_ports_size; n++)
  {
    for (uint16_t m = 0; m < STR_SIZE; m++)
    {
      if (
          short_port_name[m] == '\0' ||
          own_out_ports_short_names[n][m] == '\0')
      {
        if (short_port_name[m] == own_out_ports_short_names[n][m])
        {
          return n; // index of port
        }
        else
        {
          break; // go to next port
        }
      }
      else if (short_port_name[m] != own_out_ports_short_names[n][m])
      {
        break; // go to next port
      }
    } // for (char of port name)
  }   // for (ports)

  return -1; // port not found
} // check_own_out_port_exists() }}}1

// processing {{{1

#define UV_PROCESS_STOP()                \
  {                                      \
    napi_value undefined;                \
    napi_get_undefined(env, &undefined); \
    delete task;                         \
    baton = NULL;                        \
    uv_sem_post(&semaphore);             \
    return undefined;                    \
  }

#define UV_PROCESS_EXCEPTION(err)                                          \
  {                                                                        \
    const uint8_t argc = 1;                                                \
    napi_value argv[argc];                                                 \
    napi_create_string_utf8(env, err, NAPI_AUTO_LENGTH, &argv[0]);         \
    napi_value global;                                                     \
    napi_get_global(env, &global);                                         \
    napi_value result;                                                     \
    napi_call_function(env, global, processCallback, argc, argv, &result); \
    UV_PROCESS_STOP();                                                     \
  }

void uv_process(uv_work_t *task, int status) // {{{2
{
  HandleScope scope;

  uint16_t nframes = *((uint16_t *)(&task->data));

  Local<Object> capture = Object::New();
  for (uint8_t i = 0; i < own_in_ports_size; i++)
  {
    Local<Array> portBuf = Array::New(nframes);
    for (uint16_t n = 0; n < nframes; n++)
    {
      Local<Number> sample = Number::New(capture_buf[i][n]);
      portBuf->Set(n, sample);
    }
    capture->Set(
        String::NewSymbol(own_in_ports_short_names[i]),
        portBuf);
  }

  const uint8_t argc = 3;
  Local<Value> argv[argc] = {
      Local<Value>::New(Null()),
      Local<Number>::New(Number::New(nframes)),
      Local<Object>::New(capture)};
  Local<Value> retval =
      processCallback->Call(Context::GetCurrent()->Global(), argc, argv);

  if (!retval->IsNull() && !retval->IsUndefined() && !retval->IsObject())
  {
    UV_PROCESS_EXCEPTION(
        Exception::TypeError(String::New(
            "Returned value of \"process\" callback must be an object"
            " of port{String}:buffer{Array.<Number|Float>} values"
            " or null or undefined")));
  }

  if (retval->IsObject())
  {
    Local<Object> obj = retval.As<Object>();
    Local<Array> keys = obj->GetOwnPropertyNames();
    for (uint16_t i = 0; i < keys->Length(); i++)
    {
      Local<Value> key = keys->Get(i);
      if (!key->IsString())
      {
        UV_PROCESS_EXCEPTION(
            Exception::TypeError(String::New(
                "Incorrect key type in returned value of \"process\""
                " callback, must be a string (own port name)")));
      }
      String::AsciiValue port_name(key->ToString());

      int16_t port_index = get_own_out_port_index(*port_name);
      if (port_index == -1)
      {
        char err[] = "Port \"%s\" not found";
        char err_msg[STR_SIZE + sizeof(err)];
        sprintf(err_msg, err, *port_name);
        UV_PROCESS_EXCEPTION(Exception::Error(String::New(err_msg)));
      }

      Local<Value> val = obj->Get(key);
      if (!val->IsArray())
      {
        UV_PROCESS_EXCEPTION(
            Exception::TypeError(String::New(
                "Incorrect buffer type of returned value of \"process\""
                " callback, must be an Array<Float|Number>")));
      }
      Local<Array> buffer = val.As<Array>();

      if (buffer->Length() != nframes)
      {
        UV_PROCESS_EXCEPTION(
            Exception::RangeError(String::New(
                "Incorrect buffer size of returned value"
                " of \"process\" callback")));
      }

      for (uint16_t sample_i = 0; sample_i < nframes; sample_i++)
      {
        Local<Value> sample = buffer->Get(sample_i);
        if (!sample->IsNumber())
        {
          UV_PROCESS_EXCEPTION(
              Exception::TypeError(String::New(
                  "Incorrect sample type of returned value"
                  " of \"process\" callback"
                  ", must be a {Number|Float}")));
        }
        playback_buf[port_index][sample_i] = sample->ToNumber()->Value();
      }
    } // for (ports)
  }   // if we has something to output from callback

  UV_PROCESS_STOP();
} // uv_process() }}}2

int jack_process(jack_nframes_t nframes, void *arg) // {{{2
{
  if (!process)
    return 0;
  if (!hasProcessCallback)
    return 0;

  if (baton)
  {
    uv_sem_wait(&semaphore);
    uv_sem_destroy(&semaphore);
  }

  baton = new uv_work_t();

  if (uv_sem_init(&semaphore, 0) < 0)
  {
    perror("uv_sem_init");
    return 1;
  }

  for (uint8_t i = 0; i < own_in_ports_size; i++)
  {
    capture_buf[i] = (jack_default_audio_sample_t *)
        jack_port_get_buffer(capture_ports[i], nframes);
  }

  for (uint8_t i = 0; i < own_out_ports_size; i++)
  {
    playback_buf[i] = (jack_default_audio_sample_t *)
        jack_port_get_buffer(playback_ports[i], nframes);
  }

  baton->data = (void *)(uint16_t)nframes;
  uv_queue_work(uv_default_loop(), baton, uv_work_plug, uv_process);
  uv_sem_wait(&semaphore);
  uv_sem_destroy(&semaphore);

  return 0;
} // jack_process() }}}2

// processing }}}1

/**
 * Get JACK sample rate
 *
 * @public
 * @returns {v8::Number} sampleRate
 * @example
 *   var jackConnector = require('jack-connector');
 *   jackConnector.openClientSync('jack_client_name');
 *   console.log( jackConnector.getSampleRateSync() );
 */

napi_value getSampleRateSync(napi_env env, napi_callback_info info) // {{{1
{
  napi_status status;
  NEED_JACK_CLIENT_OPENED();
  double sampleRate = jack_get_sample_rate(client);
  napi_value val;
  status = napi_create_double(env, sampleRate, &val);
  return val;
} // getSampleRateSync() }}}1

napi_value getBufferSizeSync(napi_env env, napi_callback_info info) // {{{1
{
  napi_status status;
  NEED_JACK_CLIENT_OPENED();
  double bufferSize = jack_get_buffer_size(client);
  napi_value val;
  status = napi_create_double(env, bufferSize, &val);
  return val;
} // getBufferSizeSync() }}}1

#define DECLARE_NAPI_METHOD(name, func)     \
  {                                         \
    name, 0, func, 0, 0, 0, napi_default, 0 \
  }

napi_value Init(napi_env env, napi_value exports) // {{{1
{
  napi_status status;

  napi_property_descriptor descriptors[] = {
      DECLARE_NAPI_METHOD("getVersion", getVersion),
      DECLARE_NAPI_METHOD("checkClientOpenedSync", checkClientOpenedSync),
      DECLARE_NAPI_METHOD("openClientSync", openClientSync),
      DECLARE_NAPI_METHOD("closeClient", closeClient),
      DECLARE_NAPI_METHOD("registerInPortSync", registerInPortSync),
      DECLARE_NAPI_METHOD("registerOutPortSync", registerOutPortSync),
      DECLARE_NAPI_METHOD("unregisterPortSync", unregisterPortSync),
      DECLARE_NAPI_METHOD("connectPortSync", connectPortSync),
      DECLARE_NAPI_METHOD("disconnectPortSync", disconnectPortSync),
      DECLARE_NAPI_METHOD("getAllPortsSync", getAllPortsSync),
      DECLARE_NAPI_METHOD("getOutPortsSync", getOutPortsSync),
      DECLARE_NAPI_METHOD("getInPortsSync", getInPortsSync),
      DECLARE_NAPI_METHOD("portExistsSync", portExistsSync),
      DECLARE_NAPI_METHOD("outPortExistsSync", outPortExistsSync),
      DECLARE_NAPI_METHOD("inPortExistsSync", inPortExistsSync),
      DECLARE_NAPI_METHOD("bindProcessSync", bindProcessSync),
      DECLARE_NAPI_METHOD("checkActiveSync", checkActiveSync),
      DECLARE_NAPI_METHOD("activateSync", activateSync),
      DECLARE_NAPI_METHOD("deactivateSync", deactivateSync),
      DECLARE_NAPI_METHOD("getSampleRateSync", getSampleRateSync),
      DECLARE_NAPI_METHOD("getBufferSizeSync", getBufferSizeSync),
  };

  status = napi_define_properties(env, exports, sizeof(descriptors) / sizeof(*descriptors), descriptors);

  if (status != napi_ok)
  {
    napi_throw_error(env, NULL, "Error defining exports properties");
  }

  return exports;
} // Init() }}}1

NAPI_MODULE(jack_connector, Init);

// vim:set ts=4 sts=4 sw=4 et:
