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

#define assertAppCase(cond, message_lit, args...) if (!cond) {\
  emberAfAppPrintln("ERROR: " message_lit "\n", args); \
  emberAfAppFlush(); \
  assert(false); \
}

typedef enum {
  APP_STATE_UNKNOWN,
  APP_STATE_DISCONNECTED,
  APP_STATE_RECONNECTING,
  APP_STATE_NO_NETWORK,
  APP_STATE_SCANNING,
  APP_STATE_SCANNED,
  APP_STATE_JOINING,
  APP_STATE_CONNECTED
} APP_STATE;
APP_STATE app_state = APP_STATE_UNKNOWN;

EmberZigbeeNetwork best_network = {};
int8_t best_rssi;
int networks_found = 0;

void app_init(void)
{
  static EmberNetworkInitStruct init_opts = {
    .bitmask = EMBER_NETWORK_INIT_END_DEVICE_REJOIN_ON_REBOOT
  };
  EmberStatus init_status = ezspNetworkInit(&init_opts);
  assertAppCase(init_status != EZSP_SUCCESS, "Failed to init network with status: 0x%02X", init_status);
}

void unexpectedTransition(APP_STATE *from, EmberNetworkStatus status) {
  emberAfAppPrintln(
    "WARNING: Unexpected transition from app state "
      "%d to APP_STATE_UNKNOWN due to status 0x%02X\n",
    *from,
    status);
  emberAfAppFlush();
  *from = APP_STATE_UNKNOWN;
}

static const EmberKeyData defaultLinkKey = {
  { 0x5A, 0x69, 0x67, 0x42, 0x65, 0x65, 0x41, 0x6C,
    0x6C, 0x69, 0x61, 0x6E, 0x63, 0x65, 0x30, 0x39 }
};

void app_process_action(void)
{
  EmberNetworkStatus status = ezspNetworkState();
  if (app_state == APP_STATE_UNKNOWN) {
    switch (status) {
      case EMBER_NO_NETWORK:
        app_state = APP_STATE_NO_NETWORK;
        return;
      case EMBER_JOINED_NETWORK:
        app_state = APP_STATE_CONNECTED;
        return;
      case EMBER_JOINING_NETWORK:
        app_state = APP_STATE_JOINING;
        return;
      case EMBER_JOINED_NETWORK_NO_PARENT:
        app_state = APP_STATE_DISCONNECTED;
        return;
      default:
        app_state = APP_STATE_UNKNOWN;
        return;
    }
  }

  if (app_state == APP_STATE_DISCONNECTED) {
    if (status != EMBER_JOINED_NETWORK_NO_PARENT) {
      unexpectedTransition(&app_state, status);
      return;
    }
    EmberStatus rejoin_status = ezspFindAndRejoinNetwork(true, EMBER_ALL_802_15_4_CHANNELS_MASK);
    if (rejoin_status != EMBER_SUCCESS) {
      unexpectedTransition(&app_state, status);
      return;
    }
    app_state = APP_STATE_RECONNECTING;
    return;
  }

  if (app_state == APP_STATE_RECONNECTING) {
    return;
  }

  if (app_state == APP_STATE_JOINING) {
    switch (status) {
      case EMBER_JOINED_NETWORK:
        app_state = APP_STATE_CONNECTED;
        return;
      case EMBER_JOINING_NETWORK:
        return;
      default:
        unexpectedTransition(&app_state, status);
        return;
    }
  }

  if (app_state == APP_STATE_NO_NETWORK) {
    if (status != EMBER_NO_NETWORK) {
      unexpectedTransition(&app_state, status);
      return;
    }
    EmberStatus sscan_status = emberStartScan(
      EMBER_ACTIVE_SCAN,
      EMBER_ALL_802_15_4_CHANNELS_MASK,
      3
    );
    if (sscan_status != EMBER_SUCCESS)
      unexpectedTransition(&app_state, status);
    else
      app_state = APP_STATE_SCANNING;
    return;
  }

  if (app_state == APP_STATE_SCANNING) {
    return;
  }

  if (app_state == APP_STATE_SCANNED) {
    if (status != EMBER_NO_NETWORK) {
      unexpectedTransition(&app_state, status);
      return;
    }
    if (networks_found == 0) {
      app_state = APP_STATE_NO_NETWORK;
      return;
    }

    EmberInitialSecurityState sec_state;
    (void) memcpy(
      emberKeyContents(&(sec_state.preconfiguredKey)),
      emberKeyContents(&defaultLinkKey),
      EMBER_ENCRYPTION_KEY_SIZE);
    sec_state.bitmask = ( EMBER_TRUST_CENTER_GLOBAL_LINK_KEY
                        | EMBER_HAVE_PRECONFIGURED_KEY
                        | EMBER_REQUIRE_ENCRYPTED_KEY
                        | EMBER_NO_FRAME_COUNTER_RESET);
    EmberStatus sec_status = ezspSetInitialSecurityState(&sec_state);
    if (sec_status != EMBER_SUCCESS) {
      unexpectedTransition(&app_state, status);
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
    EmberStatus join_status = ezspJoinNetwork(EMBER_ROUTER, &params);
    if (join_status != EMBER_SUCCESS) {
      unexpectedTransition(&app_state, status);
      return;
    }
    app_state = APP_STATE_JOINING;
  }

  if (app_state == APP_STATE_CONNECTED) {
    return;
  }
}

void emberAfAppNetworkFoundHandler(EmberZigbeeNetwork *network, uint8_t lqi, int8_t rssi) {
  if (app_state != APP_STATE_SCANNING) {
    return;
  }
  if (!network->allowingJoin) {
    return;
  }
  if (networks_found == 0 || rssi > best_rssi) {
    networks_found++;
    best_rssi = rssi;
    (void) memcpy(&best_network, network, sizeof(network));
  }
}

void emAfStackStatusCallback(EmberStatus status) {
  if (app_state == APP_STATE_JOINING) {
    if (status == EMBER_NETWORK_UP) {
      app_state = APP_STATE_CONNECTED;
      return;
    }
    if (status == EMBER_JOIN_FAILED) {
      app_state = APP_STATE_NO_NETWORK;
      return;
    }
    unexpectedTransition(&app_state, status);
    return;
  }
  if (app_state == APP_STATE_RECONNECTING) {
    if (status != EMBER_NETWORK_UP) {
      unexpectedTransition(&app_state, status);
      return;
    }
    app_state = APP_STATE_CONNECTED;
    return;
  }
  if (app_state == APP_STATE_CONNECTED) {
    if (status == EMBER_NETWORK_DOWN) {
      app_state = APP_STATE_DISCONNECTED;
    }
    return;
  }
}

// These callbacks won't get called
void emberAfAppScanCompleteHandler(uint8_t channel, EmberStatus status) {
  if (app_state != APP_STATE_SCANNING) {
    return;
  }
  if (status != EMBER_SUCCESS) {
    unexpectedTransition(&app_state, status);
  }
  app_state = APP_STATE_SCANNED;
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
