/* EES4100 Bridge Application
*  First pass at contacting the remote server
*  Changed IP address and app name
*  Tested */

#include <stdio.h>

#include <libbacnet/address.h>	/*bacnet headers */
#include <libbacnet/device.h>
#include <libbacnet/handlers.h>
#include <libbacnet/datalink.h>
#include <libbacnet/bvlc.h>
#include <libbacnet/client.h>
#include <libbacnet/txbuf.h>
#include <libbacnet/tsm.h>
#include <libbacnet/ai.h>
#include "bacnet_namespace.h"

#include "modbus-tcp.h"		/*modbus headers include all required*/

#define BACNET_DEVICE_ID	44	/* my assigned device Id*/
//#define BACNET_INSTANCE_NO	12	/* used for self-testing*/
#define BACNET_PORT		0xBAC1	/* default Bacnet UDP port */
#define BACNET_INTERFACE	"lo"	/* loopback mode*/
#define BACNET_DATALINK_TYPE	"bvlc"	/* Bacnet virtual link control */
#define BACNET_SELECT_TIMEOUT_MS    1	/* msec */

#define SERVER_PORT		502	// Modbus default port number
#define SERVER_PORT          	0xBAC0	/*  another UDP port*/
//#define SERVER_ADDRESS	"127.0.0.1"	// loopback address for testing
#define SERVER_ADDRESS		"140.159.153.159"    //modbus server address

#define RUN_AS_BBMD_CLIENT	1		/* true so next part will run*/

#if RUN_AS_BBMD_CLIENT			/* BBMD =>talking to BACnet client */
#define BACNET_BBMD_PORT	0xBAC1
#define BACNET_BBMD_ADDRESS	"140.159.160.7"	/* when BBMD operating */
#define BACNET_BBMD_TTL		90
#endif

/* These are needed for Bacnet maintenance tasks */
static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;

//---------------------------------------------------------------------
//Arranging sequence for listing the data from Modbus server

/*1. Define linked list objects*/
typedef struct s_word_object word_object;
struct s_word_object {
    uint16_t word;	
    word_object *next;
};

static word_object *list_head;

/*2. Share list using list lock*/
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t list_data_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t list_data_flush = PTHREAD_COND_INITIALIZER;

/*3. Add objects to list*/
static void add_to_list(uint16_t word)
{
    word_object *last_object;	//more variables- have been defined?
    if (list_head == NULL) {
	last_object = malloc(sizeof(word_object));
	list_head = last_object;	//if no value, last obj to top?
    } else {
	last_object = list_head;	/*4. Allocate memory to each object */
	while (last_object->next)
	    last_object = last_object->next;
	last_object->next = malloc(sizeof(word_object));
	last_object = last_object->next;
    }
    last_object->word = word;
    last_object->next = NULL;
}

//---------------------------------------------------------------------
//gathering the data from modbus server to send to Bacnet client
//"get list head object"

static int Update_Analog_Input_Read_Property(BACNET_READ_PROPERTY_DATA *
					     rpdata)
{
    static int index;		//gets incremented below

    int instance_no =
	bacnet_Analog_Input_Instance_To_Index(rpdata->object_instance);

    if (rpdata->object_property != bacnet_PROP_PRESENT_VALUE)
	goto not_pv;

    printf("AI_Present_Value request for instance %i\n", instance_no);
    
    /* Without reconfiguring libbacnet, a maximum of 4 values may be sent */
    bacnet_Analog_Input_Present_Value_Set(0, test_data[index++]);
    bacnet_Analog_Input_Present_Value_Set(1, test_data[index++]);
    /* bacnet_Analog_Input_Present_Value_Set(2, test_data[index++]); */

    //setting up the thread for number of instances to be sent?

    if (index == NUM_TEST_DATA)
	index = 0;

  not_pv:			/*If not present value program comes here */
    printf("Not Present_Value\n");	//test print
    return bacnet_Analog_Input_Read_Property(rpdata);
}

