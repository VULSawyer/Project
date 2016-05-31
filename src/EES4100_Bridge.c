/* EES4100 Bridge Application with patch for link lists applied
*  Using remote server addresses
*  comments:
		changed some names
*
*  With KT's comments added */

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

#include "modbus-tcp.h"		/*Modbus header includes all else required */

#define SERVER_PORT		502	/* Modbus default port number */
//#define SERVER_ADDRESS        "127.0.0.1"     /* loopback address for testing*/
#define SERVER_ADDRESS	"140.159.153.159"	/* remote Modbus server address */

#define BACNET_DEVICE_ID	44	/* assigned device Id */
#define INST_NUM		2	/* total Analog Instances */
//#define BACNET_INSTANCE_NO    12      /* used for self-testing*/
#define BACNET_PORT		0xBAC1	/* default Bacnet UDP port */
#define BACNET_INTERFACE	"lo"	/* loopback mode */
#define BACNET_DATALINK_TYPE	"bvlc"	/* Bacnet virtual link control */
#define BACNET_SELECT_TIMEOUT_MS    1	/* msec */
#define RUN_AS_BBMD_CLIENT	1	/* for if statement below */

#if RUN_AS_BBMD_CLIENT		/* BBMD =>talking to foreign device */
#define BACNET_BBMD_PORT	0xBAC0
#define BACNET_BBMD_ADDRESS	"140.159.160.7"	/* when BBMD operating */
#define BACNET_BBMD_TTL		90
#endif

//--------Setting up threads: Modbus client and BACnet server-----

/*1. Define linked list object type*/

typedef struct s_AI_object AI_object;
struct s_AI_object {
    uint16_t number;
    AI_object *next;		//next points to address for analog data
};

static AI_object *list_heads[INST_NUM];	//there will be 2 instances

/*2. Use of list lock to share the list between threads*/

static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t list_data_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t list_data_flush = PTHREAD_COND_INITIALIZER;

/*3. For the Modbus server side: Add objects to list- make sure it gets locked/unlocked*/

static void add_to_list(AI_object ** list_heads, uint16_t number)
{
    pthread_mutex_lock(&list_lock);	//LOCK to begin list operation
    printf("adding to list\n");	//test print 

    AI_object *last_object, *temp_object;	/* 2nd pointer variable for array */
    temp_object = malloc(sizeof(AI_object));	/* allocate of memory for data */
    temp_object->number = number;	/* number dereferenced to save its value */
    temp_object->next = NULL;	/* next pointer set to NULL */

    if (*list_heads == NULL) {	/* check list head addr value */
	*list_heads = temp_object;	/* put temp object at the head if empty */
    } else {
	last_object = *list_heads;	/* if not empty go to next data */
	while (last_object->next) {
	    last_object = last_object->next;	/* move through list */
	}
	last_object->next = temp_object;	/* new data */
    }

    pthread_mutex_unlock(&list_lock);	/* unlock list again */
    pthread_cond_signal(&list_data_ready);	/* for list flush operation only */
}

/*4. For the BACnet Client side: Take data from the top of the list*/

static AI_object *list_get_first(AI_object ** list_heads)
{
    AI_object *first_object;
    first_object = *list_heads;
    *list_heads = (*list_heads)->next;	/* get next value */
    return first_object;
}

/*5. BACnet READ PROPERTY */

static int Update_Analog_Input_Read_Property(BACNET_READ_PROPERTY_DATA
					     * rpdata)
{
    AI_object *first_object;	/* define object */
    int instance =
	bacnet_Analog_Input_Instance_To_Index(rpdata->object_instance);
    if (rpdata->object_property != bacnet_PROP_PRESENT_VALUE) {
	goto not_pv;		/* check for value */
    }
    pthread_mutex_lock(&list_lock);

    if (list_heads[instance] == NULL) {	/*and check the instance as well */
	pthread_mutex_unlock(&list_lock);
	goto not_pv;
    }
    first_object = list_get_first(&list_heads[instance]);
    printf("AI_Present_Value request for instance %i\n", instance);

    /* need  a current value object to push through to client */
    bacnet_Analog_Input_Present_Value_Set(instance, first_object->number);
    free(first_object);		/* get ready for next data */
    pthread_mutex_unlock(&list_lock);

  not_pv:			/*If not present value or instance null, skips here */
    printf("Not Present_Value\n");	//test print
    return bacnet_Analog_Input_Read_Property(rpdata);
}

