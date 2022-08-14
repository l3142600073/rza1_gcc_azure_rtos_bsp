/**************************************************************************/
/*                                                                        */
/*       Copyright (c) Microsoft Corporation. All rights reserved.        */
/*                                                                        */
/*       This software is licensed under the Microsoft Software License   */
/*       Terms for Microsoft Azure RTOS. Full text of the license can be  */
/*       found in the LICENSE file at https://aka.ms/AzureRTOS_EULA       */
/*       and in the root directory of this software.                      */
/*                                                                        */
/**************************************************************************/


/**************************************************************************/
/**************************************************************************/
/**                                                                       */ 
/** USBX Component                                                        */ 
/**                                                                       */
/**   RZ Controller Driver                                                */
/**                                                                       */
/**************************************************************************/
/**************************************************************************/


/* Include necessary system files.  */

#define UX_SOURCE_CODE

#include "fcntl.h"
#include "ux_api.h"
#include "ux_hcd_rz.h"
#include "ux_system.h"
#include "ux_utility.h"

#include "r_dmac_drv_api.h"
#include "iodefine.h"

/* DMA driver */
int_t dma_usb_out_handle = -1;
int_t dma_usb_in_handle = -1;

/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _ux_hcd_rz_initialize                               PORTABLE C      */ 
/*                                                           5.8          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Thierry Giron                                                       */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function initializes the RZ controller.                        */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    HCD                                   Pointer to HCD                */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    Completion Status                                                   */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _ux_hcd_rz_register_read            Read RZ register read           */ 
/*    _ux_hcd_rz_register_write           Write RZ register write         */ 
/*    _ux_hcd_rz_periodic_tree_create     Create periodic tree            */ 
/*    _ux_hcd_rz_power_root_hubs          Power root HUBs                 */ 
/*    _ux_hcd_rz_low_level_setup          Low-level setup on RZ           */   
/*    _ux_utility_memory_allocate         Allocate memory block           */ 
/*    _ux_utility_set_interrupt_handler   Setup interrupt handler         */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    Host Stack                                                          */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2014     TCRG                     Initial Version 5.7           */ 
/*  06-01-2017     TCRG                     Modified comment(s),          */ 
/*                                            resulting in version 5.8    */ 
/*                                                                        */ 
/**************************************************************************/ 
UINT  _ux_hcd_rz_initialize(UX_HCD *hcd)
{   

TX_INTERRUPT_SAVE_AREA
  
UX_HCD_RZ           *hcd_rz;
UINT                status;
ULONG               buf1, buf2, buf3;
st_r_drv_dmac_config_t dma_config;

    /* Call low-level setup RZ.  */
    _ux_hcd_rz_low_level_setup();

    /* The controller initialized here is of RZ type.  */
    hcd -> ux_hcd_controller_type =  UX_RZ_CONTROLLER;
    
    /* Ensure we have compiled for the proper controller.  */
    if (hcd -> ux_hcd_irq != UX_RZ_CONTROLLER_RZ)
    {

        /* Error trap. */
        _ux_system_error_handler(UX_SYSTEM_LEVEL_THREAD, UX_SYSTEM_CONTEXT_HCD, UX_CONTROLLER_INIT_FAILED);

        /* If trace is enabled, insert this event into the trace buffer.  */
        UX_TRACE_IN_LINE_INSERT(UX_TRACE_ERROR, UX_CONTROLLER_INIT_FAILED, 0, 0, 0, UX_TRACE_ERRORS, 0, 0)

        return (UX_CONTROLLER_INIT_FAILED);
    }           
  
    /* Initialize the max bandwidth for periodic endpoints. On rz, the spec says 
       no more than 90% to be allocated for periodic.  */
    hcd -> ux_hcd_available_bandwidth =  UX_RZ_HC_AVAILABLE_BANDWIDTH;

    /* Allocate memory for this RZ HCD instance.  */
    hcd_rz =  _ux_utility_memory_allocate(UX_NO_ALIGN, UX_REGULAR_MEMORY, sizeof(UX_HCD_RZ));
    if (hcd_rz == UX_NULL)
        return(UX_MEMORY_INSUFFICIENT);

    /* Set the pointer to the RZ HCD.  */
    hcd -> ux_hcd_controller_hardware =  (VOID *) hcd_rz;

    /* Set the generic HCD owner for the RZ HCD.  */
    hcd_rz -> ux_hcd_rz_hcd_owner =  hcd;

    /* Initialize the function collector for this HCD.  */
    hcd -> ux_hcd_entry_function =  _ux_hcd_rz_entry;

    /* Set the state of the controller to HALTED first.  */
    hcd -> ux_hcd_status =  UX_HCD_STATUS_HALTED;
    
    /* Allocate the list of eds. All eds are allocated on 16 byte memory boundary.  */
    hcd_rz -> ux_hcd_rz_ed_list =  _ux_utility_memory_allocate(UX_ALIGN_16, UX_REGULAR_MEMORY, sizeof(UX_RZ_ED) *_ux_system_host -> ux_system_host_max_ed);
    if (hcd_rz -> ux_hcd_rz_ed_list == UX_NULL)
        return(UX_MEMORY_INSUFFICIENT);

    /* Allocate the list of tds. All tds are allocated on 32 byte memory boundary.  */
    hcd_rz -> ux_hcd_rz_td_list =  _ux_utility_memory_allocate(UX_ALIGN_32, UX_REGULAR_MEMORY, sizeof(UX_RZ_TD) *_ux_system_host -> ux_system_host_max_td);
    if (hcd_rz -> ux_hcd_rz_td_list == UX_NULL)
        return(UX_MEMORY_INSUFFICIENT);

    /* Allocate the list of isochronous tds. All tds are allocated on 32 byte memory boundary.  */
    hcd_rz -> ux_hcd_rz_iso_td_list =  _ux_utility_memory_allocate(UX_ALIGN_32, UX_REGULAR_MEMORY, sizeof(UX_RZ_ISO_TD) *_ux_system_host -> ux_system_host_max_iso_td);
    if (hcd_rz -> ux_hcd_rz_td_list == UX_NULL)
        return(UX_MEMORY_INSUFFICIENT);

    /* Initialize the periodic tree.  */
    status =  _ux_hcd_rz_periodic_tree_create(hcd_rz);
    if (status != UX_SUCCESS)
      return(status);
   
    /* So far no device is attached. */
    hcd_rz -> ux_rz_device_attached = UX_FALSE;
    
        _ux_hcd_rz_register_clear(hcd_rz, UX_RZ_HC_SUSPMODE, UX_RZ_HC_SUSPMODE_SUSPM);
        _ux_hcd_rz_register_write(hcd_rz, UX_RZ_HC_SYSCFG,0);

        /* UPLLE bit only available in channel 0.  */
        _ux_hcd_rz_register_set(hcd_rz, UX_RZ_HC_SYSCFG_0, UX_RZ_HC_SYSCFG_UPLLE ); 
        tx_thread_sleep(150 * UX_PERIODIC_RATE / 1000);
        _ux_hcd_rz_register_set(hcd_rz, UX_RZ_HC_SUSPMODE, UX_RZ_HC_SUSPMODE_SUSPM);
        
         _ux_hcd_rz_register_write(hcd_rz, UX_RZ_HC_BUSWAIT,1); /* USB_HOST_BUSWAIT_03*/
    
        /* Initialize the SYSCFG register. Enable Host, High Speed, DRPD ....  
           Set/initialize host controller functionality.  */
         
        _ux_hcd_rz_register_set(hcd_rz, UX_RZ_HC_SYSCFG, UX_RZ_HC_SYSCFG_DCFM); /* HOST mode  */
        _ux_hcd_rz_register_set(hcd_rz, UX_RZ_HC_SYSCFG, UX_RZ_HC_SYSCFG_HSE);  /* High Speed */
        _ux_hcd_rz_register_set(hcd_rz, UX_RZ_HC_SYSCFG, UX_RZ_HC_SYSCFG_DRPD); /* PORT0 D+, D- setting */

  /* renesas code does this */

    do {
        buf1 = _ux_hcd_rz_register_read(hcd_rz, UX_RZ_HC_SYSSTS0);
        tx_thread_sleep( 100 * UX_PERIODIC_RATE / 1000 );
        buf2 =  _ux_hcd_rz_register_read(hcd_rz, UX_RZ_HC_SYSSTS0);
        tx_thread_sleep( 100 * UX_PERIODIC_RATE / 1000 );
        buf3 =  _ux_hcd_rz_register_read(hcd_rz, UX_RZ_HC_SYSSTS0);

    } while( ( buf1 != buf2 ) || ( buf1 != buf3 ) );

       /* USB clock operation enabled.  */
    _ux_hcd_rz_register_set(hcd_rz, UX_RZ_HC_SYSCFG, UX_RZ_HC_SYSCFG_USBE); 
 
    /* xFIFOSEL initialization.  */
    _ux_hcd_rz_register_set(hcd_rz, UX_RZ_HC_CFIFOSEL, UX_RZ_HC_CFIFOSEL_RCNT | UX_RZ_HC_CFIFOSEL_MBW_8);
    _ux_hcd_rz_register_set(hcd_rz, UX_RZ_HC_D0FIFOSEL,UX_RZ_HC_DFIFOSEL_MBW_8);
    _ux_hcd_rz_register_set(hcd_rz, UX_RZ_HC_D1FIFOSEL,UX_RZ_HC_DFIFOSEL_MBW_8);
   
    /* Set the controller to operational mode.  */
    hcd -> ux_hcd_status =  UX_HCD_STATUS_OPERATIONAL;

    /* The asynchronous queues are empty for now.  */
    hcd_rz -> ux_hcd_rz_queue_empty =  UX_TRUE;

    /* The periodic scheduler is not active.  */
    hcd_rz -> ux_hcd_rz_periodic_scheduler_active =  0;

    /*  Buffer number starts at 8 , till 127 */
    hcd_rz -> ux_rz_next_available_bufnum =  8;
        
    /* Update the device address in the HCD structure.  We will force a resetting later. */
    hcd_rz -> ux_hcd_rz_root_hub_device_address = 0xffffffff;

#ifdef UX_RZ_HCD_USE_DMA
    

#if 1
    /* Reset DMA TX channel.  */
    _ux_hcd_rz_dma_register_write(hcd_rz, UX_RZ_DMA_CHCTRL(UX_RZ_DMA_TX_CH),
                                  UX_RZ_DMA_CHCTRL_CLREN | UX_RZ_DMA_CHCTRL_SWRST);

#if UX_RZ_USB_BASE == UX_RZ_USB0_BASE
    /* Select peripheral trigger for DMA TX channel.  */
    _ux_hcd_rz_dma_register_write(hcd_rz, UX_RZ_DMA_TX_RS, UX_RZ_DMA_USB0_TX_RS_VALUE);
#else
    _ux_hcd_rz_dma_register_write(hcd_rz, UX_RZ_DMA_TX_RS, UX_RZ_DMA_USB1_TX_RS_VALUE);	/* UX_RZ_DMA_RS0 lower(ch0), 0x8B */
#endif

    /* Configure DMA TX channel.  */
#if 1	/* grape */
    _ux_hcd_rz_dma_register_write(hcd_rz, UX_RZ_DMA_CHCFG(UX_RZ_DMA_TX_CH),
                              (UX_RZ_DMA_TX_CH & 0x7) | 				/* SEL[2:0] */
                              UX_RZ_DMA_CHCFG_REQD |	/* (1<<3) */	/* REQD[3] */	/* grape add */
							  	  	  	  	  	  	  	  	  	  	  	/* LOEN[4] 0 default??? */
							  UX_RZ_DMA_CHCFG_HIEN |	/* (1<<5) */	/* HIEN[5] */
                              UX_RZ_DMA_CHCFG_LVL | 	/* (1<<6) */	/* LVL[6] */
// grape del                                      (0x1<<8) |
                                                  (0x2<<8) |	/* AM[8:10] bus cycle mode */ /* grape add */
/* grape del */                                            (0x2<<12) |	/* SDS[15:12] 32bit */
/* grape del */                                            (0x2<<16) |	/* DDS[19:16] 32bit */
// grape del							UX_RZ_DMA_CHCFG_SAD	/* (1<<20) */	/* SAD[20] source address fix */
					          UX_RZ_DMA_CHCFG_DAD		/* (1<<21) */	/* DAD[21] destination address fix */	/* grape add */
																/* TM[22] single transfer mode (default) */
																/* DEM[24] comp. interrupt NO mask (default ) */
																/* SBE[27] quit transfer without flash (default) */
																/* RSEL[28] Next0 (default) */
																/* RSW[29] NO flip RSEL after comp. (default) */
																/* REN[30] 0 default??? */
																/* DMS[31] register mode (default) */
	);
#else
    _ux_hcd_rz_dma_register_write(hcd_rz, UX_RZ_DMA_CHCFG(UX_RZ_DMA_TX_CH),
                              (UX_RZ_DMA_TX_CH & 0x7) | 
                              UX_RZ_DMA_CHCFG_LVL | 
                              UX_RZ_DMA_CHCFG_HIEN |
							  (0x1<<8) |
                              (0x2<<12) | (0x2<<16) |
                              UX_RZ_DMA_CHCFG_SAD);
#endif

#else
    dma_usb_out_handle = open(DEVICE_INDENTIFIER "dma_usb_out", O_WRONLY);

    dma_config.config.resource = DMA_RS_USB0_DMA0_TX;                                    /* DMA transfer resource */
	dma_config.config.source_width = DMA_DATA_SIZE_4;                                                /* DMA transfer unit size (source) - 32 bits */
	dma_config.config.destination_width = DMA_DATA_SIZE_4;                                           /* DMA transfer unit size (destination) - 32 bits */
	dma_config.config.source_address_type = DMA_ADDRESS_INCREMENT;                                   /* DMA address type (source) */
	dma_config.config.destination_address_type = DMA_ADDRESS_FIX;                                    /* DMA address type (destination) */
	dma_config.config.direction = DMA_REQUEST_SOURCE;                                                /* DMA transfer direction will be set by the driver */
	dma_config.config.source_address = 0x00000000;           /* Source Address */
	dma_config.config.destination_address = USB200.D0FIFO; /* Destination Address */
	dma_config.config.count = 0;                             /* length */
	dma_config.config.p_dmaComplete = _ux_hcd_rz_dma_tx_interrupt_handler;                                           /* set callback function (DMA end interrupt) */

	control(dma_usb_out_handle, CTL_DMAC_SET_CONFIGURATION, (void *) &dma_config);

	close(dma_usb_out_handle);
#endif

#if 1
    /* Reset DMA RX channel.  */
    _ux_hcd_rz_dma_register_write(hcd_rz, UX_RZ_DMA_CHCTRL(UX_RZ_DMA_RX_CH),
                                  UX_RZ_DMA_CHCTRL_CLREN | UX_RZ_DMA_CHCTRL_SWRST);
#if UX_RZ_USB_BASE == UX_RZ_USB0_BASE
    /* Select peripheral trigger for DMA RX channel.  */
    _ux_hcd_rz_dma_register_set(hcd_rz, UX_RZ_DMA_RX_RS, UX_RZ_DMA_USB0_RX_RS_VALUE);
#else
#if 1	/* grape */
    _ux_hcd_rz_dma_register_set(hcd_rz, UX_RZ_DMA_RX_RS, 0x8B<<16);	/* UX_RZ_DMA_RS0 high(ch1), 0x8F<<16 */
#else
    _ux_hcd_rz_dma_register_set(hcd_rz, UX_RZ_DMA_RX_RS, UX_RZ_DMA_USB1_RX_RS_VALUE);
#endif
#endif

    /* Configure DMA RX channel.  */
#if 1	/* grape */
    _ux_hcd_rz_dma_register_write(hcd_rz, UX_RZ_DMA_CHCFG(UX_RZ_DMA_RX_CH),
                              (UX_RZ_DMA_RX_CH & 0x7) | 				/* SEL[2:0] */
// grape del                  UX_RZ_DMA_CHCFG_REQD |	/* (1<<3) */	/* REQD[3] */
							  	  	  	  	  	  	  	  	  	  	  	/* LOEN[4] 0 default??? */
                              UX_RZ_DMA_CHCFG_HIEN |	/* (1<<5) */	/* HIEN[5] */
							  UX_RZ_DMA_CHCFG_LVL | /* (1<<6) */		/* LVL[6] */ /* grape add */
                                                           (0x2<<8) |	/* AM[8:10] */
/* grape del */                                             (0x2<<12) |	/* SDS[15:12] 32bit */
/* grape del */                                             (0x2<<16) |	/* DDS[19:16] 32 bit */
// grape del														   	   	   	    /* SAD[20] source address increment (default) */
							  UX_RZ_DMA_CHCFG_SAD /* (1<<20) */		/* SAD[20] source address fix */
//* grape del */							  UX_RZ_DMA_CHCFG_DAD		/* (1<<21) */	/* DAD[21] destination address fix */
// grape del															/* TM[22] single transfer mode (default) */
																		/* DEM[24] comp. interrupt NO mask (default ) */
																		/* SBE[27] quit transfer without flash (default) */
																		/* RSEL[28] Next0 (default) */
																		/* RSW[29] NO flip RSEL after comp. (default) */
																		/* REN[30] 0 default??? */
																		/* DMS[31] register mode (default) */
    						  );
#else
    _ux_hcd_rz_dma_register_write(hcd_rz, UX_RZ_DMA_CHCFG(UX_RZ_DMA_RX_CH),
                              (UX_RZ_DMA_RX_CH & 0x7) | 
							  UX_RZ_DMA_CHCFG_REQD |
                              UX_RZ_DMA_CHCFG_HIEN |
							  (0x2<<8) |
							  (0x2<<12) | (0x2<<16) |
							  UX_RZ_DMA_CHCFG_DAD);
#endif

#else
    dma_usb_in_handle = open(DEVICE_INDENTIFIER "dma_usb_in", O_WRONLY);

    dma_config.config.resource = DMA_RS_USB0_DMA0_RX;                                    /* DMA transfer resource */
	dma_config.config.source_width = DMA_DATA_SIZE_4;                                                /* DMA transfer unit size (source) - 32 bits */
	dma_config.config.destination_width = DMA_DATA_SIZE_4;                                           /* DMA transfer unit size (destination) - 32 bits */
	dma_config.config.source_address_type = DMA_ADDRESS_INCREMENT;                                   /* DMA address type (source) */
	dma_config.config.destination_address_type = DMA_ADDRESS_FIX;                                    /* DMA address type (destination) */
	dma_config.config.direction = DMA_REQUEST_SOURCE;                                                /* DMA transfer direction will be set by the driver */
	dma_config.config.source_address = USB200.D1FIFO;           /* Source Address */
	dma_config.config.destination_address = 0x00000000; /* Destination Address */
	dma_config.config.count = 0;                             /* length */
	dma_config.config.p_dmaComplete = _ux_hcd_rz_dma_rx_interrupt_handler;                                           /* set callback function (DMA end interrupt) */

	control(dma_usb_in_handle, CTL_DMAC_SET_CONFIGURATION, (void *) &dma_config);

	close(dma_usb_in_handle);
#endif
#endif

    TX_DISABLE
    
    /* Enable only the interrupts we will use: SOF, ATTCH/BCHG, BRDY, NRDY, BEMP. */
    _ux_hcd_rz_register_write(hcd_rz, UX_RZ_HC_BRDYENB, UX_RZ_HC_PIPE_ALL);
    _ux_hcd_rz_register_write(hcd_rz, UX_RZ_HC_NRDYENB, UX_RZ_HC_PIPE_ALL);
    _ux_hcd_rz_register_write(hcd_rz, UX_RZ_HC_BEMPENB, UX_RZ_HC_PIPE_ALL);
 
    _ux_hcd_rz_register_write(hcd_rz, UX_RZ_HC_INTENB0,UX_RZ_HC_INTSTS0_SOFR | UX_RZ_HC_INTENB0_BRDYE | UX_RZ_HC_INTENB0_NRDYE | UX_RZ_HC_INTENB0_BEMPE);
    _ux_hcd_rz_register_write(hcd_rz, UX_RZ_HC_INTENB1, UX_RZ_HC_INTENB1_ATTCHE | UX_RZ_HC_INTENB1_EOFERRE|  UX_RZ_HC_INTENB1_SACKE);
    
    /* Get the number of ports on the controller. The number of ports needs to be reflected both 
       for the generic HCD container and the local RZ container.  */
     hcd_rz -> ux_hcd_rz_nb_root_hubs =  1;
    hcd -> ux_hcd_nb_root_hubs =  1;

    /* All ports must now be powered to pick up device insertion.  */
    _ux_hcd_rz_power_root_hubs(hcd_rz);
    
    TX_RESTORE
    
    /* Return successful completion.  */
    return(UX_SUCCESS);
}

