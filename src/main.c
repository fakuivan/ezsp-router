/***************************************************************************//**
 * @file main.c
 * @brief main() function.
 *******************************************************************************
 * # License
 * <b>Copyright 2021 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include "sl_component_catalog.h"
#include "sl_system_init.h"
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
#include "sl_power_manager.h"
#endif
#if defined(SL_CATALOG_KERNEL_PRESENT)
#include "sl_system_kernel.h"
#else
#include "sl_system_process_action.h"
#endif // SL_CATALOG_KERNEL_PRESENT

#include "af.h"
#include "ezsp-enum-decode.h"
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "commands.h"

#define INVALID_FD -1
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#define assertAppCase(cond, message_lit_and_args...) if (!(cond)) {\
  emberAfAppPrintln("ERROR: " message_lit_and_args); \
  emberAfAppFlush(); \
  assert(false); \
}

#define logInfoWith(func, args...) func("INFO: " args)

#define logInfoln(args...) logInfoWith(emberAfAppPrintln, args)
#define logInfo(args...) logInfoWith(emberAfAppPrint, args)

typedef enum {
  APP_STATE_UNKNOWN,
  APP_STATE_DISCONNECTED,
  APP_STATE_RECONNECTING,
  APP_STATE_NO_NETWORK,
  APP_STATE_SCANNING,
  APP_STATE_SCANNED,
  APP_STATE_JOINING,
  APP_STATE_CONNECTED,
  APP_STATE_HALTED
} APP_STATE;
APP_STATE app_state = APP_STATE_UNKNOWN;

const char* decode_app_state(APP_STATE state) {
  switch (state) {
    case APP_STATE_UNKNOWN: return "APP_STATE_UNKNOWN";
    case APP_STATE_DISCONNECTED: return "APP_STATE_DISCONNECTED";
    case APP_STATE_RECONNECTING: return "APP_STATE_RECONNECTING";
    case APP_STATE_NO_NETWORK: return "APP_STATE_NO_NETWORK";
    case APP_STATE_SCANNING: return "APP_STATE_SCANNING";
    case APP_STATE_SCANNED: return "APP_STATE_SCANNED";
    case APP_STATE_JOINING: return "APP_STATE_JOINING";
    case APP_STATE_CONNECTED: return "APP_STATE_CONNECTED";
    case APP_STATE_HALTED: return "APP_STATE_HALTED";
  }
  assert(0);
}

const char* decode_app_state_short(APP_STATE state) {
  switch (state) {
    case APP_STATE_UNKNOWN: return "unknown";
    case APP_STATE_DISCONNECTED: return "disconnected";
    case APP_STATE_RECONNECTING: return "reconnecting";
    case APP_STATE_NO_NETWORK: return "no_network";
    case APP_STATE_SCANNING: return "scanning";
    case APP_STATE_SCANNED: return "scanned";
    case APP_STATE_JOINING: return "joining";
    case APP_STATE_CONNECTED: return "connected";
    case APP_STATE_HALTED: return "halted";
  }
  assert(0);
}

EmberZigbeeNetwork best_network = {};
int8_t best_rssi;
int networks_found = 0;
int join_attempts = 0;

FILE* input_fifo_file = NULL;
FILE* output_fifo_file = NULL;

const int max_join_attempts = 5;
const char input_fifo_name[] = "ezsp_router.in";
const char output_fifo_name[] = "ezsp_router.out";
const char pid_file_name[] = "ezsp_router.pid";

static void on_state_changed(APP_STATE prev_state, APP_STATE new_state) {
  assert(output_fifo_file != NULL);
  fprintf(
    output_fifo_file,
    "state %s %s\n",
    decode_app_state_short(prev_state),
    decode_app_state_short(new_state)
  );
  fflush(output_fifo_file);
}

void remove_fifos() {
  fclose(input_fifo_file);
  fclose(output_fifo_file);
  remove(input_fifo_name);
  remove(output_fifo_name);
}

FILE* fopen_flags(const char* file_name, int flags, const char* mode, int* fd) {
  *fd = open(file_name, flags);
  if (*fd == INVALID_FD) {
    return NULL;
  }
  FILE* file = fdopen(*fd, mode);
  if (!file) {
    close(*fd);
  }
  return file;
}

void init_fifos() {
  logInfoln("Creating input fifo");
  if (mkfifo(input_fifo_name, 0666) != 0) {
    assertAppCase(false, "Failed to create input fifo for IPC: %s", strerror(errno));
  }

  logInfoln("Opening input fifo");
  int fd = INVALID_FD;
  if (!(input_fifo_file = fopen_flags(input_fifo_name, O_RDONLY | O_NONBLOCK, "r", &fd))) {
    logInfoln("Failed to open input control fifo");
    remove(input_fifo_name);
    assertAppCase(false, "Failed to open input control fifo: %s", strerror(errno));
  }

  logInfoln("Creating output fifo");
  if (mkfifo(output_fifo_name, 0666) != 0) {
    remove(input_fifo_name);
    fclose(input_fifo_file);
    assertAppCase(false, "Failed to create input fifo for IPC");
  }

  logInfoln("Opening output fifo");
  if (!(output_fifo_file = fopen_flags(output_fifo_name, O_RDWR | O_NONBLOCK, "w", &fd))) {
    remove(input_fifo_name);
    fclose(input_fifo_file);
    remove(output_fifo_name);
    assertAppCase(false, "Failed to open output control fifo: %s", strerror(errno));
  }
}

static void on_exit() {
  logInfoln("Doing cleanup");
  remove_fifos();
  remove(pid_file_name);
}

bool create_pid_file(const char* file_name) {
  FILE* pid_file = fopen(file_name, "wx");
  if (!pid_file) {
    return false;
  }
  fprintf(pid_file, "%ld\n", (long)getpid());
  fclose(pid_file);
  return true;
}

void app_init(void)
{
  logInfoln("Creating pid file");
  if (!create_pid_file(pid_file_name)) {
    assertAppCase(false, "Failed to create PID file");
  }
  logInfoln("Creating control fifos");
  init_fifos();
  logInfoln("Registering exit function");
  atexit(on_exit);
}

APP_STATE advance_state(APP_STATE next_state) {
  APP_STATE prev_state = app_state;
  if (prev_state == next_state) {
    return prev_state;
  }
  on_state_changed(prev_state, next_state);
  app_state = next_state;
  return prev_state;
}

bool in_state(APP_STATE state) {
  return app_state == state;
}

void unexpectedTransition(EmberNetworkStatus status) {
  APP_STATE prev_state = advance_state(APP_STATE_UNKNOWN);
  emberAfAppPrintln(
    "WARNING: Unexpected transition from app state "
      "%s to APP_STATE_UNKNOWN due to status 0x%02X\n",
    decode_app_state(prev_state),
    status);
  emberAfAppFlush();
}

static const EmberKeyData defaultLinkKey = {
  { 0x5A, 0x69, 0x67, 0x42, 0x65, 0x65, 0x41, 0x6C,
    0x6C, 0x69, 0x61, 0x6E, 0x63, 0x65, 0x30, 0x39 }
};

bool process_command(const char* command) {
  // TODO: join, leave
  // should we just use optarg?
  char argument[COMMAND_MAX_LENGTH+1];
  assert(strlen(command) <= sizeof(argument));
  if (sscanf(command, "%s", argument) < 1) {
    logInfoln("Got empty command");
    return false;
  }
  if (strcmp(argument, "exit") == 0) {
    uint8_t code = 0;
    sscanf(command + strlen(argument), "%hhu", &code);
    logInfoln("Got exit command, code: %hhu", code);
    exit(code);
  }
  logInfoln("Unrecognized command: %s", command);
  return false;
}

void poll_commands() {
  static char commands_buffer[COMMAND_MAX_LENGTH+1];
  static char command_buffer[COMMAND_MAX_LENGTH+1];
  bool have_command, buffer_full = false;
  assert(input_fifo_file);
  if (!read_command(
    fileno(input_fifo_file),
    commands_buffer,
    command_buffer,
    MIN(sizeof(commands_buffer), sizeof(command_buffer)),
    &buffer_full,
    &have_command)
  ) {
    assertAppCase(
      false,
      "Failed to read from command input buffer: %s",
      strerror(errno)
    );
  }
  assertAppCase(!buffer_full,
    "Command exceeded length of buffer (max is %d)", sizeof(command_buffer)-1
  );
  if (have_command) {
    processs_command(command_buffer);
    strcpy(command_buffer, "");
  }
}

void app_process_action(void)
{
  poll_commands();

  if (in_state(APP_STATE_HALTED)) {
    return;
  }

  if (in_state(APP_STATE_CONNECTED)) {
    join_attempts = 0;
    return;
  }

  if (in_state(APP_STATE_SCANNING)) {
    return;
  }

  if (in_state(APP_STATE_RECONNECTING)) {
    return;
  }

  EmberNetworkStatus status = ezspNetworkState();
  if (in_state(APP_STATE_UNKNOWN)) {
    switch (status) {
      case EMBER_NO_NETWORK:
        logInfoln("Joining network");
        advance_state(APP_STATE_NO_NETWORK);
        return;
      case EMBER_JOINED_NETWORK:
        logInfoln("Connected to network");
        advance_state(APP_STATE_CONNECTED);
        return;
      case EMBER_JOINING_NETWORK:
        advance_state(APP_STATE_JOINING);
        return;
      case EMBER_JOINED_NETWORK_NO_PARENT:
        advance_state(APP_STATE_DISCONNECTED);
        return;
      default:
        advance_state(APP_STATE_UNKNOWN);
        return;
    }
  }

  if (in_state(APP_STATE_DISCONNECTED)) {
    if (status != EMBER_JOINED_NETWORK_NO_PARENT) {
      unexpectedTransition(status);
      return;
    }
    logInfoln("Disconnected from network");
    EmberStatus rejoin_status = ezspFindAndRejoinNetwork(true, EMBER_ALL_802_15_4_CHANNELS_MASK);
    if (rejoin_status != EMBER_SUCCESS) {
      unexpectedTransition(status);
      return;
    }
    logInfoln("Trying to reconnect...");
    advance_state(APP_STATE_RECONNECTING);
    return;
  }

  if (in_state(APP_STATE_JOINING)) {
    switch (status) {
      case EMBER_JOINED_NETWORK:
        logInfoln("Joined network");
        advance_state(APP_STATE_CONNECTED);
        return;
      case EMBER_JOINING_NETWORK:
        return;
      default:
        unexpectedTransition(status);
        return;
    }
  }

  if (in_state(APP_STATE_NO_NETWORK)) {
    if (status != EMBER_NO_NETWORK) {
      unexpectedTransition(status);
      return;
    }
    if (join_attempts > max_join_attempts) {
      logInfoln("Max join attempts reached, halting");
      advance_state(APP_STATE_HALTED);
      return;
    }
    join_attempts++;
    logInfoln("Not connected to any network");
    EmberStatus sscan_status = emberStartScan(
      EMBER_ACTIVE_SCAN,
      EMBER_ALL_802_15_4_CHANNELS_MASK,
      3
    );
    if (sscan_status != EMBER_SUCCESS) {
      unexpectedTransition(status);
    } else {
      logInfoln("Starting scan");
      advance_state(APP_STATE_SCANNING);
    }
    return;
  }

  if (in_state(APP_STATE_SCANNED)) {
    if (status != EMBER_NO_NETWORK) {
      unexpectedTransition(status);
      return;
    }
    if (networks_found == 0) {
      logInfoln("Failed to find any joinable networks");
      advance_state(APP_STATE_NO_NETWORK);
      return;
    }
    networks_found = 0;

    EmberInitialSecurityState sec_state;
    (void) memcpy(
      emberKeyContents(&(sec_state.preconfiguredKey)),
      emberKeyContents(&defaultLinkKey),
      EMBER_ENCRYPTION_KEY_SIZE);
    sec_state.bitmask = ( EMBER_TRUST_CENTER_GLOBAL_LINK_KEY
                        | EMBER_HAVE_PRECONFIGURED_KEY
                        | EMBER_REQUIRE_ENCRYPTED_KEY
                        | EMBER_NO_FRAME_COUNTER_RESET);
    logInfoln("Setting initial security state");
    EmberStatus sec_status = ezspSetInitialSecurityState(&sec_state);
    if (sec_status != EMBER_SUCCESS) {
      unexpectedTransition(status);
      return;
    }

    EmberNetworkParameters params;
    (void) memcpy(
      params.extendedPanId,
      best_network.extendedPanId,
      sizeof(params.extendedPanId)
    );
    params.panId = best_network.panId;
    params.radioTxPower = 0;
    params.radioChannel = best_network.channel;
    params.nwkUpdateId = best_network.nwkUpdateId;
    params.channels = 0; // check
    params.nwkManagerId = 0; //check
    logInfoln("Trying to join network");
    EmberStatus join_status = ezspJoinNetwork(EMBER_ROUTER, &params);
    if (join_status != EMBER_SUCCESS) {
      unexpectedTransition(status);
      return;
    }
    advance_state(APP_STATE_JOINING);
  }
}

void emberAfAppNetworkFoundHandler(EmberZigbeeNetwork *network, uint8_t lqi, int8_t rssi) {
  if (app_state != APP_STATE_SCANNING) {
    return;
  }
  logInfo("Found network with EPAN: ");
  printIeeeLine(network->extendedPanId);
  if (!network->allowingJoin) {
    logInfoln("Network doesn't allow joining");
    return;
  }
  logInfoln("Network allows joining");
  if (networks_found == 0 || rssi > best_rssi) {
    networks_found++;
    best_rssi = rssi;
    (void) memcpy(&best_network, network, sizeof(*network));
  }
  if (networks_found > 0) {
    if (rssi > best_rssi) {
      logInfoln("Network has higher RSSI than last best one");
    } else {
      logInfoln("Network has lower RSSI, keeping the last best one for now");
    }
  }
}

void emberAfAppStackStatusCallback(EmberStatus status) {
  if (in_state(APP_STATE_JOINING)) {
    if (status == EMBER_NETWORK_UP) {
      logInfoln("Joined network");
      advance_state(APP_STATE_CONNECTED);
      return;
    }
    if (status == EMBER_JOIN_FAILED) {
      logInfoln("Failed to join network");
      advance_state(APP_STATE_NO_NETWORK);
      return;
    }
    unexpectedTransition(status);
    return;
  }
  if (in_state(APP_STATE_RECONNECTING)) {
    if (status != EMBER_NETWORK_UP) {
      unexpectedTransition(status);
      return;
    }
    logInfoln("Reconnected to network");
    advance_state(APP_STATE_CONNECTED);
    return;
  }
  if (in_state(APP_STATE_CONNECTED)) {
    if (status == EMBER_NETWORK_DOWN) {
      logInfoln("Disconnected from network");
      advance_state(APP_STATE_DISCONNECTED);
    }
    return;
  }
}

void emberAfAppScanCompleteHandler(uint8_t channel, EmberStatus status) {
  if (app_state != APP_STATE_SCANNING) {
    return;
  }
  if (status != EMBER_SUCCESS) {
    unexpectedTransition(status);
  }
  logInfoln("Finished scanning for networks");
  advance_state(APP_STATE_SCANNED);
}

#ifdef EMBER_TEST
int nodeMain(void)
#else
int main(int argc, char* argv[])
#endif
{
#ifndef EMBER_TEST
  {
    // Initialize ezspProcessCommandOptions and gatewayBackchannelStart
    // for host apps running on hardware.
    int returnCode;
    if (emberAfMainStartCallback(&returnCode, argc, argv)) {
      return returnCode;
    }
  }
#endif // EMBER_TEST

  // Initialize Silicon Labs device, system, service(s) and protocol stack(s).
  // Note that if the kernel is present, processing task(s) will be created by
  // this call.
  sl_system_init();

  // Initialize the application. For example, create periodic timer(s) or
  // task(s) if the kernel is present.
  app_init();

#if defined(SL_CATALOG_KERNEL_PRESENT)
  // Start the kernel. Task(s) created in app_init() will start running.
  sl_system_kernel_start();
#else // SL_CATALOG_KERNEL_PRESENT
  while (1) {
    // Do not remove this call: Silicon Labs components process action routine
    // must be called from the super loop.
    sl_system_process_action();

    // Application process.
    app_process_action();

    // Let the CPU go to sleep if the system allow it.
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
    sl_power_manager_sleep();
#endif // SL_CATALOG_POWER_MANAGER_PRESENT
  }
#endif // SL_CATALOG_KERNEL_PRESENT

  return 0;
}