//---------------------------------------------------------------------

static bacnet_object_functions_t server_objects[] = {
    {bacnet_OBJECT_DEVICE,
     NULL,
     bacnet_Device_Count,
     bacnet_Device_Index_To_Instance,
     bacnet_Device_Valid_Object_Instance_Number,
     bacnet_Device_Object_Name,
     bacnet_Device_Read_Property_Local,
     bacnet_Device_Write_Property_Local,
     bacnet_Device_Property_Lists,
     bacnet_DeviceGetRRInfo,
     NULL,	/* Iterator */
     NULL,	/* Value_Lists */
     NULL,	/* COV */
     NULL,	/* COV Clear */
     NULL 	/* Intrinsic Reporting */ },
    {bacnet_OBJECT_ANALOG_INPUT,
     bacnet_Analog_Input_Init,
     bacnet_Analog_Input_Count,
     bacnet_Analog_Input_Index_To_Instance,
     bacnet_Analog_Input_Valid_Instance,
     bacnet_Analog_Input_Object_Name,
     Update_Analog_Input_Read_Property,
     bacnet_Analog_Input_Write_Property,
     bacnet_Analog_Input_Property_Lists,
     NULL 	/* ReadRangeInfo */ ,
     NULL 	/* Iterator */ ,
     bacnet_Analog_Input_Encode_Value_List,
     bacnet_Analog_Input_Change_Of_Value,
     bacnet_Analog_Input_Change_Of_Value_Clear,
     bacnet_Analog_Input_Intrinsic_Reporting},
    {MAX_BACNET_OBJECT_TYPE}
};

//---------------------------------------------------------------------

/*Running as BBMD client*/
static void register_with_bbmd(void)
{
#if RUN_AS_BBMD_CLIENT
    /* Thread safety: Shares data with datalink_send_pdu */
    bacnet_bvlc_register_with_bbmd(bacnet_bip_getaddrbyname
				   (BACNET_BBMD_ADDRESS),
				   htons(BACNET_BBMD_PORT),
				   BACNET_BBMD_TTL);
#endif
}

//---------------------------------------------------------------------

/*Setting up minute and second timers*/
static void *minute_tick(void *arg)
{
    while (1) {
	pthread_mutex_lock(&timer_lock);

	/* Expire addresses once the TTL has expired */
	bacnet_address_cache_timer(60);

	/* Re-register with BBMD once BBMD TTL has expired */
	register_with_bbmd();

	/*Update addresses for notification class recipient list 
	 * Requred for INTRINSIC_REPORTING
	 * bacnet_Notification_Class_find_recipient(); */

	/* Sleep for 1 minute */
	pthread_mutex_unlock(&timer_lock);
	sleep(60);
    }
    return arg;
}
static void *second_tick(void *arg)
{
    while (1) {
	pthread_mutex_lock(&timer_lock);

	/* Invalidates stale BBMD foreign device table entries */
	bacnet_bvlc_maintenance_timer(1);

	/* Transaction state machine: Responsible for retransmissions 
	 * and ack checking for confirmed services */
	bacnet_tsm_timer_milliseconds(1000);

	/*removed more bacnet functions not used here */

	/* Sleep for 1 second */
	pthread_mutex_unlock(&timer_lock);
	sleep(1);
    }
    return arg;
}

//---------------------------------------------------------------------
//MODBUS THREAD IN HERE

