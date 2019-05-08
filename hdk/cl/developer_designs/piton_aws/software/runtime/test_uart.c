// Amazon FPGA Hardware Development Kit
//
// Copyright 2016 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Amazon Software License (the "License"). You may not use
// this file except in compliance with the License. A copy of the License is
// located at
//
//    http://aws.amazon.com/asl/
//
// or in the "license" file accompanying this file. This file is distributed on
// an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, express or
// implied. See the License for the specific language governing permissions and
// limitations under the License.
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>

#include <utils/sh_dpi_tasks.h>
#include <fpga_pci.h>
#include <fpga_mgmt.h>
#include <utils/lcd.h>
#include "test_uart.h"

/* use the stdout logger for printing debug information  */
const struct logger *logger = &logger_stdout;
/*
 * pci_vendor_id and pci_device_id values below are Amazon's and avaliable to use for a given FPGA slot. 
 * Users may replace these with their own if allocated to them by PCI SIG
 */
static uint16_t pci_vendor_id = 0x1D0F; /* Amazon PCI Vendor ID */
static uint16_t pci_device_id = 0xF001; /* PCI Device ID preassigned by Amazon for F1 applications */

/*
 * check if the corresponding AFI for hello_world is loaded
 */
int check_afi_ready(int slot_id);
/*
 * An example to attach to an arbitrary slot, pf, and bar with register access.
 */
int peek_poke_example(uint32_t value, int slot_id, int pf_id, int bar_id);

void usage(char* program_name) {
    printf("usage: %s [--slot <slot-id>][<poke-value>]\n", program_name);
}


int main(int argc, char **argv) {
    uint32_t value = 0x78;
    int slot_id = 0;
    int rc;
    
    // Process command line args
    {
        int i;
        int value_set = 0;
        for (i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--slot")) {
                i++;
                if (i >= argc) {
                    printf("error: missing slot-id\n");
                    usage(argv[0]);
                    return 1;
                }
                sscanf(argv[i], "%d", &slot_id);
            } else if (!value_set) {
                sscanf(argv[i], "%x", &value);
                value_set = 1;
            } else {
                printf("error: Invalid arg: %s", argv[i]);
                usage(argv[0]);
                return 1;
            }
        }
    }

    /* initialize the fpga_pci library so we could have access to FPGA PCIe from this applications */
    rc = fpga_pci_init();
    fail_on(rc, out, "Unable to initialize the fpga_pci library");

    rc = check_afi_ready(slot_id);
    fail_on(rc, out, "AFI not ready");

    
    /* Accessing the CL registers via AppPF BAR0, which maps to sh_cl_ocl_ AXI-Lite bus between AWS FPGA Shell and the CL*/

    printf("===== Starting with peek_poke_example =====\n");
    rc = peek_poke_example(value, slot_id, FPGA_APP_PF, APP_PF_BAR0);
    fail_on(rc, out, "peek-poke example failed");

    return rc;
    
out:
    return 1;
}

 int check_afi_ready(int slot_id) {
   struct fpga_mgmt_image_info info = {0}; 
   int rc;

   /* get local image description, contains status, vendor id, and device id. */
   rc = fpga_mgmt_describe_local_image(slot_id, &info,0);
   fail_on(rc, out, "Unable to get AFI information from slot %d. Are you running as root?",slot_id);

   /* check to see if the slot is ready */
   if (info.status != FPGA_STATUS_LOADED) {
     rc = 1;
     fail_on(rc, out, "AFI in Slot %d is not in READY state !", slot_id);
   }

   printf("AFI PCI  Vendor ID: 0x%x, Device ID 0x%x\n",
          info.spec.map[FPGA_APP_PF].vendor_id,
          info.spec.map[FPGA_APP_PF].device_id);

   /* confirm that the AFI that we expect is in fact loaded */
   if (info.spec.map[FPGA_APP_PF].vendor_id != pci_vendor_id ||
       info.spec.map[FPGA_APP_PF].device_id != pci_device_id) {
     printf("AFI does not show expected PCI vendor id and device ID. If the AFI "
            "was just loaded, it might need a rescan. Rescanning now.\n");

     rc = fpga_pci_rescan_slot_app_pfs(slot_id);
     fail_on(rc, out, "Unable to update PF for slot %d",slot_id);
     /* get local image description, contains status, vendor id, and device id. */
     rc = fpga_mgmt_describe_local_image(slot_id, &info,0);
     fail_on(rc, out, "Unable to get AFI information from slot %d",slot_id);

     printf("AFI PCI  Vendor ID: 0x%x, Device ID 0x%x\n",
            info.spec.map[FPGA_APP_PF].vendor_id,
            info.spec.map[FPGA_APP_PF].device_id);

     /* confirm that the AFI that we expect is in fact loaded after rescan */
     if (info.spec.map[FPGA_APP_PF].vendor_id != pci_vendor_id ||
         info.spec.map[FPGA_APP_PF].device_id != pci_device_id) {
       rc = 1;
       fail_on(rc, out, "The PCI vendor id and device of the loaded AFI are not "
               "the expected values.");
     }
   }
    
   return rc;
 out:
   return 1;
 }