/* SERVER OBJECTS*/

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
     NULL,			/* Iterator */
     NULL,			/* Value_Lists */
     NULL,			/* COV */
     NULL,			/* COV Clear */
     NULL /* Intrinsic Reporting */ },
    {bacnet_OBJECT_ANALOG_INPUT,
     bacnet_Analog_Input_Init,
     bacnet_Analog_Input_Count,
     bacnet_Analog_Input_Index_To_Instance,
     bacnet_Analog_Input_Valid_Instance,
     bacnet_Analog_Input_Object_Name,
     Update_Analog_Input_Read_Property,
     bacnet_Analog_Input_Write_Property,
     bacnet_Analog_Input_Property_Lists,
     NULL /* ReadRangeInfo */ ,
     NULL /* Iterator */ ,
     bacnet_Analog_Input_Encode_Value_List,
     bacnet_Analog_Input_Change_Of_Value,
     bacnet_Analog_Input_Change_Of_Value_Clear,
     bacnet_Analog_Input_Intrinsic_Reporting},
    {MAX_BACNET_OBJECT_TYPE}
};

/*REGISTER AS A FOREIGN DEVICE*/

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

/*SETTING UP MINUTE AND SECOND TIMERS*/

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

/*START OF MODBUS THREAD*/

static void *modbus_side(void *arg)	/*allocate and initialize a structure*/
{
    uint16_t tab_reg[32];
    int rv;			/* return value*/
    int i;			/* iteration */
    modbus_t *ctx;		/* pointer to structure*/

    printf("got to the modbus thread\n");	//test print       

  modbus_restart:		/* retry connection at this point*/
				/* establish the context for communication*/
    ctx = modbus_new_tcp(SERVER_ADDRESS, SERVER_PORT);

    if (ctx == NULL) {		/* structure not allocated*/
	fprintf(stderr, "Unsuccessful allocation and initialization\n");
	sleep(1);
	goto modbus_restart;
    }
    if (modbus_connect(ctx) == -1) {	/* establish connection status*/
	fprintf(stderr, "Unsuccessful connection to server: %s\n",
		modbus_strerror(errno));
	modbus_free(ctx);
	modbus_close(ctx);
	sleep(1);
	goto modbus_restart;
    } else {
	fprintf(stderr, "Successful connection to server\n");
    }

/* READ MODBUS REGISTERS*/

    while (1) {			/* assigned address info*/
	rv = modbus_read_registers(ctx, BACNET_DEVICE_ID, INST_NUM,
				   tab_reg);
	printf("got into the read registers: rv value is: %d\n", rv);//test
	if (rv == -1) {
	    fprintf(stderr, "Register read failed: %s\n",
		    modbus_strerror(errno));
	    modbus_free(ctx);
	    modbus_close(ctx);
	    sleep(1);
	    goto modbus_restart;
	}
	for (i = 0; i < rv; i++) {
	    add_to_list(&list_heads[i], tab_reg[i]);
	    printf("Register[%d] = [%d] (0x%X)\n", i, tab_reg[i],
		   tab_reg[i]);
	}
	usleep(100000);		/* 100ms sleep */
	//return 0;  designed to loop forever
    }
    printf("got to the end of modbus thread \n");	//test print       
}	//should never see end of modbus thread

/* BACnet APDU SERVICE */

#define BN_UNC(service, handler) \
    bacnet_apdu_set_unconfirmed_handler(		\
		    SERVICE_UNCONFIRMED_##service,	\
		    bacnet_handler_##handler)
#define BN_CON(service, handler) \
    bacnet_apdu_set_confirmed_handler(			\
		    SERVICE_CONFIRMED_##service,	\
		    bacnet_handler_##handler)

/* MAIN */

int main(int argc, char **argv)
{
    uint8_t rx_buf[bacnet_MAX_MPDU];
    uint16_t pdu_len;
    BACNET_ADDRESS src;
    pthread_t minute_tick_id, second_tick_id;
    pthread_t modbus_start_id;		/*added modbus thread */

    bacnet_Device_Set_Object_Instance_Number(BACNET_DEVICE_ID);
    bacnet_address_init();

    printf("got to the main\n");	//test print

    bacnet_Device_Init(server_objects);	/* Setup device objects */
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

/*holding the lock to ensure synchronised operations */

    while (1) {
	pdu_len = bacnet_datalink_receive(&src, rx_buf, bacnet_MAX_MPDU,
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