static void *modbus_side(void *arg)	//allocate and initialize a structure
{
    uint16_t tab_reg[32];
    int rv;			//return value
    int i;			//iteration 
    modbus_t *ctx;		//pointer to structure

    printf("got to the modbus thread\n");	//test print       

  modbus_restart:		//retry connection at this point

    ctx = modbus_new_tcp(SERVER_ADDRESS, SERVER_PORT);

    if (ctx == NULL) {		//structure not allocated
	fprintf(stderr, "Unsuccessful allocation and initialization\n");
	sleep(1);
	goto modbus_restart;
    }
    if (modbus_connect(ctx) == -1) {	//establish connection status
	fprintf(stderr, "Unsuccessful connection to server: %s\n",
		modbus_strerror(errno));
	modbus_free(ctx);
	modbus_close(ctx);
	sleep(1);
	goto modbus_restart;
    } else {
	fprintf(stderr, "Successful connection to server\n");
    }

//read modbus registers 

    while (1) {				//assigned address info
	rv = modbus_read_registers(ctx, BACNET_DEVICE_ID, 2, tab_reg);
	printf("got into the read regs- rv value is: %d\n", rv);	//test print
	if (rv == -1) {
	    fprintf(stderr, "Register read failed: %s\n",
		    modbus_strerror(errno));
	    modbus_free(ctx);
	    modbus_close(ctx);
	    sleep(1);
	    goto modbus_restart;
	}
	for (i = 0; i < rv; i++) {
	    //add_to_list(&list_head[i], tab_reg[i]);
	    printf("Register[%d] = [%d] (0x%X)\n", i,
		   tab_reg[i], tab_reg[i]);
	}
	usleep(100000);		//100ms sleep
	//return 0;
    }
    printf("got to the end of modbus thread \n");	//test print       
}

//end of modbus thread
//---------------------------------------------------------------------

#define BN_UNC(service, handler) \
    bacnet_apdu_set_unconfirmed_handler(		\
		    SERVICE_UNCONFIRMED_##service,	\
		    bacnet_handler_##handler)
#define BN_CON(service, handler) \
    bacnet_apdu_set_confirmed_handler(			\
		    SERVICE_CONFIRMED_##service,	\
		    bacnet_handler_##handler)
//---------------------------------------------------------------------
//MAIN 

int main(int argc, char **argv)
{
    uint8_t rx_buf[bacnet_MAX_MPDU];
    uint16_t pdu_len;
    BACNET_ADDRESS src;
    pthread_t minute_tick_id, second_tick_id;
    pthread_t modbus_start_id;	/*added modbus thread */
    bacnet_Device_Set_Object_Instance_Number(BACNET_INSTANCE_NO);
    bacnet_address_init();

    printf("got to the main\n");	//test print

/* Setup device objects */
    bacnet_Device_Init(server_objects);
    BN_UNC(WHO_IS, who_is);
    BN_CON(READ_PROPERTY, read_property);

    bacnet_BIP_Debug = true;
    bacnet_bip_set_port(htons(BACNET_PORT));
    bacnet_datalink_set(BACNET_DATALINK_TYPE);
    bacnet_datalink_init(BACNET_INTERFACE);
    atexit(bacnet_datalink_cleanup);
    memset(&src, 0, sizeof(src));

    register_with_bbmd();

    bacnet_Send_I_Am(bacnet_Handler_Transmit_Buffer);

    pthread_create(&minute_tick_id, 0, minute_tick, NULL);
    pthread_create(&second_tick_id, 0, second_tick, NULL);
    pthread_create(&modbus_start_id, 0, modbus_side, NULL);

//---------------------------------------------------------------------
/*holding the lock to ensure synchronised operations */

    while (1) {
	pdu_len =
	    bacnet_datalink_receive(&src, rx_buf, bacnet_MAX_MPDU,
				    BACNET_SELECT_TIMEOUT_MS);
	if (pdu_len) {
	    /* May call any registered handler.
	     * Thread safety: May block, however we still need to guarantee
	     * atomicity with the timers, so hold the lock anyway */
	    pthread_mutex_lock(&timer_lock);
	    bacnet_npdu_handler(&src, rx_buf, pdu_len);
	    pthread_mutex_unlock(&timer_lock);
	}
    }
    return 0;
}				//END OF PROGRAM