/*
 * An example to attach to an arbitrary slot, pf, and bar with register access.
 */
int peek_poke_example(uint32_t value, int slot_id, int pf_id, int bar_id) {
    int rc;
    /* pci_bar_handle_t is a handler for an address space exposed by one PCI BAR on one of the PCI PFs of the FPGA */

    pci_bar_handle_t pci_bar_handle = PCI_BAR_HANDLE_INIT;

    /* attach to the fpga, with a pci_bar_handle out param
     * To attach to multiple slots or BARs, call this function multiple times,
     * saving the pci_bar_handle to specify which address space to interact with in
     * other API calls.
     * This function accepts the slot_id, physical function, and bar number
     */
    rc = fpga_pci_attach(slot_id, pf_id, bar_id, 0, &pci_bar_handle);
    fail_on(rc, out, "Unable to attach to the AFI on slot id %d", slot_id);
    
    /* init uart regs */

    rc = fpga_pci_poke(pci_bar_handle, IER_ADDR, UINT32_C(0));
    fail_on(rc, out, "Unable to write to the fpga !");

    rc = fpga_pci_poke(pci_bar_handle, FCR_ADDR, UINT32_C(0));
    fail_on(rc, out, "Unable to write to the fpga !");
    
    rc = fpga_pci_poke(pci_bar_handle, FCR_ADDR, FCR_XMIT_RESET|FCR_RCVR_RESET);
    fail_on(rc, out, "Unable to write to the fpga !");

    rc = fpga_pci_poke(pci_bar_handle, FCR_ADDR, FCR_FIFO_ENABLE);
    fail_on(rc, out, "Unable to write to the fpga !");

    rc = fpga_pci_poke(pci_bar_handle, LCR_ADDR, LCR_DLAB | LCR_8N1);
    fail_on(rc, out, "Unable to write to the fpga !");

    rc = fpga_pci_poke(pci_bar_handle, DLL_ADDR, 0);
    fail_on(rc, out, "Unable to write to the fpga !");

    rc = fpga_pci_poke(pci_bar_handle, DLM_ADDR, 1);
    fail_on(rc, out, "Unable to write to the fpga !");
    
    rc = fpga_pci_poke(pci_bar_handle, LCR_ADDR, LCR_8N1);
    fail_on(rc, out, "Unable to write to the fpga !");
   
    uint32_t expected = value & 0xff;  

    /* Send a value */
    printf("Writing to THR register (0x%016x)\n", THR_ADDR);
    uint32_t temt = 0;
    do {
        uint32_t tmp = 0;
        rc = fpga_pci_peek(pci_bar_handle, LSR_ADDR, &tmp);
        fail_on(rc, out, "Unable to read read from the fpga !");
        temt = tmp & LSR_TEMT;
    } while (!temt);

    rc = fpga_pci_poke(pci_bar_handle, THR_ADDR, value);
    fail_on(rc, out, "Unable to write to the fpga !");

    /* Read a value */
    printf("Reading from RBR register (0x%016x)\n", RBR_ADDR);
    uint32_t drdy = 0;
    do {
        uint32_t tmp = 0;
        rc = fpga_pci_peek(pci_bar_handle, LSR_ADDR, &tmp);
        fail_on(rc, out, "Unable to read read from the fpga !");
        drdy = tmp & LSR_DRDY;
    } while (!drdy);

    rc = fpga_pci_peek(pci_bar_handle, RBR_ADDR, &value);
    fail_on(rc, out, "Unable to read read from the fpga !");
    printf("register: 0x%x\n", value);
    if(value == expected) {
        printf("TEST PASSED\n");
        printf("Resulting value matched expected value 0x%x. It worked!\n", expected);
    }
    else{
        printf("TEST FAILED\n");
        printf("Resulting value did not match expected value 0x%x. Something didn't work.\n", expected);
    }
out:
    /* clean up */
    if (pci_bar_handle >= 0) {
        rc = fpga_pci_detach(pci_bar_handle);
        if (rc) {
            printf("Failure while detaching from the fpga.\n");
        }
    }

    /* if there is an error code, exit with status 1 */
    return (rc != 0 ? 1 : 0);
}

